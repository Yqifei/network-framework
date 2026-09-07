#pragma once

#include <QByteArray>
#include <QNetworkRequest>
#include <QtPromise>
#include <chrono>
#include <functional>

#include <client/LifeCycle.h>
#include <client/NetworkClientError.h>
#include <core/global.h>

namespace NetCore {

// 调用链的下一环：可透传、可短路，也可以多次调用来重试
using NextHandler = std::function<QtPromise::QPromise<QByteArray>()>;

// 洋葱模型：请求由外向内穿过每层拦截器，响应沿原路返回
class NETWORK_EXPORT HttpInterceptor {
public:
	virtual ~HttpInterceptor() = default;

	virtual QtPromise::QPromise<QByteArray> intercept(
		const QNetworkRequest& req, NextHandler next) = 0;
};

class NETWORK_EXPORT RetryInterceptor : public HttpInterceptor, public TrackLifeCycle {
public:
	struct Policy {
		int maxRetries = 3;
		std::chrono::milliseconds delay{ 500 };
		std::function<bool(const NetworkClientError&)> shouldRetry;

		Policy();
	};

	explicit RetryInterceptor(Policy policy = {});
	~RetryInterceptor() override;

	QtPromise::QPromise<QByteArray> intercept(
		const QNetworkRequest& req, NextHandler next) override;

private:
	QtPromise::QPromise<QByteArray> attempt(NextHandler next, int remaining);

	Policy policy_;
};

} // namespace NetCore
