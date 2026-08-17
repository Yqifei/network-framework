#include <QtTest>
#include <QByteArray>
#include <QString>
#include <type_traits>
#include <request/HttpRequest.h>

static_assert(std::is_same<STR("hello"), STR("hello")>::value,
	"same string must map to same type");
static_assert(!std::is_same<STR("hello"), STR("world")>::value,
	"different strings must map to different types");

struct MyStruct {
};

static_assert(std::is_same_v<NetCore::ValueTypeOf<NetCore::TypeInt>::type, int64_t>,
	"TypeInt must map to int64_t");
static_assert(std::is_same_v<NetCore::ValueTypeOf<NetCore::TypeString>::type, std::string_view>,
	"TypeString must map to std::string_view");
static_assert(std::is_same_v<NetCore::ValueTypeOf<NetCore::TypeFloat>::type, double>,
	"TypeFloat must map to double");
static_assert(std::is_same_v<NetCore::ValueTypeOf<NetCore::TypeBool>::type, bool>,
	"TypeBool must map to bool");
static_assert(std::is_same_v<NetCore::ValueTypeOf<NetCore::TypeQString>::type, QString>,
	"TypeQString must map to QString");
static_assert(std::is_same_v<NetCore::ValueTypeOf<NetCore::TypeJson>::type, QJsonObject>,
	"TypeJson must map to QJsonObject");
static_assert(std::is_same_v<NetCore::ValueTypeOf<NetCore::TypeFile>::type, NetCore::FileValue>,
	"TypeFile must map to FileValue");
static_assert(std::is_same_v<NetCore::ValueTypeOf<NetCore::TypeBinary>::type, QByteArray>,
	"TypeBinary must map to QByteArray");
static_assert(std::is_same_v<NetCore::ValueTypeOf<NetCore::TypeModel<MyStruct>>::type, MyStruct>,
	"TypeModel<T> must map to T");


static_assert(NetCore::NoKeyStr::view().empty(), "NoKeyStr view must be empty");
static_assert(NetCore::NoKeyStr::c_str()[0] == '\0', "NoKeyStr c_str must be empty");

// ---- 阶段 3.2：HttpRequestParam ----

using IdParam = NetCore::Path<STR("id"), NetCore::TypeInt>;

static_assert(std::is_same_v<IdParam::tag, NetCore::PathTag>,
	"Path tag must be PathTag");
static_assert(std::is_same_v<IdParam::key, STR("id")>,
	"Path key must be the compile-time string type");
static_assert(std::is_same_v<IdParam::value_type, int64_t>,
	"TypeInt must map to int64_t");
static_assert(IdParam::key_view == "id", "key_view must equal the string");
static_assert(IdParam::key_cstr[0] == 'i', "key_cstr must point at the string");

using PageParam = NetCore::Query<STR("page")>;
static_assert(std::is_same_v<PageParam::value_type, std::string_view>,
	"default value tag must be TypeString");

using RawBody = NetCore::Body<NetCore::TypeBinary>;
static_assert(std::is_same_v<RawBody::tag, NetCore::BodyTag>,
	"Body tag must be BodyTag");
static_assert(std::is_same_v<RawBody::key, NetCore::NoKeyStr>,
	"Body key must be NoKeyStr");
static_assert(std::is_same_v<RawBody::value_type, QByteArray>,
	"TypeBinary must map to QByteArray");

class TestHttpRequest : public QObject
{
	Q_OBJECT

private slots:
	void strView();
	void strSize();
	void fileValueFactories();
};

void TestHttpRequest::strView()
{
	const auto view = STR("hello")::view();
	QCOMPARE(QString::fromLatin1(view.data(), static_cast<int>(view.size())),
		QStringLiteral("hello"));
}

void TestHttpRequest::strSize()
{
	QCOMPARE(STR("hello")::view().size(), size_t(5));
}

void TestHttpRequest::fileValueFactories()
{
	const NetCore::FileValue fromDisk = NetCore::FileValue::fromPath(QStringLiteral("a.txt"));
	QCOMPARE(fromDisk.filePath, QStringLiteral("a.txt"));
	QVERIFY(fromDisk.data.isEmpty());

	const NetCore::FileValue fromMemory =
		NetCore::FileValue::fromBytes(QByteArray("hello"), QStringLiteral("b.txt"));
	QCOMPARE(fromMemory.data, QByteArray("hello"));
	QCOMPARE(fromMemory.fileName, QStringLiteral("b.txt"));
	QVERIFY(fromMemory.filePath.isEmpty());
}

QTEST_GUILESS_MAIN(TestHttpRequest)
#include "tst_httprequest.moc"