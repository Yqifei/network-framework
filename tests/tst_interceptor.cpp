#include <QNetworkRequest>
#include <QtTest>
#include <chrono>

#include <client/Interceptor.h>
#include <client/NetworkException.h>

using namespace NetCore;
using QtPromise::QPromise;

namespace {

// 假的下游，前 failTimes 次调用返回失败
struct Downstream {
	int calls = 0;
	int failTimes = 0;
	NetworkClientError error = NetworkClientError::timeout();
	QByteArray payload = "ok";
};

NextHandler makeNext(QSharedPointer<Downstream> down)
{
	return [down]() -> QPromise<QByteArray> {
		++down->calls;
		if (down->calls <= down->failTimes)
			return QPromise<QByteArray>::reject(NetworkException(down->error));
		return QPromise<QByteArray>::resolve(down->payload);
	};
}

RetryInterceptor::Policy fastPolicy(int maxRetries)
{
	RetryInterceptor::Policy policy;
	policy.maxRetries = maxRetries;
	policy.delay = std::chrono::milliseconds(10);
	return policy;
}

} // namespace

class TestInterceptor : public QObject
{
	Q_OBJECT

private slots:
	void defaultPolicyClassifiesErrors();
	void passesThroughOnSuccess();
	void retriesUntilSuccess();
	void stopsWhenRetriesExhausted();
	void doesNotRetryPermanentError();
	void abandonsRetryWhenDestroyed();
};

void TestInterceptor::defaultPolicyClassifiesErrors()
{
	const RetryInterceptor::Policy policy;

	QVERIFY(policy.shouldRetry(NetworkClientError::timeout()));
	QVERIFY(policy.shouldRetry(NetworkClientError::http(500, {}, "boom")));
	QVERIFY(policy.shouldRetry(NetworkClientError::http(502, {}, "bad gateway")));
	QVERIFY(policy.shouldRetry(NetworkClientError::http(503, {}, "unavailable")));
	QVERIFY(policy.shouldRetry(NetworkClientError::http(429, {}, "slow down")));

	QVERIFY(!policy.shouldRetry(NetworkClientError::http(400, {}, "bad request")));
	QVERIFY(!policy.shouldRetry(NetworkClientError::http(401, {}, "unauthorized")));
	QVERIFY(!policy.shouldRetry(NetworkClientError::http(404, {}, "not found")));

	QVERIFY(policy.shouldRetry(
		NetworkClientError::network(QNetworkReply::TemporaryNetworkFailureError, "flaky")));
	QVERIFY(!policy.shouldRetry(
		NetworkClientError::network(QNetworkReply::ContentNotFoundError, "gone")));

	QVERIFY(policy.shouldRetry(NetworkClientError::http(599, {}, "who knows")));	// 未知码默认重试

	QVERIFY(!policy.shouldRetry(NetworkClientError::serialization("bad json")));
	QVERIFY(!policy.shouldRetry(NetworkClientError::logic(10001, "余额不足")));
}

void TestInterceptor::passesThroughOnSuccess()
{
	auto down = QSharedPointer<Downstream>::create();
	RetryInterceptor interceptor(fastPolicy(3));

	QByteArray got;
	bool settled = false;
	interceptor.intercept(QNetworkRequest(), makeNext(down))
		.then([&](const QByteArray& body) { got = body; settled = true; })
		.fail([&](const NetworkException&) { settled = true; });

	QTRY_VERIFY(settled);
	QCOMPARE(down->calls, 1);
	QCOMPARE(got, QByteArray("ok"));
}

void TestInterceptor::retriesUntilSuccess()
{
	auto down = QSharedPointer<Downstream>::create();
	down->failTimes = 2;
	RetryInterceptor interceptor(fastPolicy(3));

	QByteArray got;
	bool failed = false;
	bool settled = false;
	interceptor.intercept(QNetworkRequest(), makeNext(down))
		.then([&](const QByteArray& body) { got = body; settled = true; })
		.fail([&](const NetworkException&) { failed = settled = true; });

	QTRY_VERIFY(settled);
	QVERIFY(!failed);
	QCOMPARE(got, QByteArray("ok"));
	QCOMPARE(down->calls, 3);	// 首次 + 两次重试
}

void TestInterceptor::stopsWhenRetriesExhausted()
{
	auto down = QSharedPointer<Downstream>::create();
	down->failTimes = 99;
	RetryInterceptor interceptor(fastPolicy(2));

	bool failed = false;
	bool settled = false;
	interceptor.intercept(QNetworkRequest(), makeNext(down))
		.then([&](const QByteArray&) { settled = true; })
		.fail([&](const NetworkException& ex) {
			failed = settled = true;
			QVERIFY(ex.error().isTimeout());
		});

	QTRY_VERIFY(settled);
	QVERIFY(failed);
	QCOMPARE(down->calls, 3);	// 首次 + 两次重试后放弃
}

void TestInterceptor::doesNotRetryPermanentError()
{
	auto down = QSharedPointer<Downstream>::create();
	down->failTimes = 99;
	down->error = NetworkClientError::http(404, {}, "not found");
	RetryInterceptor interceptor(fastPolicy(3));

	bool failed = false;
	bool settled = false;
	interceptor.intercept(QNetworkRequest(), makeNext(down))
		.then([&](const QByteArray&) { settled = true; })
		.fail([&](const NetworkException&) { failed = settled = true; });

	QTRY_VERIFY(settled);
	QVERIFY(failed);
	QCOMPARE(down->calls, 1);
}

// 延迟等待期间销毁拦截器，弱引用兜住悬垂
void TestInterceptor::abandonsRetryWhenDestroyed()
{
	auto down = QSharedPointer<Downstream>::create();
	down->failTimes = 99;

	auto policy = fastPolicy(5);
	policy.delay = std::chrono::milliseconds(500);
	auto* interceptor = new RetryInterceptor(policy);

	bool failed = false;
	bool settled = false;
	interceptor->intercept(QNetworkRequest(), makeNext(down))
		.then([&](const QByteArray&) { settled = true; })
		.fail([&](const NetworkException& ex) {
			failed = settled = true;
			QVERIFY(ex.error().isTimeout());
		});

	// 让首次失败先派发掉，重试定时器起来后停在延迟窗口里
	QTest::qWait(100);
	QCOMPARE(down->calls, 1);
	delete interceptor;

	QTRY_VERIFY_WITH_TIMEOUT(settled, 3000);
	QVERIFY(failed);
	QCOMPARE(down->calls, 1);	// 重试没有再发出去
}

QTEST_GUILESS_MAIN(TestInterceptor)
#include "tst_interceptor.moc"
