#include <QtTest>
#include <QScopedPointer>
#include <stdexcept>
#include <request/HttpRequest.h>
#include <request/RequestConverter.h>
#include <request/RequestAttrCode.h>

// ---------- 测试类型 ----------

class NoteModel
{
	Q_GADGET
	Q_PROPERTY(QString text READ text WRITE setText)

public:
	QString text() const { return m_text; }
	void setText(const QString& text) { m_text = text; }

private:
	QString m_text;
};
Q_DECLARE_METATYPE(NoteModel)

// ---------- 请求声明 ----------

using GetUser = NetCore::HttpRequest<
	NetCore::HttpMethod::GET,
	STR("api/users/{id}"),
	void,
	NetCore::Path<STR("id"), NetCore::TypeInt>,
	NetCore::Query<STR("page"), NetCore::TypeInt>>;

using HeaderRequest = NetCore::HttpRequest<
	NetCore::HttpMethod::GET,
	STR("api/ping"),
	void,
	NetCore::Header<STR("X-Custom"), NetCore::TypeString>>;

using BinaryRequest = NetCore::HttpRequest<
	NetCore::HttpMethod::POST,
	STR("api/upload_raw"),
	void,
	NetCore::Body<NetCore::TypeBinary>>;

using TextRequest = NetCore::HttpRequest<
	NetCore::HttpMethod::POST,
	STR("api/login"),
	void,
	NetCore::Body<NetCore::TypeString>>;

using FormRequest = NetCore::HttpRequest<
	NetCore::HttpMethod::POST,
	STR("api/form"),
	void,
	NetCore::Form<STR("user"), NetCore::TypeString>,
	NetCore::Form<STR("pw"), NetCore::TypeString>>;

using ModelRequest = NetCore::HttpRequest<
	NetCore::HttpMethod::POST,
	STR("api/note"),
	void,
	NetCore::Body<NetCore::TypeModel<NoteModel>>>;

using MultipartUpload = NetCore::HttpMultipartRequest<
	NetCore::HttpMethod::POST,
	STR("api/upload"),
	void,
	NetCore::Form<STR("file"), NetCore::TypeFile>,
	NetCore::Form<STR("name"), NetCore::TypeString>,
	NetCore::Form<STR("blob"), NetCore::TypeBinary>>;

class TestRequestConverter : public QObject
{
	Q_OBJECT

private slots:
	void urlWithPathAndQuery();
	void headerParam();
	void bearerAuth();
	void binaryBody();
	void stringBody();
	void timeoutOverride();
	void multipartHasNoUrlEncodedBody();
	void multipartBuildsFromMemory();
	void multipartThrowsOnMissingFile();
	void urlEncodedFormBody();
	void modelBody();
};

void TestRequestConverter::urlWithPathAndQuery()
{
	auto inst = GetUser::make(42, 1);
	const auto req = NetCore::RequestConverter::convertToQNetworkRequest(
		QStringLiteral("https://api.com/"), inst);

	QCOMPARE(req.url().toString(),
		QStringLiteral("https://api.com/api/users/42?page=1"));
}

void TestRequestConverter::headerParam()
{
	auto inst = HeaderRequest::make("abc123");
	const auto req = NetCore::RequestConverter::convertToQNetworkRequest(
		QStringLiteral("https://api.com/"), inst);

	QCOMPARE(QString::fromUtf8(req.rawHeader("X-Custom")), QStringLiteral("abc123"));
}

void TestRequestConverter::bearerAuth()
{
	auto inst = GetUser::make(42, 1);
	inst.runtime_headers.insert("Authorization", "mytoken");
	const auto req = NetCore::RequestConverter::convertToQNetworkRequest(
		QStringLiteral("https://api.com/"), inst);

	QCOMPARE(QString::fromUtf8(req.rawHeader("Authorization")),
		QStringLiteral("Bearer mytoken"));
}

void TestRequestConverter::binaryBody()
{
	auto inst = BinaryRequest::make(QByteArray("\x01\x02\x03", 3));
	const auto req = NetCore::RequestConverter::convertToQNetworkRequest(
		QStringLiteral("https://api.com/"), inst);

	const QVariant attr = req.attribute(
		static_cast<QNetworkRequest::Attribute>(NetCore::kRequestBodyAttrCode));
	QCOMPARE(attr.toByteArray(), QByteArray("\x01\x02\x03", 3));
}

void TestRequestConverter::stringBody()
{
	auto inst = TextRequest::make("hello");
	const auto req = NetCore::RequestConverter::convertToQNetworkRequest(
		QStringLiteral("https://api.com/"), inst);

	const QVariant attr = req.attribute(
		static_cast<QNetworkRequest::Attribute>(NetCore::kRequestBodyAttrCode));
	QCOMPARE(attr.toByteArray(), QByteArray("hello"));
}

void TestRequestConverter::timeoutOverride()
{
	auto inst = GetUser::make(42, 1);
	inst.withTimeout(5000);
	const auto req = NetCore::RequestConverter::convertToQNetworkRequest(
		QStringLiteral("https://api.com/"), inst);

	const QVariant attr = req.attribute(
		static_cast<QNetworkRequest::Attribute>(NetCore::kRequestTimeoutAttrCode));
	QCOMPARE(attr.toInt(), 5000);
}

void TestRequestConverter::multipartHasNoUrlEncodedBody()
{
	auto inst = MultipartUpload::make(
		NetCore::FileValue::fromPath(QStringLiteral("photo.jpg")),
		"doc",
		QByteArray("raw"));

	// multipart 的 form 由 buildMultipart 处理，buildBody 必须为空
	QVERIFY(NetCore::RequestConverter::buildBody(inst).isEmpty());
}

void TestRequestConverter::multipartBuildsFromMemory()
{
	auto inst = MultipartUpload::make(
		NetCore::FileValue::fromBytes(QByteArray("hi"), QStringLiteral("f.txt")),
		"doc",
		QByteArray("raw"));

	QScopedPointer<QHttpMultiPart> mp(
		NetCore::RequestConverter::buildMultipart(inst));

	QVERIFY(!mp.isNull());
}

void TestRequestConverter::multipartThrowsOnMissingFile()
{
	auto inst = MultipartUpload::make(
		NetCore::FileValue::fromPath(QStringLiteral("no_such_file_42.bin")),
		"doc",
		QByteArray("raw"));

	QVERIFY_EXCEPTION_THROWN(
		NetCore::RequestConverter::buildMultipart(inst),
		std::runtime_error);
}

void TestRequestConverter::urlEncodedFormBody()
{
	auto inst = FormRequest::make("admin", "123");

	QCOMPARE(NetCore::RequestConverter::buildBody(inst),
		QByteArray("user=admin&pw=123"));
}

void TestRequestConverter::modelBody()
{
	NoteModel note;
	note.setText("hi");

	auto inst = ModelRequest::make(note);
	const auto req = NetCore::RequestConverter::convertToQNetworkRequest(
		QStringLiteral("https://api.com/"), inst);

	const QVariant attr = req.attribute(
		static_cast<QNetworkRequest::Attribute>(NetCore::kRequestBodyAttrCode));
	QVERIFY(attr.toByteArray().contains("hi"));
}

QTEST_GUILESS_MAIN(TestRequestConverter)
#include "tst_requestconverter.moc"
