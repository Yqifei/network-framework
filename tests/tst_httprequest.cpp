#include <QtTest>
#include <QByteArray>
#include <QString>
#include <tuple>
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

// ---- 阶段 3.3：参数分类 Trait ----

static_assert(NetCore::IsPathParam<IdParam>::value, "IdParam must be a path param");
static_assert(!NetCore::IsQueryParam<IdParam>::value, "IdParam is not a query param");
static_assert(NetCore::IsHeaderParam<NetCore::Header<STR("token")>>::value,
	"header param must be detected");
static_assert(!NetCore::IsBodyParam<IdParam>::value, "IdParam is not a body param");

// ---- 阶段 3.3：编译期过滤 ----

using PathOnly = NetCore::FilterHttpRequestParams<NetCore::PathTag,
	NetCore::Path<STR("id"), NetCore::TypeInt>,
	NetCore::Query<STR("page"), NetCore::TypeInt>,
	NetCore::Header<STR("token"), NetCore::TypeString>>::type;

static_assert(std::tuple_size_v<PathOnly> == 1, "only the path param must remain");
static_assert(std::is_same_v<std::tuple_element_t<0, PathOnly>, IdParam>,
	"the filtered element must be the original param type");

using QueryOnly = NetCore::FilterHttpRequestParams<NetCore::QueryTag,
	NetCore::Path<STR("id"), NetCore::TypeInt>,
	NetCore::Query<STR("page"), NetCore::TypeInt>,
	NetCore::Header<STR("token"), NetCore::TypeString>>::type;

static_assert(std::tuple_size_v<QueryOnly> == 1, "only the query param must remain");

using BodyOnly = NetCore::FilterHttpRequestParams<NetCore::BodyTag,
	NetCore::Path<STR("id"), NetCore::TypeInt>,
	NetCore::Query<STR("page"), NetCore::TypeInt>>::type;

static_assert(std::tuple_size_v<BodyOnly> == 0, "no body param must yield an empty tuple");

// ---- 阶段 3.4：运行时值容器 ----

using TokenParam = NetCore::Header<STR("token"), NetCore::TypeString>;

static_assert(std::is_same_v<NetCore::HttpRequestParamValue<IdParam>::value_type, int64_t>,
	"param value must expose the mapped value type");
static_assert(NetCore::HttpRequestParamValue<IdParam>::key == "id",
	"param value must expose the compile-time key");

static_assert(std::is_same_v<NetCore::MakeValueList<std::tuple<IdParam>>::type,
	NetCore::HttpRequestParamValueList<IdParam>>,
	"tuple must expand into the value list");
static_assert(std::is_same_v<NetCore::MakeValueList<std::tuple<>>::type,
	NetCore::HttpRequestParamValueList<>>,
	"empty tuple must yield an empty list");

// ---- 阶段 3.6：HttpRequest 顶层组装 ----

struct UserResponse {
};

using GetUser = NetCore::HttpRequest<
	NetCore::HttpMethod::GET,
	STR("api/users/{id}"),
	UserResponse,
	NetCore::Path<STR("id"), NetCore::TypeInt>,
	NetCore::Query<STR("expand"), NetCore::TypeString>>;

static_assert(GetUser::method == NetCore::HttpMethod::GET, "method must be GET");
static_assert(GetUser::path_view == "api/users/{id}", "path must match");
static_assert(std::tuple_size_v<GetUser::path_params> == 1, "one path param");
static_assert(std::tuple_size_v<GetUser::query_params> == 1, "one query param");
static_assert(std::tuple_size_v<GetUser::form_params> == 0, "no form param");
static_assert(std::tuple_size_v<GetUser::header_params> == 0, "no header param");
static_assert(std::tuple_size_v<GetUser::body_params> == 0, "no body param");

class TestHttpRequest : public QObject
{
	Q_OBJECT

private slots:
	void strView();
	void strSize();
	void fileValueFactories();
	void paramValueList();
	void makeFillsInstance();
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

void TestHttpRequest::paramValueList()
{
	NetCore::HttpRequestParamValueList<IdParam, TokenParam> list;
	list.set<IdParam>(42);
	list.set<TokenParam>("abc");

	QCOMPARE(list.get<IdParam>(), int64_t(42));
	QVERIFY(list.get<TokenParam>() == "abc");
}

void TestHttpRequest::makeFillsInstance()
{
	auto req = GetUser::make(42, "profile");

	QCOMPARE(req.path.get<IdParam>(), int64_t(42));
	QVERIFY((req.query.get<NetCore::Query<STR("expand"), NetCore::TypeString>>() == "profile"));
	QCOMPARE(req.timeout_override_ms, -1);

	req.withTimeout(5000);
	QCOMPARE(req.timeout_override_ms, 5000);

	req.runtime_headers.insert("X-Request-Id", "abc123");
	QCOMPARE(req.runtime_headers.value("X-Request-Id"), QByteArray("abc123"));
}

QTEST_GUILESS_MAIN(TestHttpRequest)
#include "tst_httprequest.moc"