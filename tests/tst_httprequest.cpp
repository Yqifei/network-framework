#include <QtTest>
#include <type_traits>
#include <request/HttpRequest.h>

static_assert(std::is_same<STR("hello"), STR("hello")>::value,
    "same string must map to same type");
static_assert(!std::is_same<STR("hello"), STR("world")>::value,
    "different strings must map to different types");

class TestHttpRequest : public QObject
{
    Q_OBJECT

private slots:
    void strView();
    void strSize();
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

QTEST_GUILESS_MAIN(TestHttpRequest)
#include "tst_httprequest.moc"