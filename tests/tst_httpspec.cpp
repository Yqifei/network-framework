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
#include <client/NetworkException.h>
#include <request/HttpSpec.h>

// 网络用例打 httpbin，可用 HTTPBIN_URL 指向本地实例

class SpecModel
{
	Q_GADGET
	Q_PROPERTY(QString name READ name WRITE setName)

public:
	QString name() const { return m_name; }
	void setName(const QString& name) { m_name = name; }

private:
	QString m_name;
};
Q_DECLARE_METATYPE(SpecModel)

// 记录 sendSpec 是否也走了拦截器链
class RecordingInterceptor : public NetCore::HttpInterceptor
{
public:
	explicit RecordingInterceptor(int* counter)
		: counter_(counter)
	{
	}

	QtPromise::QPromise<QByteArray> intercept(
		const QNetworkRequest&, NetCore::NextHandler next) override
	{
		++(*counter_);
		return next();
	}

private:
	int* counter_;
};

namespace {

struct SpecResult {
	QByteArray body;
	bool ok = false;
	bool settled = false;
};

void awaitBytes(QtPromise::QPromise<QByteArray> promise, SpecResult& out)
{
	promise
		.then([&out](const QByteArray& data) {
			out.body = data;
			out.ok = true;
			out.settled = true;
		})
		.fail([&out]() { out.settled = true; });
}

} // namespace

class TestHttpSpec : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void factoriesSetMethod();
	void chainingReturnsSelf();
	void bodyJsonSerializesObject();
	void bodyJsonSerializesScalar();
	void bodyModelSerializes();
	void bodyFormEncodes();
	void bodyRawKeepsData();
	void filePartMakesMultipart();
	void timeoutOverrideStored();

	void sendSpecEchoesQueryAndHeader();
	void sendSpecPostsJsonBody();
	void sendSpecUploadsMultipart();
	void sendSpecGoesThroughInterceptors();

private:
	std::shared_ptr<NetCore::NetworkClient> makeClient() const;
	void requireNetwork() const;

	QString base_url_;
	bool network_ok_ = false;
};

void TestHttpSpec::initTestCase()
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

std::shared_ptr<NetCore::NetworkClient> TestHttpSpec::makeClient() const
{
	return NetCore::NetworkClientBuilder()
		.baseUrl(base_url_)
		.totalTimeout(std::chrono::milliseconds(15000))
		.build();
}

void TestHttpSpec::requireNetwork() const
{
	if (!network_ok_) QSKIP("httpbin 不可达");
}

void TestHttpSpec::factoriesSetMethod()
{
	const QString url = QStringLiteral("http://example.invalid/thing");

	QCOMPARE(NetCore::HttpSpec::get(url).method(), NetCore::HttpMethod::GET);
	QCOMPARE(NetCore::HttpSpec::post(url).method(), NetCore::HttpMethod::POST);
	QCOMPARE(NetCore::HttpSpec::put(url).method(), NetCore::HttpMethod::PUT);
	QCOMPARE(NetCore::HttpSpec::del(url).method(), NetCore::HttpMethod::DEL);
	QCOMPARE(NetCore::HttpSpec::patch(url).method(), NetCore::HttpMethod::PATCH);
}

void TestHttpSpec::chainingReturnsSelf()
{
	auto spec = NetCore::HttpSpec::get(QStringLiteral("http://example.invalid/"));

	QCOMPARE(&spec.header("X-A", "1"), &spec);
	QCOMPARE(&spec.query(QStringLiteral("k"), QStringLiteral("v")), &spec);
	QCOMPARE(&spec.timeout(std::chrono::milliseconds(100)), &spec);
}

void TestHttpSpec::bodyJsonSerializesObject()
{
	QJsonObject payload;
	payload.insert(QStringLiteral("a"), 1);
	payload.insert(QStringLiteral("b"), QStringLiteral("x"));

	auto spec = NetCore::HttpSpec::post(QStringLiteral("http://example.invalid/"));
	spec.bodyJson(payload);

	QCOMPARE(spec.body(), QByteArray(R"({"a":1,"b":"x"})"));
}

void TestHttpSpec::bodyJsonSerializesScalar()
{
	auto number = NetCore::HttpSpec::post(QStringLiteral("http://example.invalid/"));
	number.bodyJson(QJsonValue(42));
	QCOMPARE(number.body(), QByteArray("42"));

	// 字符串标量要带引号才是合法 JSON
	auto text = NetCore::HttpSpec::post(QStringLiteral("http://example.invalid/"));
	text.bodyJson(QJsonValue(QStringLiteral("hi")));
	QCOMPARE(text.body(), QByteArray("\"hi\""));
}

void TestHttpSpec::bodyModelSerializes()
{
	SpecModel model;
	model.setName(QStringLiteral("dynamic"));

	auto spec = NetCore::HttpSpec::post(QStringLiteral("http://example.invalid/"));
	spec.bodyModel(model);

	QCOMPARE(spec.body(), QByteArray(R"({"name":"dynamic"})"));
}

void TestHttpSpec::bodyFormEncodes()
{
	QHash<QString, QString> form;
	form.insert(QStringLiteral("user"), QStringLiteral("alice smith"));

	auto spec = NetCore::HttpSpec::post(QStringLiteral("http://example.invalid/"));
	spec.bodyForm(form);

	// 不写死 %20 还是 +，只要求确实做了编码
	QVERIFY(spec.body().startsWith("user=alice"));
	QVERIFY(!spec.body().contains(' '));
}

void TestHttpSpec::bodyRawKeepsData()
{
	auto spec = NetCore::HttpSpec::post(QStringLiteral("http://example.invalid/"));
	spec.bodyRaw(QByteArray("\x01\x02\x03", 3), "application/octet-stream");

	QCOMPARE(spec.body(), QByteArray("\x01\x02\x03", 3));
}

void TestHttpSpec::filePartMakesMultipart()
{
	auto spec = NetCore::HttpSpec::post(QStringLiteral("http://example.invalid/"));
	QVERIFY(!spec.isMultipart());

	spec.filePart("file", NetCore::FileValue::fromBytes(
		"payload", QStringLiteral("a.txt"), QStringLiteral("text/plain")));
	QVERIFY(spec.isMultipart());
}

void TestHttpSpec::timeoutOverrideStored()
{
	auto spec = NetCore::HttpSpec::get(QStringLiteral("http://example.invalid/"));
	QCOMPARE(spec.timeoutOverride(), -1);

	spec.timeout(std::chrono::seconds(5));
	QCOMPARE(spec.timeoutOverride(), 5000);
}

void TestHttpSpec::sendSpecEchoesQueryAndHeader()
{
	requireNetwork();

	auto client = makeClient();
	auto spec = NetCore::HttpSpec::get(base_url_ + "get");
	spec.query(QStringLiteral("page"), QStringLiteral("7")).header("X-Custom", "spec-test");

	SpecResult result;
	awaitBytes(client->sendSpec(spec), result);

	QTRY_VERIFY_WITH_TIMEOUT(result.settled, 20000);
	QVERIFY(result.ok);

	const QJsonObject root = QJsonDocument::fromJson(result.body).object();
	QVERIFY(root.value(QStringLiteral("url")).toString().contains(QStringLiteral("page=7")));
	QCOMPARE(root.value(QStringLiteral("headers")).toObject()
			.value(QStringLiteral("X-Custom")).toString(),
		QStringLiteral("spec-test"));
}

void TestHttpSpec::sendSpecPostsJsonBody()
{
	requireNetwork();

	QJsonObject payload;
	payload.insert(QStringLiteral("name"), QStringLiteral("spec"));

	auto client = makeClient();
	auto spec = NetCore::HttpSpec::post(base_url_ + "post");
	spec.bodyJson(payload);

	SpecResult result;
	awaitBytes(client->sendSpec(spec), result);

	QTRY_VERIFY_WITH_TIMEOUT(result.settled, 20000);
	QVERIFY(result.ok);

	// Content-Type 是 application/json，httpbin 才会把它解析进 json 字段
	const QJsonObject root = QJsonDocument::fromJson(result.body).object();
	QCOMPARE(root.value(QStringLiteral("json")).toObject()
			.value(QStringLiteral("name")).toString(),
		QStringLiteral("spec"));
}

void TestHttpSpec::sendSpecUploadsMultipart()
{
	requireNetwork();

	auto client = makeClient();
	auto spec = NetCore::HttpSpec::post(base_url_ + "post");
	spec.filePart("file", NetCore::FileValue::fromBytes(
			"spec upload", QStringLiteral("note.txt"), QStringLiteral("text/plain")))
		.formPart("tag", QStringLiteral("demo"));

	SpecResult result;
	awaitBytes(client->sendSpec(spec), result);

	QTRY_VERIFY_WITH_TIMEOUT(result.settled, 20000);
	QVERIFY(result.ok);

	const QJsonObject root = QJsonDocument::fromJson(result.body).object();
	QCOMPARE(root.value(QStringLiteral("files")).toObject()
			.value(QStringLiteral("file")).toString(),
		QStringLiteral("spec upload"));
	QCOMPARE(root.value(QStringLiteral("form")).toObject()
			.value(QStringLiteral("tag")).toString(),
		QStringLiteral("demo"));
}

// sendSpec 和 sendRequest 走的是同一条拦截器链
void TestHttpSpec::sendSpecGoesThroughInterceptors()
{
	requireNetwork();

	int calls = 0;
	auto client = NetCore::NetworkClientBuilder()
		.baseUrl(base_url_)
		.totalTimeout(std::chrono::milliseconds(15000))
		.addInterceptor(std::make_shared<RecordingInterceptor>(&calls))
		.build();

	SpecResult result;
	awaitBytes(client->sendSpec(NetCore::HttpSpec::get(base_url_ + "get")), result);

	QTRY_VERIFY_WITH_TIMEOUT(result.settled, 20000);
	QVERIFY(result.ok);
	QCOMPARE(calls, 1);
}

QTEST_GUILESS_MAIN(TestHttpSpec)
#include "tst_httpspec.moc"
