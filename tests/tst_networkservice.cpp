#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QScopedPointer>
#include <QTimer>
#include <QtTest>
#include <chrono>
#include <memory>

#include <client/NetworkClient.h>
#include <request/HttpSpec.h>
#include <service/NetworkService.h>

// 网络用例打 httpbin，可用 HTTPBIN_URL 指向本地实例

// httpbin 把请求头 Title-Case 后回显；属性名不能带连字符，所以只取 Accept
class HeadersEcho
{
	Q_GADGET
	Q_PROPERTY(QString Accept MEMBER accept)

public:
	QString accept;
};
Q_DECLARE_METATYPE(HeadersEcho)

// 声明成 QObject 子类，响应类型写 EchoResponse*，走堆上反序列化那条路径。
// 这里用 READ/WRITE 而不是 MEMBER：moc 会给 MEMBER 属性生成 != 比较，
// 而 HeadersEcho 这个 Q_GADGET 没有相等运算符
class EchoResponse : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString url READ url WRITE setUrl)
	Q_PROPERTY(HeadersEcho headers READ headers WRITE setHeaders)

public:
	using QObject::QObject;

	QString url() const { return m_url; }
	void setUrl(const QString& url) { m_url = url; }
	HeadersEcho headers() const { return m_headers; }
	void setHeaders(const HeadersEcho& headers) { m_headers = headers; }

private:
	QString m_url;
	HeadersEcho m_headers;
};

using GetEcho = NetCore::HttpRequest<NetCore::HttpMethod::GET, STR("get"), EchoResponse*>;
using DeleteVoid = NetCore::HttpRequest<NetCore::HttpMethod::DEL, STR("delete"), void>;
using GetNotFound = NetCore::HttpRequest<NetCore::HttpMethod::GET, STR("status/404"), void>;

class TestService : public NetCore::NetworkService
{
public:
	using NetworkService::NetworkService;

	QHash<QByteArray, QByteArray> commonHeaders() const override
	{
		return { { "Accept", "application/json" }, { "X-Api-Key", "secret" } };
	}

	// 测试里需要直接调这几个 protected 方法
	using NetworkService::httpCall;
	using NetworkService::httpPromise;
	using NetworkService::httpSpecPromise;
};

class TestNetworkService : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void httpCallDeliversSuccess();
	void httpCallInjectsCommonHeaders();
	void httpCallReportsFailure();
	void httpCallWithoutFailCallback();
	void voidResponseCallbackTakesNoArgs();
	void httpPromiseResolves();
	void httpSpecPromiseInjectsCommonHeaders();

private:
	std::shared_ptr<TestService> makeService() const;
	void requireNetwork() const;

	QString base_url_;
	bool network_ok_ = false;
};

void TestNetworkService::initTestCase()
{
	base_url_ = qEnvironmentVariable("HTTPBIN_URL", QStringLiteral("http://httpbin.org/"));
	if (!base_url_.endsWith(QLatin1Char('/'))) base_url_ += QLatin1Char('/');

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

std::shared_ptr<TestService> TestNetworkService::makeService() const
{
	auto client = NetCore::NetworkClientBuilder()
		.baseUrl(base_url_)
		.totalTimeout(std::chrono::milliseconds(15000))
		.build();
	return std::make_shared<TestService>(client);
}

void TestNetworkService::requireNetwork() const
{
	if (!network_ok_) QSKIP("httpbin 不可达");
}

void TestNetworkService::httpCallDeliversSuccess()
{
	requireNetwork();

	auto service = makeService();
	EchoResponse* got = nullptr;
	bool settled = false;

	service->httpCall<GetEcho>(GetEcho::make(),
		[&](EchoResponse* echo) { got = echo; settled = true; },
		[&](const NetCore::NetworkClientError&) { settled = true; });

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QVERIFY(got != nullptr);
	QVERIFY(got->url().contains(QStringLiteral("/get")));
	delete got;
}

void TestNetworkService::httpCallInjectsCommonHeaders()
{
	requireNetwork();

	auto service = makeService();
	EchoResponse* got = nullptr;
	bool settled = false;

	service->httpCall<GetEcho>(GetEcho::make(),
		[&](EchoResponse* echo) { got = echo; settled = true; },
		[&](const NetCore::NetworkClientError&) { settled = true; });

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QVERIFY(got != nullptr);
	QCOMPARE(got->headers().accept, QStringLiteral("application/json"));
	delete got;
}

void TestNetworkService::httpCallReportsFailure()
{
	requireNetwork();

	auto service = makeService();
	int status = 0;
	bool settled = false;

	service->httpCall<GetNotFound>(GetNotFound::make(),
		[&]() { settled = true; },
		[&](const NetCore::NetworkClientError& error) {
			if (const auto http = error.asHttp()) status = http->status;
			settled = true;
		});

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QCOMPARE(status, 404);
}

void TestNetworkService::httpCallWithoutFailCallback()
{
	requireNetwork();

	// 只给成功回调的重载，失败时静默忽略，不该崩
	auto service = makeService();
	bool settled = false;

	service->httpCall<GetNotFound>(GetNotFound::make(), [&]() { settled = true; });

	QTest::qWait(3000);
	QVERIFY(!settled);	// 请求是 404，成功回调不该被调用
}

void TestNetworkService::voidResponseCallbackTakesNoArgs()
{
	requireNetwork();

	auto service = makeService();
	bool ok = false;
	bool settled = false;

	service->httpCall<DeleteVoid>(DeleteVoid::make(),
		[&]() { ok = settled = true; },
		[&](const NetCore::NetworkClientError&) { settled = true; });

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QVERIFY(ok);
}

void TestNetworkService::httpPromiseResolves()
{
	requireNetwork();

	auto service = makeService();
	EchoResponse* got = nullptr;
	bool settled = false;

	service->httpPromise<GetEcho>(GetEcho::make())
		.then([&](EchoResponse* echo) { got = echo; settled = true; })
		.fail([&]() { settled = true; });

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);
	QVERIFY(got != nullptr);
	QVERIFY(got->url().contains(QStringLiteral("/get")));
	delete got;
}

void TestNetworkService::httpSpecPromiseInjectsCommonHeaders()
{
	requireNetwork();

	auto service = makeService();
	QByteArray raw;
	bool settled = false;

	service->httpSpecPromise(NetCore::HttpSpec::get(base_url_ + "get"))
		.then([&](const QByteArray& data) { raw = data; settled = true; })
		.fail([&]() { settled = true; });

	QTRY_VERIFY_WITH_TIMEOUT(settled, 20000);

	const QJsonObject headers =
		QJsonDocument::fromJson(raw).object().value(QStringLiteral("headers")).toObject();
	QCOMPARE(headers.value(QStringLiteral("X-Api-Key")).toString(), QStringLiteral("secret"));
}

QTEST_GUILESS_MAIN(TestNetworkService)
#include "tst_networkservice.moc"
