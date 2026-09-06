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
			// 黑名单: 明确不可能通过重试恢复的永久性错误
			static const QSet<QNetworkReply::NetworkError> kPermanent = {
				QNetworkReply::OperationCanceledError,          // 代码/用户主动取消，不应悄悄重试
				QNetworkReply::SslHandshakeFailedError,         // TLS/证书配置问题，重试必然失败
				QNetworkReply::BackgroundRequestNotAllowedError, // 系统省电/流量策略拒绝，重试被同样拒绝
				QNetworkReply::TooManyRedirectsError,            // 重定向循环，重试只会继续循环
				QNetworkReply::InsecureRedirectError,            // 安全策略阻止 https->http 降级
				QNetworkReply::ProxyAuthenticationRequiredError, // 代理需要凭证，需人工介入
				QNetworkReply::AuthenticationRequiredError,      // 401, 需要重新鉴权，重试继续 401
				QNetworkReply::ContentAccessDenied,              // 403, 权限永久拒绝
				QNetworkReply::ContentOperationNotPermittedError,// 操作不被允许(永久性)
				QNetworkReply::ContentNotFoundError,             // 404, 资源不存在
				QNetworkReply::ContentGoneError,                  // 410, 资源已永久删除
				QNetworkReply::ContentConflictError,              // 409, 业务冲突，重试同样冲突
				QNetworkReply::ProtocolUnknownError,              // 协议级别不兼容
				QNetworkReply::ProtocolInvalidOperationError,    // 协议操作非法
				QNetworkReply::ProtocolFailure,                  // 协议层根本性错误
				QNetworkReply::OperationNotImplementedError,      // 501, 服务端不支持此操作
			};
			// 其余一律重试: 包括 HostNotFound/NetworkSessionFailed/Unknown 等瞬时性错误
			return !kPermanent.contains(net->code);
		}

		if (const auto http = e.asHttp()) {
			// 黑名单: 4xx 客户端错误 (除 429 限流) 永久性，重试必然失败
			// 429 不在内，限流本来就该等一会儿再试
			static const QSet<int> kPermanentHttp = {
				400, // Bad Request - 请求格式错误
				401, // Unauthorized - 需要鉴权
				403, // Forbidden - 权限不足
				404, // Not Found - 资源不存在
				405, // Method Not Allowed - HTTP 方法不被接受
				406, // Not Acceptable - 无法满足 Accept 头
				409, // Conflict - 业务冲突
				410, // Gone - 资源已永久删除
				411, // Length Required - 缺少 Content-Length
				413, // Payload Too Large - 请求体超限
				414, // URI Too Long - URL 超长
				415, // Unsupported Media Type - Content-Type 不支持
				422, // Unprocessable Entity - 语义错误
				501, // Not Implemented - 服务端不支持
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
