#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <serializer/JsonSerializer.h>

// ---------- 测试类型 ----------

// 嵌套 QObject：Q_INVOKABLE 构造函数接收 parent，用于验证创建时入树
class Note : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText)

public:
    Q_INVOKABLE explicit Note(QObject *parent = nullptr) : QObject(parent) {}

    QString text() const { return m_text; }
    void setText(const QString &text) { m_text = text; }

private:
    QString m_text;
};

class Geo
{
    Q_GADGET
    Q_PROPERTY(double lat READ lat WRITE setLat)

public:
    double lat() const { return m_lat; }
    void setLat(double lat) { m_lat = lat; }

private:
    double m_lat = 0.0;
};
Q_DECLARE_METATYPE(Geo)

class Addr
{
    Q_GADGET
    Q_PROPERTY(QString city READ city WRITE setCity)
    Q_PROPERTY(Geo location READ location WRITE setLocation)

public:
    QString city() const { return m_city; }
    void setCity(const QString &city) { m_city = city; }
    Geo location() const { return m_location; }
    void setLocation(const Geo &location) { m_location = location; }

private:
    QString m_city;
    Geo m_location;
};
Q_DECLARE_METATYPE(Addr)
Q_DECLARE_METATYPE(QList<Addr>)

// 顶层 Q_GADGET：值类型反序列化，覆盖 null 容错
class Profile
{
    Q_GADGET
    Q_PROPERTY(int age READ age WRITE setAge)
    Q_PROPERTY(Addr address READ address WRITE setAddress)
    Q_PROPERTY(QList<Addr> addresses READ addresses WRITE setAddresses)

public:
    int age() const { return m_age; }
    void setAge(int age) { m_age = age; }
    Addr address() const { return m_address; }
    void setAddress(const Addr &address) { m_address = address; }
    QList<Addr> addresses() const { return m_addresses; }
    void setAddresses(const QList<Addr> &addresses) { m_addresses = addresses; }

private:
    int m_age = 0;
    Addr m_address;
    QList<Addr> m_addresses;
};
Q_DECLARE_METATYPE(Profile)

// 顶层 QObject：嵌套指针属性验证父子树
class Holder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Note *mentor READ mentor WRITE setMentor)
    Q_PROPERTY(QList<Note*> notes READ notes WRITE setNotes)

public:
    using QObject::QObject;

    Note *mentor() const { return m_mentor; }
    void setMentor(Note *mentor) { m_mentor = mentor; }
    QList<Note*> notes() const { return m_notes; }
    void setNotes(const QList<Note*> &notes) { m_notes = notes; }

private:
    Note *m_mentor = nullptr;
    QList<Note*> m_notes;
};
Q_DECLARE_METATYPE(QList<Note*>)

// 基础类型扩展：bool 容错 + short/float 往返
class Metrics
{
    Q_GADGET
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled)
    Q_PROPERTY(short rank READ rank WRITE setRank)
    Q_PROPERTY(float score READ score WRITE setScore)
    Q_PROPERTY(unsigned int views READ views WRITE setViews)

public:
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }
    short rank() const { return m_rank; }
    void setRank(short rank) { m_rank = rank; }
    float score() const { return m_score; }
    void setScore(float score) { m_score = score; }
    unsigned int views() const { return m_views; }
    void setViews(unsigned int views) { m_views = views; }

private:
    bool m_enabled = false;
    short m_rank = 0;
    float m_score = 0.0f;
    unsigned int m_views = 0;
};
Q_DECLARE_METATYPE(Metrics)

// ---------- 测试 ----------

class TestJsonSerializerRobustness : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void nestedQObjectJoinsParentTree();
    void nestedQObjectInListJoinsParentTree();
    void nestedFailureReportsFieldPath();
    void nullFieldsFallBackToDefaults();
    void nullPointerInListAllowed();
    void undefinedFallsBackToDefaults();
    void boolToleratesAlternativeEncodings();
    void shortFloatRoundTrip();
};

void TestJsonSerializerRobustness::initTestCase()
{
    qRegisterMetaType<Addr>();
    qRegisterMetaType<Geo>();
    qRegisterMetaType<QList<Note*>>();

    QMetaType::registerConverter<QVariantList, QList<Addr>>(
        [](const QVariantList &list) {
            QList<Addr> result;
            result.reserve(list.size());
            for (const QVariant &item : list)
                result.append(item.value<Addr>());
            return result;
        });
    QMetaType::registerConverter<QVariantList, QList<Note*>>(
        [](const QVariantList &list) {
            QList<Note*> result;
            result.reserve(list.size());
            for (const QVariant &item : list)
                result.append(item.value<Note*>());
            return result;
        });
}

// 嵌套 QObject 创建时即入树：parent 是外层对象，外层销毁自动级联释放
void TestJsonSerializerRobustness::nestedQObjectJoinsParentTree()
{
    NetCore::JsonSerializer serializer;
    const QByteArray bytes = R"({"mentor":{"text":"m"}})";

    Holder *holder = serializer.deserializeFromBytes<Holder>(bytes);
    QVERIFY(holder != nullptr);
    QVERIFY(holder->mentor() != nullptr);
    QCOMPARE(holder->mentor()->text(), QStringLiteral("m"));
    QCOMPARE(holder->mentor()->parent(), static_cast<QObject*>(holder));
    QCOMPARE(holder->children().size(), 1);
    delete holder;
}

// 列表里的嵌套 QObject 元素同样入树（contextParent 沿列表递归传递）
void TestJsonSerializerRobustness::nestedQObjectInListJoinsParentTree()
{
    NetCore::JsonSerializer serializer;
    const QByteArray bytes = R"({"notes":[{"text":"n1"},{"text":"n2"}]})";

    Holder *holder = serializer.deserializeFromBytes<Holder>(bytes);
    QVERIFY(holder != nullptr);
    QCOMPARE(holder->notes().size(), 2);
    QCOMPARE(holder->notes().at(0)->text(), QStringLiteral("n1"));
    QCOMPARE(holder->notes().at(0)->parent(), static_cast<QObject*>(holder));
    QCOMPARE(holder->notes().at(1)->parent(), static_cast<QObject*>(holder));
    QCOMPARE(holder->children().size(), 2);
    delete holder;
}

// 深层嵌套失败：异常消息逐层带上字段名，外层字段在前
void TestJsonSerializerRobustness::nestedFailureReportsFieldPath()
{
    NetCore::JsonSerializer serializer;
    // address 是对象但 location 不是：最底层报类型错误，逐层包装字段路径
    const QByteArray bytes =
        R"({"age":20,"address":{"city":"X","location":42},"addresses":[]})";

    try {
        serializer.deserializeFromBytes<Profile>(bytes);
        QFAIL("嵌套类型错误应当抛出");
    } catch (const NetCore::DeserializerException &ex) {
        const QByteArray message(ex.what());
        QVERIFY2(message.contains("address"), message.constData());
        QVERIFY2(message.contains("location"), message.constData());
        QVERIFY2(message.contains("Expected JSON object"), message.constData());
        QVERIFY(message.indexOf("address") < message.indexOf("location"));
    }
}

// null 与缺字段同义：不抛异常，全部回落默认值
void TestJsonSerializerRobustness::nullFieldsFallBackToDefaults()
{
    NetCore::JsonSerializer serializer;
    const QByteArray bytes =
        R"({"age":null,"address":null,"addresses":[{"city":"X","location":null},null]})";

    Profile profile = serializer.deserializeFromBytes<Profile>(bytes);
    QCOMPARE(profile.age(), 0);
    QVERIFY(profile.address().city().isEmpty());
    QCOMPARE(profile.addresses().size(), 2);
    QCOMPARE(profile.addresses().at(0).city(), QStringLiteral("X"));
    QVERIFY(profile.addresses().at(1).city().isEmpty());
    QCOMPARE(profile.addresses().at(0).location().lat(), 0.0);
}

// AllowNullForClasses：指针列表里的 null 元素解析成空指针，非空元素正常入树
void TestJsonSerializerRobustness::nullPointerInListAllowed()
{
    NetCore::JsonSerializer serializer;
    serializer.setFlags(NetCore::AllowNullForClasses | NetCore::IgnoreUnknownKeys);
    const QByteArray bytes = R"({"mentor":null,"notes":[{"text":"n1"},null]})";

    Holder *holder = serializer.deserializeFromBytes<Holder>(bytes);
    QVERIFY(holder != nullptr);
    QVERIFY(holder->mentor() == nullptr);
    QCOMPARE(holder->notes().size(), 2);
    QCOMPARE(holder->notes().at(0)->text(), QStringLiteral("n1"));
    QVERIFY(holder->notes().at(1) == nullptr);
    QCOMPARE(holder->notes().at(0)->parent(), static_cast<QObject*>(holder));
    delete holder;
}

// QJsonDocument 解析不出 undefined，手工构造对象验证该分支同样回落默认值
void TestJsonSerializerRobustness::undefinedFallsBackToDefaults()
{
    NetCore::JsonSerializer serializer;

    QJsonObject object;
    object.insert(QStringLiteral("age"), QJsonValue(QJsonValue::Undefined));
    object.insert(QStringLiteral("address"), QJsonValue(QJsonValue::Undefined));
    object.insert(QStringLiteral("addresses"), QJsonArray());

    Profile profile = serializer.deserialize<Profile>(object);
    QCOMPARE(profile.age(), 0);
    QVERIFY(profile.address().city().isEmpty());
}

// bool 容错：服务端用 0/1 或字符串表示布尔值时也能正确解析
void TestJsonSerializerRobustness::boolToleratesAlternativeEncodings()
{
    NetCore::JsonSerializer serializer;

    struct Case {
        const char *json;
        bool expected;
    };
    const Case cases[] = {
        { R"({"enabled":true})", true },
        { R"({"enabled":1})", true },
        { R"({"enabled":"true"})", true },
        { R"({"enabled":"1"})", true },
        { R"({"enabled":"TRUE"})", true },
        { R"({"enabled":false})", false },
        { R"({"enabled":0})", false },
        { R"({"enabled":"false"})", false },
    };

    for (const Case &c : cases) {
        Metrics metrics =
            serializer.deserializeFromBytes<Metrics>(QByteArray(c.json));
        QCOMPARE(metrics.enabled(), c.expected);
    }
}

// short/float/unsigned 属性：序列化往返保持一致
void TestJsonSerializerRobustness::shortFloatRoundTrip()
{
    NetCore::JsonSerializer serializer;

    Metrics metrics;
    metrics.setEnabled(true);
    metrics.setRank(7);
    metrics.setScore(3.5f);
    metrics.setViews(42);

    const QByteArray bytes = serializer.serializeToBytes(metrics);
    QVERIFY(bytes.contains("3.5"));   // float 序列化成 JSON 数字，保留小数部分

    Metrics restored = serializer.deserializeFromBytes<Metrics>(bytes);
    QCOMPARE(restored.enabled(), true);
    QCOMPARE(restored.rank(), short(7));
    QCOMPARE(restored.score(), 3.5f);
    QCOMPARE(restored.views(), 42u);
}

QTEST_GUILESS_MAIN(TestJsonSerializerRobustness)
#include "tst_jsonserializer_robustness.moc"
