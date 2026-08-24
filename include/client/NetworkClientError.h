#pragma once

#include <QByteArray>
#include <QNetworkReply>
#include <QString>
#include <optional>
#include <variant>

#include <core/global.h>

namespace NetCore {

// 五级错误分类：超时 / 网络层 / HTTP 层 / 反序列化 / 业务逻辑
class NETWORK_EXPORT NetworkClientError {
public:
	struct TimeoutDetail {};
	struct NetworkDetail { QNetworkReply::NetworkError code; };
	struct HttpDetail { int status; QByteArray rawBody; };
	struct SerializationDetail { QByteArray rawBody; };
	struct LogicDetail { int serverCode; QString serverMessage; };

	enum class Category { Timeout, Network, Http, Serialization, Logic };

	using Detail = std::variant<TimeoutDetail, NetworkDetail, HttpDetail,
		SerializationDetail, LogicDetail>;

	// 静态工厂：唯一创建入口，类型安全
	static NetworkClientError timeout();
	static NetworkClientError network(QNetworkReply::NetworkError code, const QString& message);
	static NetworkClientError http(int status, const QByteArray& rawBody, const QString& message);
	static NetworkClientError serialization(const QString& message, const QByteArray& rawBody = {});
	static NetworkClientError logic(int serverCode, const QString& serverMessage);

	const QString& message() const noexcept { return message_; }
	Category category() const;

	bool isTimeout() const { return category() == Category::Timeout; }
	bool isNetworkError() const { return category() == Category::Network; }
	bool isHttpError() const { return category() == Category::Http; }
	bool isParseError() const { return category() == Category::Serialization; }
	bool isLogicError() const { return category() == Category::Logic; }

	bool isUnauthorized() const;	// HTTP 401
	bool isForbidden() const;		// HTTP 403
	bool isServerError() const;		// HTTP 5xx

	std::optional<NetworkDetail> asNetwork() const;
	std::optional<HttpDetail> asHttp() const;
	std::optional<SerializationDetail> asSerialization() const;
	std::optional<LogicDetail> asLogic() const;

private:
	NetworkClientError(Detail detail, QString message);

	Detail detail_;
	QString message_;
};

} // namespace NetCore
