#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QScopedPointer>
#include <QStringList>
#include <QTimer>
#include <QtTest>
#include <chrono>
#include <memory>

#include <client/Interceptor.h>
#include <client/NetworkClient.h>
#include <client/NetworkException.h>
#include <request/HttpRequest.h>

// 默认打 httpbin.org 的公网服务，也可以用 HTTPBIN_URL 指向本地实例：
//   docker run -p 8080:80 kennethreitz/httpbin
// 走 http 而不是 https，免得依赖 Qt 的 OpenSSL DLL

// ---------- 响应模型 ----------

// GET /get 的响应，只声明关心的字段（序列化器默认 IgnoreUnknownKeys）
class EchoModel
{
	Q_GADGET
	Q_PROPERTY(QString url READ url WRITE setUrl)
	Q_PROPERTY(QString origin READ origin WRITE setOrigin)

public:
	QString url() const { return m_url; }
	void setUrl(const QString& url) { m_url = url; }
	QString origin() const { return m_origin; }
	void setOrigin(const QString& origin) { m_origin = origin; }

private:
	QString m_url;
	QString m_origin;
};
Q_DECLARE_METATYPE(EchoModel)

// POST /post 把请求体原样放在 data 字段
class DataEcho
{
	Q_GADGET
	Q_PROPERTY(QString data READ data WRITE setData)

public:
	QString data() const { return m_data; }
	void setData(const QString& data) { m_data = data; }

private:
	QString m_data;
};
Q_DECLARE_METATYPE(DataEcho)

// POST /post 上传的文件放在 files 里，键名就是表单字段名
class FilesEcho
{
	Q_GADGET
	Q_PROPERTY(QString file READ file WRITE setFile)

public:
	QString file() const { return m_file; }
	void setFile(const QString& file) { m_file = file; }

private:
	QString m_file;
};
Q_DECLARE_METATYPE(FilesEcho)

class UploadEcho
{
	Q_GADGET
	Q_PROPERTY(FilesEcho files READ files WRITE setFiles)

public:
	FilesEcho files() const { return m_files; }
	void setFiles(const FilesEcho& files) { m_files = files; }

private:
	FilesEcho m_files;
};
Q_DECLARE_METATYPE(UploadEcho)

// ---------- 请求声明 ----------

using GetEcho = NetCore::HttpRequest<
	NetCore::HttpMethod::GET,
	STR("get"),
	EchoModel,
	NetCore::Query<STR("page"), NetCore::TypeInt>>;

using GetBytesVoid = NetCore::HttpRequest<
	NetCore::HttpMethod::GET,
	STR("bytes/16"),
	void>;

using GetBytesAsModel = NetCore::HttpRequest<
	NetCore::HttpMethod::GET,
	STR("bytes/16"),
	EchoModel>;

using GetNotFound = NetCore::HttpRequest<
	NetCore::HttpMethod::GET,
	STR("status/404"),
	void>;

using GetServerError = NetCore::HttpRequest<
	NetCore::HttpMethod::GET,
	STR("status/500"),
	void>;

using GetSlow = NetCore::HttpRequest<
	NetCore::HttpMethod::GET,
	STR("delay/5"),
	void>;

using DeleteThing = NetCore::HttpRequest<
	NetCore::HttpMethod::DEL,
	STR("delete"),
	void>;

// 不显式声明 Content-Type 的话 Qt 会默认按表单发，httpbin 就会把 body
// 解析进 form 字段而不是原样回显到 data
using PostText = NetCore::HttpRequest<
	NetCore::HttpMethod::POST,
	STR("post"),
	DataEcho,
	NetCore::Header<STR("Content-Type"), NetCore::TypeString>,
	NetCore::Body<NetCore::TypeString>>;

using UploadFile = NetCore::HttpMultipartRequest<
	NetCore::HttpMethod::POST,
	STR("post"),
	UploadEcho,
	NetCore::Form<STR("file"), NetCore::TypeFile>>;

// ---------- 记录调用顺序的拦截器 ----------

class RecordingInterceptor : public NetCore::HttpInterceptor
{
public:
	RecordingInterceptor(QStringList* log, QString name)
		: log_(log)
		, name_(std::move(name))
	{
	}

	QtPromise::QPromise<QByteArray> intercept(
		const QNetworkRequest&, NetCore::NextHandler next) override
	{
		log_->append(name_);
		return next();
	}

private:
	QStringList* log_;
	QString name_;
};

class TestNetworkClient : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void builderIsChainable();
	void getDeserializesJson();
	void voidResponseSkipsDeserialization();
	void badJsonRaisesSerializationError();
	void httpErrorCarriesStatus();
	void requestTimeoutAborts();
	void deleteReturnsVoid();
	void postSendsBody();
	void uploadsMultipart();
	void interceptorChainRunsOutsideIn();
	void retriesOnServerError();

private:
	std::shared_ptr<NetCore::NetworkClient> makeClient(
		std::chrono::milliseconds timeout = std::chrono::milliseconds(15000)) const;
	void requireNetwork() const;

	QString base_url_;
	bool network_ok_ = false;
};

void TestNetworkClient::initTestCase()
{
	base_url_ = qEnvironmentVariable("HTTPBIN_URL", QStringLiteral("http://httpbin.org/"));
	if (!base_url_.endsWith(QLatin1Char('/'))) base_url_ += QLatin1Char('/');

	// 探活：不通就跳过网络用例，而不是让整个套件红掉
	QNetworkAccessManager nam;
	QScopedPointer<QNetworkReply> reply(nam.get(QNetworkRequest(QUrl(base_url_ + "status/200"))));

	QEventLoop loop;
	QTimer::singleShot(10000, &loop, &QEventLoop::quit);
	connect(reply.data(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();

	network_ok_ = reply->isFinished() && reply->error() == QNetworkReply::NoError;
	if (!network_ok_) {
		qWarning("%s 不可达，网络用例将跳过", qPrintable(base_url_));
	}
}

std::shared_ptr<NetCore::NetworkClient> TestNetworkClient::makeClient(
	std::chrono::milliseconds timeout) const
{
	return NetCore::NetworkClientBuilder()
		.baseUrl(base_url_)
		.totalTimeout(timeout)
		.build();
}

void TestNetworkClient::requireNetwork() const
{
	if (!network_ok_) QSKIP("httpbin 不可达");
}

void TestNetworkClient::builderIsChainable()
{
	auto retry = std::make_shared<NetCore::RetryInterceptor>();

	auto client = NetCore::NetworkClientBuilder()
		.baseUrl(QStringLiteral("http://example.invalid/"))
		.totalTimeout(std::chrono::milliseconds(1234))
		.addInterceptor(retry)
		.followRedirects(QNetworkRequest::NoLessSafeRedirectPolicy, 5)
		.build();

	QVERIFY(client != nullptr);
}

void TestNetworkClient::getDeserializesJson()
{
	requireNetwork();

	auto client = makeClient();
	EchoModel got;
	bool settled = false;
	bool failed = false;

	client->sendRequest(GetEcho::make(2))
		.then([&](const EchoModel& echo) { got = echo; settled = true; })
		.fail([&](const NetCore::NetworkException& ex) {
			failed = settled = true;
			qWarning("%s", qPrintable(ex.error().message()));
		});

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QVERIFY(!failed);
	QVERIFY(got.url().contains(QStringLiteral("/get")));
	QVERIFY(got.url().contains(QStringLiteral("page=2")));	// Query 拼接生效
}

void TestNetworkClient::voidResponseSkipsDeserialization()
{
	requireNetwork();

	// /bytes 返回的是随机二进制，void 响应压根不解析，所以不该报错
	auto client = makeClient();
	bool settled = false;
	bool failed = false;

	client->sendRequest(GetBytesVoid::make())
		.then([&]() { settled = true; })
		.fail([&](const NetCore::NetworkException&) { failed = settled = true; });

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QVERIFY(!failed);
}

void TestNetworkClient::badJsonRaisesSerializationError()
{
	requireNetwork();

	// 同一个端点，换成有类型的响应就该在反序列化这步失败
	auto client = makeClient();
	bool settled = false;
	NetCore::NetworkClientError::Category category{};

	client->sendRequest(GetBytesAsModel::make())
		.then([&](const EchoModel&) { settled = true; })
		.fail([&](const NetCore::NetworkException& ex) {
			category = ex.error().category();
			settled = true;
		});

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QCOMPARE(category, NetCore::NetworkClientError::Category::Serialization);
}

void TestNetworkClient::httpErrorCarriesStatus()
{
	requireNetwork();

	auto client = makeClient();
	bool settled = false;
	int status = 0;

	client->sendRequest(GetNotFound::make())
		.then([&]() { settled = true; })
		.fail([&](const NetCore::NetworkException& ex) {
			settled = true;
			if (const auto http = ex.error().asHttp()) status = http->status;
		});

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QCOMPARE(status, 404);
}

void TestNetworkClient::requestTimeoutAborts()
{
	requireNetwork();

	// 服务端要拖 5 秒，请求级超时 800ms 应该先触发并 abort 掉 reply
	auto client = makeClient();
	auto request = GetSlow::make();
	request.withTimeout(800);

	bool settled = false;
	bool timed_out = false;
	QElapsedTimer clock;
	clock.start();

	client->sendRequest(request)
		.then([&]() { settled = true; })
		.fail([&](const NetCore::NetworkException& ex) {
			timed_out = ex.error().isTimeout();
			settled = true;
		});

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QVERIFY(timed_out);
	QVERIFY(clock.elapsed() < 4000);	// 没有干等到服务端那 5 秒
}

void TestNetworkClient::deleteReturnsVoid()
{
	requireNetwork();

	auto client = makeClient();
	bool settled = false;
	bool failed = false;

	client->sendRequest(DeleteThing::make())
		.then([&]() { settled = true; })
		.fail([&](const NetCore::NetworkException&) { failed = settled = true; });

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QVERIFY(!failed);
}

void TestNetworkClient::postSendsBody()
{
	requireNetwork();

	auto client = makeClient();
	DataEcho got;
	bool settled = false;
	bool failed = false;

	client->sendRequest(PostText::make("text/plain", "hello from network-framework"))
		.then([&](const DataEcho& echo) { got = echo; settled = true; })
		.fail([&](const NetCore::NetworkException&) { failed = settled = true; });

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QVERIFY(!failed);
	QCOMPARE(got.data(), QStringLiteral("hello from network-framework"));
}

void TestNetworkClient::uploadsMultipart()
{
	requireNetwork();

	auto client = makeClient();
	UploadEcho got;
	bool settled = false;
	bool failed = false;

	const auto file = NetCore::FileValue::fromBytes(
		"multipart payload", QStringLiteral("note.txt"), QStringLiteral("text/plain"));

	client->sendRequest(UploadFile::make(file))
		.then([&](const UploadEcho& echo) { got = echo; settled = true; })
		.fail([&](const NetCore::NetworkException& ex) {
			failed = settled = true;
			qWarning("%s", qPrintable(ex.error().message()));
		});

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QVERIFY(!failed);
	QCOMPARE(got.files().file(), QStringLiteral("multipart payload"));
}

void TestNetworkClient::interceptorChainRunsOutsideIn()
{
	requireNetwork();

	QStringList log;
	auto client = NetCore::NetworkClientBuilder()
		.baseUrl(base_url_)
		.totalTimeout(std::chrono::milliseconds(15000))
		.addInterceptor(std::make_shared<RecordingInterceptor>(&log, QStringLiteral("A")))
		.addInterceptor(std::make_shared<RecordingInterceptor>(&log, QStringLiteral("B")))
		.addInterceptor(std::make_shared<RecordingInterceptor>(&log, QStringLiteral("C")))
		.build();

	bool settled = false;
	client->sendRequest(GetBytesVoid::make())
		.then([&]() { settled = true; })
		.fail([&](const NetCore::NetworkException&) { settled = true; });

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QCOMPARE(log, QStringList({ QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C") }));
}

void TestNetworkClient::retriesOnServerError()
{
	requireNetwork();

	// 计数器加在重试拦截器之后 -> 位于它内层 -> 每次重试都会重新经过它
	QStringList log;
	NetCore::RetryInterceptor::Policy policy;
	policy.maxRetries = 2;
	policy.delay = std::chrono::milliseconds(50);

	auto client = NetCore::NetworkClientBuilder()
		.baseUrl(base_url_)
		.totalTimeout(std::chrono::milliseconds(15000))
		.addInterceptor(std::make_shared<NetCore::RetryInterceptor>(policy))
		.addInterceptor(std::make_shared<RecordingInterceptor>(&log, QStringLiteral("try")))
		.build();


	bool settled = false;
	int status = 0;
	client->sendRequest(GetServerError::make())
		.then([&]() { settled = true; })
		.fail([&](const NetCore::NetworkException& ex) {
			settled = true;
			if (const auto http = ex.error().asHttp()) status = http->status;
		});

	QTRY_VERIFY_WITH_TIMEOUT(settled, 30000);
	QCOMPARE(status, 500);
	QCOMPARE(log.size(), 3);	// 首次 + 两次重试
}

QTEST_GUILESS_MAIN(TestNetworkClient)
#include "tst_networkclient.moc"
