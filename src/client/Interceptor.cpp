#include <client/Interceptor.h>

#include <QNetworkReply>
#include <QSet>
#include <utility>

#include <client/NetworkException.h>

namespace NetCore {

// 黑名单而非白名单：默认重试，只排除重试无意义的错误，
// 这样将来冒出来的未知错误码也能被兜住
RetryInterceptor::Policy::Policy()
{
	shouldRetry = [](const NetworkClientError& e) {
		if (e.isTimeout()) return true;

		if (const auto net = e.asNetwork()) {
			static const QSet<QNetworkReply::NetworkError> kPermanent = {
				QNetworkReply::OperationCanceledError,
				QNetworkReply::SslHandshakeFailedError,
				QNetworkReply::AuthenticationRequiredError,
				QNetworkReply::ProxyAuthenticationRequiredError,
				QNetworkReply::ContentAccessDenied,
				QNetworkReply::ContentOperationNotPermittedError,
				QNetworkReply::ContentNotFoundError,
				QNetworkReply::ContentGoneError,
				QNetworkReply::ContentConflictError,
				QNetworkReply::ProtocolUnknownError,
				QNetworkReply::ProtocolInvalidOperationError,
				QNetworkReply::OperationNotImplementedError,
			};
			return !kPermanent.contains(net->code);
		}

		if (const auto http = e.asHttp()) {
			// 429 不在内，限流本来就该等一会儿再试
			static const QSet<int> kPermanentHttp = {
				400, 401, 403, 404, 405, 406, 409, 410, 413, 414, 415, 422, 501
			};
			return !kPermanentHttp.contains(http->status);
		}

		return false;
	};
}

RetryInterceptor::RetryInterceptor(Policy policy)
	: policy_(std::move(policy))
{
}

RetryInterceptor::~RetryInterceptor() = default;

QtPromise::QPromise<QByteArray> RetryInterceptor::intercept(
	const QNetworkRequest&, NextHandler next)
{
	return attempt(std::move(next), policy_.maxRetries);
}

QtPromise::QPromise<QByteArray> RetryInterceptor::attempt(NextHandler next, int remaining)
{
	using Result = QtPromise::QPromise<QByteArray>;

	// 回调可能在自己析构之后才跑（比如 client 先走了），全程只碰弱引用
	const auto weakSelf = AsWeakPtrT<RetryInterceptor>();

	return next().fail([weakSelf, next, remaining](const NetworkException& ex) -> Result {
		if (weakSelf.isNull()) return Result::reject(ex);

		auto* self = weakSelf.data();
		if (remaining <= 0 || !self->policy_.shouldRetry
			|| !self->policy_.shouldRetry(ex.error())) {
			return Result::reject(ex);
		}

		return QtPromise::QPromise<void>::resolve()
			.delay(self->policy_.delay)
			.then([weakSelf, next, remaining, ex]() -> Result {
				if (weakSelf.isNull()) return Result::reject(ex);
				return weakSelf.data()->attempt(next, remaining - 1);
			});
	});
}

} // namespace NetCore
