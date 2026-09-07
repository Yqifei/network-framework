#include <QtTest>
#include <client/LifeCycle.h>

namespace {

class TestTracked : public NetCore::TrackLifeCycle
{
public:
	TestTracked() = default;
};

} // namespace

class TestLifeCycle : public QObject
{
	Q_OBJECT

private slots:
	void weakRefValidWhileAlive();
	void weakRefNullAfterDestroyed();
};

void TestLifeCycle::weakRefValidWhileAlive()
{
	TestTracked obj;

	const auto weakBase = obj.AsWeakPtr();
	const auto weakDerived = obj.AsWeakPtrT<TestTracked>();

	QVERIFY(!weakBase.isNull());
	QVERIFY(!weakDerived.isNull());
	QVERIFY(weakDerived.data() == &obj);
}

void TestLifeCycle::weakRefNullAfterDestroyed()
{
	QWeakPointer<TestTracked> weak;

	{
		TestTracked obj;
		weak = obj.AsWeakPtrT<TestTracked>();
		QVERIFY(!weak.isNull());
	}

	// 对象析构后 self_ 随之析构，弱引用自动失效
	QVERIFY(weak.isNull());
	QVERIFY(weak.data() == nullptr);
}

QTEST_GUILESS_MAIN(TestLifeCycle)
#include "tst_lifecycle.moc"
