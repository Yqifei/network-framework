#include <service/NetworkService.h>

#include <QString>
#include <exception>
#include <utility>

#include <client/NetworkException.h>

namespace NetCore {

NetworkService::NetworkService(std::shared_ptr<NetworkClient> client)
	: client_(std::move(client))
{
}

void NetworkService::applyCommonHeaders(HttpSpec& spec) const
{
	const auto headers = commonHeaders();
	for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
		spec.header(it.key(), it.value());
	}
}

QtPromise::QPromise<QByteArray> NetworkService::httpSpecPromise(HttpSpec spec)
{
	applyCommonHeaders(spec);
	return client_->sendSpec(spec);
}

void NetworkService::attachFailHandler(QtPromise::QPromise<void> promise, FailCallback onFail)
{
	promise.fail([onFail](const std::exception& e) {
		if (!onFail) return;

		if (const auto* ne = dynamic_cast<const NetworkException*>(&e)) {
			onFail(ne->error());
			return;
		}
		// 非框架抛出的异常（比如序列化器的），归到反序列化失败
		onFail(NetworkClientError::serialization(QString::fromUtf8(e.what())));
	});
}

} // namespace NetCore
