#include <QtTest>
#include <QJsonDocument>
#include <serializer/JsonSerializer.h>

// ---------- 测试类型 ----------

class GeoPoint
{
    Q_GADGET
    Q_PROPERTY(double lat READ lat WRITE setLat)
    Q_PROPERTY(double lng READ lng WRITE setLng)

public:
    double lat() const { return m_lat; }
    void setLat(double lat) { m_lat = lat; }
    double lng() const { return m_lng; }
    void setLng(double lng) { m_lng = lng; }

private:
    double m_lat = 0.0;
    double m_lng = 0.0;
};
Q_DECLARE_METATYPE(GeoPoint)

class Address
{
    Q_GADGET
    Q_PROPERTY(QString city READ city WRITE setCity)
    Q_PROPERTY(QString street READ street WRITE setStreet)
    Q_PROPERTY(QString zipCode READ zipCode WRITE setZipCode)
    Q_PROPERTY(GeoPoint location READ location WRITE setLocation)

public:
    QString city() const { return m_city; }
    void setCity(const QString &city) { m_city = city; }
    QString street() const { return m_street; }
    void setStreet(const QString &street) { m_street = street; }
    QString zipCode() const { return m_zipCode; }
    void setZipCode(const QString &zipCode) { m_zipCode = zipCode; }
    GeoPoint location() const { return m_location; }
    void setLocation(const GeoPoint &location) { m_location = location; }

private:
    QString m_city;
    QString m_street;
    QString m_zipCode;
    GeoPoint m_location;
};
Q_DECLARE_METATYPE(Address)
Q_DECLARE_METATYPE(QList<Address>)

class Person : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName)
    Q_PROPERTY(int age READ age WRITE setAge)
    Q_PROPERTY(bool active READ active WRITE setActive)
    Q_PROPERTY(Status status READ status WRITE setStatus)
    Q_PROPERTY(Address address READ address WRITE setAddress)
    Q_PROPERTY(QList<Address> addresses READ addresses WRITE setAddresses)
    Q_PROPERTY(QVector<int> scores READ scores WRITE setScores)
    Q_PROPERTY(QStringList tags READ tags WRITE setTags)

public:
    enum Status { Offline, Online };
    Q_ENUM(Status)

    explicit Person(QObject *parent = nullptr) : QObject(parent) {}

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }
    int age() const { return m_age; }
    void setAge(int age) { m_age = age; }
    bool active() const { return m_active; }
    void setActive(bool active) { m_active = active; }
    Status status() const { return m_status; }
    void setStatus(Status status) { m_status = status; }
    Address address() const { return m_address; }
    void setAddress(const Address &address) { m_address = address; }
    QList<Address> addresses() const { return m_addresses; }
    void setAddresses(const QList<Address> &addresses) { m_addresses = addresses; }
    QVector<int> scores() const { return m_scores; }
    void setScores(const QVector<int> &scores) { m_scores = scores; }
    QStringList tags() const { return m_tags; }
    void setTags(const QStringList &tags) { m_tags = tags; }

private:
    QString m_name;
    int m_age = 0;
    bool m_active = false;
    Status m_status = Offline;
    Address m_address;
    QList<Address> m_addresses;
    QVector<int> m_scores;
    QStringList m_tags;
};

// Person 的 Q_GADGET 镜像，用于按值反序列化测试
class PersonDto
{
    Q_GADGET
    Q_PROPERTY(QString name READ name WRITE setName)
    Q_PROPERTY(int age READ age WRITE setAge)
    Q_PROPERTY(bool active READ active WRITE setActive)
    Q_PROPERTY(Status status READ status WRITE setStatus)
    Q_PROPERTY(Address address READ address WRITE setAddress)
    Q_PROPERTY(QList<Address> addresses READ addresses WRITE setAddresses)
    Q_PROPERTY(QVector<int> scores READ scores WRITE setScores)
    Q_PROPERTY(QStringList tags READ tags WRITE setTags)

public:
    enum Status { Offline, Online };
    Q_ENUM(Status)

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }
    int age() const { return m_age; }
    void setAge(int age) { m_age = age; }
    bool active() const { return m_active; }
    void setActive(bool active) { m_active = active; }
    Status status() const { return m_status; }
    void setStatus(Status status) { m_status = status; }
    Address address() const { return m_address; }
    void setAddress(const Address &address) { m_address = address; }
    QList<Address> addresses() const { return m_addresses; }
    void setAddresses(const QList<Address> &addresses) { m_addresses = addresses; }
    QVector<int> scores() const { return m_scores; }
    void setScores(const QVector<int> &scores) { m_scores = scores; }
    QStringList tags() const { return m_tags; }
    void setTags(const QStringList &tags) { m_tags = tags; }

private:
    QString m_name;
    int m_age = 0;
    bool m_active = false;
    Status m_status = Offline;
    Address m_address;
    QList<Address> m_addresses;
    QVector<int> m_scores;
    QStringList m_tags;
};
Q_DECLARE_METATYPE(PersonDto)

// ---------- 测试 ----------

class TestJsonSerializer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void roundTrip();
    void qobjectRoundTrip();
    void nestedGadget();
    void enumAsString();
    void enumAsInt();
    void unknownFieldStrictThrows();
    void unknownFieldDefaultIgnored();
    void invalidJsonThrows();
    void whatNotDangling();

private:
    template <typename T>
    void fill(T *target) const;
};

void TestJsonSerializer::initTestCase()
{
    qRegisterMetaType<Address>();
    qRegisterMetaType<GeoPoint>();

    // QVariantList → 容器类型转换器：自定义元素类型必须显式提供转换函数
    QMetaType::registerConverter<QVariantList, QList<Address>>(
        [](const QVariantList &list) {
            QList<Address> result;
            result.reserve(list.size());
            for (const QVariant &item : list)
                result.append(item.value<Address>());
            return result;
        });
    QMetaType::registerConverter<QVariantList, QVector<int>>(
        [](const QVariantList &list) {
            QVector<int> result;
            result.reserve(list.size());
            for (const QVariant &item : list)
                result.append(item.toInt());
            return result;
        });
}

template <typename T>
void TestJsonSerializer::fill(T *target) const
{
    target->setName("Alice");
    target->setAge(30);
    target->setActive(true);
    target->setStatus(T::Online);

    GeoPoint point;
    point.setLat(31.23);
    point.setLng(121.47);

    Address address;
    address.setCity("Shanghai");
    address.setStreet("Nanjing Road 100");
    address.setZipCode("200001");
    address.setLocation(point);
    target->setAddress(address);

    Address address2;
    address2.setCity("Beijing");
    address2.setStreet("Chang'an Avenue");
    address2.setZipCode("100000");
    address2.setLocation(point);
    target->setAddresses({ address, address2 });

    target->setScores({ 90, 85, 78 });
    target->setTags({ "cpp", "qt" });
}

void TestJsonSerializer::roundTrip()
{
    NetCore::JsonSerializer serializer;

    Person person;
    fill(&person);
    const QByteArray bytes = serializer.serializeToBytes(person);

    PersonDto restored = serializer.deserializeFromBytes<PersonDto>(bytes);

    QCOMPARE(restored.name(), QStringLiteral("Alice"));
    QCOMPARE(restored.age(), 30);
    QCOMPARE(restored.active(), true);
    QCOMPARE(restored.status(), PersonDto::Online);
    QCOMPARE(restored.address().city(), QStringLiteral("Shanghai"));
    QCOMPARE(restored.address().zipCode(), QStringLiteral("200001"));
    QCOMPARE(restored.addresses().size(), 2);
    QCOMPARE(restored.addresses().at(1).city(), QStringLiteral("Beijing"));
    QCOMPARE(restored.scores().size(), 3);
    QCOMPARE(restored.scores().at(2), 78);
    QCOMPARE(restored.tags().size(), 2);
    QCOMPARE(restored.tags().at(0), QStringLiteral("cpp"));
}

void TestJsonSerializer::qobjectRoundTrip()
{
    NetCore::JsonSerializer serializer;

    Person person;
    fill(&person);
    const QByteArray bytes = serializer.serializeToBytes(person);

    Person *restored = serializer.deserializeFromBytes<Person>(bytes);
    QVERIFY(restored != nullptr);
    QCOMPARE(restored->name(), QStringLiteral("Alice"));
    QCOMPARE(restored->age(), 30);
    QCOMPARE(restored->status(), Person::Online);
    delete restored;
}

void TestJsonSerializer::nestedGadget()
{
    NetCore::JsonSerializer serializer;

    Person person;
    fill(&person);
    const QByteArray bytes = serializer.serializeToBytes(person);

    PersonDto restored = serializer.deserializeFromBytes<PersonDto>(bytes);

    // QObject → Q_GADGET(Address) → Q_GADGET(GeoPoint) 三层嵌套 + 列表里的嵌套对象
    QCOMPARE(restored.address().location().lat(), 31.23);
    QCOMPARE(restored.address().location().lng(), 121.47);
    QCOMPARE(restored.addresses().at(1).location().lat(), 31.23);
}

void TestJsonSerializer::enumAsString()
{
    NetCore::JsonSerializer serializer;
    serializer.setFlags(NetCore::EnumAsString | NetCore::IgnoreUnknownKeys);

    Person person;
    fill(&person);
    const QByteArray bytes = serializer.serializeToBytes(person);
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    QCOMPARE(document.object().value("status").toString(), QStringLiteral("Online"));

    PersonDto restored = serializer.deserializeFromBytes<PersonDto>(bytes);
    QCOMPARE(restored.status(), PersonDto::Online);
}

void TestJsonSerializer::enumAsInt()
{
    NetCore::JsonSerializer serializer;   // 默认 flags：枚举存整数

    Person person;
    fill(&person);
    const QByteArray bytes = serializer.serializeToBytes(person);
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    QCOMPARE(document.object().value("status").toInt(), 1);
}

void TestJsonSerializer::unknownFieldStrictThrows()
{
    NetCore::JsonSerializer serializer;
    serializer.setFlags(NetCore::None);   // 关闭 IgnoreUnknownKeys = 严格模式

    const QByteArray bytes = R"({"name":"Bob","age":20,"active":false,"status":0,"address":{"city":"X","street":"","zipCode":"","location":{"lat":0,"lng":0}},"addresses":[],"scores":[],"tags":[],"unknownKey":1})";
    QVERIFY_EXCEPTION_THROWN(serializer.deserializeFromBytes<PersonDto>(bytes),
                             NetCore::DeserializerException);
}

void TestJsonSerializer::unknownFieldDefaultIgnored()
{
    NetCore::JsonSerializer serializer;   // 默认 IgnoreUnknownKeys 开启

    const QByteArray bytes = R"({"name":"Bob","age":20,"active":false,"status":0,"address":{"city":"X","street":"","zipCode":"","location":{"lat":0,"lng":0}},"addresses":[],"scores":[],"tags":[],"unknownKey":1})";
    PersonDto restored = serializer.deserializeFromBytes<PersonDto>(bytes);
    QCOMPARE(restored.name(), QStringLiteral("Bob"));
    QCOMPARE(restored.age(), 20);
}

void TestJsonSerializer::invalidJsonThrows()
{
    NetCore::JsonSerializer serializer;
    QVERIFY_EXCEPTION_THROWN(serializer.deserializeFromBytes<PersonDto>("{not valid json"),
                             NetCore::DeserializerException);
}

void TestJsonSerializer::whatNotDangling()
{
    const QByteArray message = "boom";

    try {
        throw NetCore::SerializerException(message);
    } catch (const NetCore::SerializerException &error) {
        const char *first = error.what();
        const char *second = error.what();
        QVERIFY(first == second);          // 两次调用返回同一块存储 = 不是临时对象
        QCOMPARE(QByteArray(first), message);
    }

    try {
        throw NetCore::DeserializerException(message);
    } catch (const NetCore::DeserializerException &error) {
        QCOMPARE(QByteArray(error.what()), message);
    }
}

QTEST_GUILESS_MAIN(TestJsonSerializer)
#include "tst_jsonserializer.moc"
