#include <client/NetworkClientError.h>

#include <type_traits>
#include <utility>

namespace NetCore {

NetworkClientError::NetworkClientError(Detail detail, QString message)
	: detail_(std::move(detail))
	, message_(std::move(message))
{
}

NetworkClientError NetworkClientError::timeout()
{
	return NetworkClientError(Detail(TimeoutDetail{}),
		QStringLiteral("Request timed out"));
}

NetworkClientError NetworkClientError::network(
	QNetworkReply::NetworkError code, const QString& message)
{
	return NetworkClientError(Detail(NetworkDetail{ code }), message);
}

NetworkClientError NetworkClientError::http(
	int status, const QByteArray& rawBody, const QString& message)
{
	return NetworkClientError(Detail(HttpDetail{ status, rawBody }), message);
}

NetworkClientError NetworkClientError::serialization(
	const QString& message, const QByteArray& rawBody)
{
	return NetworkClientError(Detail(SerializationDetail{ rawBody }), message);
}

NetworkClientError NetworkClientError::logic(
	int serverCode, const QString& serverMessage)
{
	return NetworkClientError(Detail(LogicDetail{ serverCode, serverMessage }),
		serverMessage);
}

NetworkClientError::Category NetworkClientError::category() const
{
	return std::visit([](auto&& d) -> Category {
		using T = std::decay_t<decltype(d)>;
		if constexpr (std::is_same_v<T, TimeoutDetail>) return Category::Timeout;
		else if constexpr (std::is_same_v<T, NetworkDetail>) return Category::Network;
		else if constexpr (std::is_same_v<T, HttpDetail>) return Category::Http;
		else if constexpr (std::is_same_v<T, SerializationDetail>) return Category::Serialization;
		else return Category::Logic;
	}, detail_);
}

bool NetworkClientError::isUnauthorized() const
{
	if (const auto* d = std::get_if<HttpDetail>(&detail_)) return d->status == 401;
	return false;
}

bool NetworkClientError::isForbidden() const
{
	if (const auto* d = std::get_if<HttpDetail>(&detail_)) return d->status == 403;
	return false;
}

bool NetworkClientError::isServerError() const
{
	if (const auto* d = std::get_if<HttpDetail>(&detail_))
		return d->status >= 500 && d->status < 600;
	return false;
}

std::optional<NetworkClientError::NetworkDetail> NetworkClientError::asNetwork() const
{
	if (const auto* d = std::get_if<NetworkDetail>(&detail_)) return *d;
	return std::nullopt;
}

std::optional<NetworkClientError::HttpDetail> NetworkClientError::asHttp() const
{
	if (const auto* d = std::get_if<HttpDetail>(&detail_)) return *d;
	return std::nullopt;
}

std::optional<NetworkClientError::SerializationDetail> NetworkClientError::asSerialization() const
{
	if (const auto* d = std::get_if<SerializationDetail>(&detail_)) return *d;
	return std::nullopt;
}

std::optional<NetworkClientError::LogicDetail> NetworkClientError::asLogic() const
{
	if (const auto* d = std::get_if<LogicDetail>(&detail_)) return *d;
	return std::nullopt;
}

} // namespace NetCore
