#include <QtPromise>
#include <QtTest>
#include <core/Version.h>

class TestSmoke : public QObject
{
    Q_OBJECT

private slots:
    void version();
    void qtpromiseWorks();
};

void TestSmoke::version()
{
    QCOMPARE(QString::fromLatin1(NetCore::versionString()), QStringLiteral("0.1.0"));
    QCOMPARE(NetCore::versionMajor(), 0);
}

void TestSmoke::qtpromiseWorks()
{
    int result = -1;
    QtPromise::QPromise<int>::resolve(42)
        .then([&result](int value) { result = value; })
        .wait();
    QCOMPARE(result, 42);
}

QTEST_GUILESS_MAIN(TestSmoke)
#include "tst_smoke.moc"
