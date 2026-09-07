#include <QtTest>
#include <QNetworkReply>
#include <client/NetworkClientError.h>
#include <client/NetworkException.h>

class TestNetworkError : public QObject
{
	Q_OBJECT

private slots:
	void timeoutCategory();
	void httpErrorCategory();
	void networkDetail();
	void logicDetail();
	void serializationDetail();
	void unauthorizedCheck();
	void serverErrorCheck();
	void exceptionWhat();
	void asNetworkMismatch();
};

void TestNetworkError::timeoutCategory()
{
	const auto err = NetCore::NetworkClientError::timeout();

	QVERIFY(err.category() == NetCore::NetworkClientError::Category::Timeout);
	QVERIFY(err.isTimeout());
	QVERIFY(!err.isHttpError());
}

void TestNetworkError::httpErrorCategory()
{
	const auto err = NetCore::NetworkClientError::http(
		404, QByteArray("not found"), QStringLiteral("HTTP 404"));

	QVERIFY(err.isHttpError());
	QVERIFY(!err.isTimeout());

	const auto detail = err.asHttp();
	QVERIFY(detail.has_value());
	QCOMPARE(detail->status, 404);
	QCOMPARE(detail->rawBody, QByteArray("not found"));
}

void TestNetworkError::networkDetail()
{
	const auto err = NetCore::NetworkClientError::network(
		QNetworkReply::ConnectionRefusedError, QStringLiteral("refused"));

	QVERIFY(err.isNetworkError());
	const auto detail = err.asNetwork();
	QVERIFY(detail.has_value());
	QCOMPARE(static_cast<int>(detail->code),
		static_cast<int>(QNetworkReply::ConnectionRefusedError));
}

void TestNetworkError::logicDetail()
{
	const auto err = NetCore::NetworkClientError::logic(
		1001, QStringLiteral("余额不足"));

	QVERIFY(err.isLogicError());
	const auto detail = err.asLogic();
	QVERIFY(detail.has_value());
	QCOMPARE(detail->serverCode, 1001);
	QCOMPARE(detail->serverMessage, QStringLiteral("余额不足"));
}

void TestNetworkError::serializationDetail()
{
	const auto err = NetCore::NetworkClientError::serialization(
		QStringLiteral("bad json"), QByteArray("{"));

	QVERIFY(err.isParseError());
	const auto detail = err.asSerialization();
	QVERIFY(detail.has_value());
	QCOMPARE(detail->rawBody, QByteArray("{"));
}

void TestNetworkError::unauthorizedCheck()
{
	const auto unauthorized = NetCore::NetworkClientError::http(401, {}, {});
	const auto forbidden = NetCore::NetworkClientError::http(403, {}, {});

	QVERIFY(unauthorized.isUnauthorized());
	QVERIFY(!forbidden.isUnauthorized());
	QVERIFY(forbidden.isForbidden());
}

void TestNetworkError::serverErrorCheck()
{
	for (int status : { 500, 502, 503 }) {
		const auto err = NetCore::NetworkClientError::http(status, {}, {});
		QVERIFY(err.isServerError());
	}
}

void TestNetworkError::exceptionWhat()
{
	const auto err = NetCore::NetworkClientError::http(
		404, {}, QStringLiteral("HTTP 404"));
	const NetCore::NetworkException ex(err);

	QVERIFY(ex.what() != nullptr);
	QCOMPARE(QString::fromUtf8(ex.what()), QStringLiteral("HTTP 404"));
	QVERIFY(ex.error().isHttpError());
}

void TestNetworkError::asNetworkMismatch()
{
	const auto err = NetCore::NetworkClientError::timeout();

	QVERIFY(!err.asNetwork().has_value());
	QVERIFY(!err.asHttp().has_value());
	QVERIFY(!err.asSerialization().has_value());
	QVERIFY(!err.asLogic().has_value());
}

QTEST_GUILESS_MAIN(TestNetworkError)
#include "tst_networkerror.moc"
