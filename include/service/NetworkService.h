#pragma once

#include <QByteArray>
#include <QHash>
#include <QtPromise>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <client/NetworkClient.h>
#include <client/NetworkClientError.h>
#include <core/global.h>
#include <request/HttpRequest.h>
#include <request/HttpSpec.h>

namespace NetCore {
namespace detail {

// 不能用 std::conditional_t：它两个分支的类型都要合法，
// 而响应类型为 void 时 std::function<void(const void&)> 本身就不成立
template <typename ResponseType>
struct SuccessCallbackOf {
	using type = std::function<void(const ResponseType&)>;
};

template <>
struct SuccessCallbackOf<void> {
	using type = std::function<void()>;
};

} // namespace detail

// 业务层基类：一个 Service 一组 API，共用同一个 client（连接池复用）和通用 Header
class NETWORK_EXPORT NetworkService {
public:
	explicit NetworkService(std::shared_ptr<NetworkClient> client);
	virtual ~NetworkService() = default;

	NetworkService(const NetworkService&) = delete;
	NetworkService& operator=(const NetworkService&) = delete;

protected:
	// void 响应的成功回调不带参数
	template <class RequestMeta>
	using SuccessCallback =
		typename detail::SuccessCallbackOf<typename RequestMeta::response_type>::type;

	using FailCallback = std::function<void(const NetworkClientError&)>;

	// 子类重写以注入 Authorization、Accept-Language 之类的通用 Header
	virtual QHash<QByteArray, QByteArray> commonHeaders() const { return {}; }

	// 风格 1：回调。失败静默忽略
	template <class RequestMeta>
	void httpCall(const HttpRequestInstance<RequestMeta>& inst,
		SuccessCallback<RequestMeta> onSuccess)
	{
		httpCall<RequestMeta>(inst, std::move(onSuccess), nullptr);
	}

	template <class RequestMeta>
	void httpCall(const HttpRequestInstance<RequestMeta>& inst,
		SuccessCallback<RequestMeta> onSuccess, FailCallback onFail)
	{
		using ResponseType = typename RequestMeta::response_type;

		auto request = inst;
		applyCommonHeaders(request);

		auto pending = client_->sendRequest<RequestMeta>(request);

		if constexpr (std::is_void_v<ResponseType>) {
			attachFailHandler(
				pending.then([onSuccess]() {
					if (onSuccess) onSuccess();
				}),
				std::move(onFail));
		}
		else {
			attachFailHandler(
				pending.then([onSuccess](const ResponseType& response) {
					if (onSuccess) onSuccess(response);
				}),
				std::move(onFail));
		}
	}

	// 风格 2：Promise，适合链式串联多个请求
	template <class RequestMeta>
	QtPromise::QPromise<typename RequestMeta::response_type>
	httpPromise(HttpRequestInstance<RequestMeta> inst)
	{
		applyCommonHeaders(inst);
		return client_->sendRequest<RequestMeta>(inst);
	}

	// 风格 3：运行时构建的动态请求
	QtPromise::QPromise<QByteArray> httpSpecPromise(HttpSpec spec);

	template <typename ResponseType>
	QtPromise::QPromise<ResponseType> httpSpecPromise(HttpSpec spec)
	{
		applyCommonHeaders(spec);
		return client_->sendSpec<ResponseType>(spec);
	}

	const std::shared_ptr<NetworkClient>& client() const { return client_; }

private:
	template <class RequestMeta>
	void applyCommonHeaders(HttpRequestInstance<RequestMeta>& inst) const
	{
		const auto headers = commonHeaders();
		for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
			inst.runtime_headers.insert(it.key(), it.value());
		}
	}

	void applyCommonHeaders(HttpSpec& spec) const;

	// 非模板，实现放 .cpp：异常分派只在一个编译单元里实例化
	static void attachFailHandler(QtPromise::QPromise<void> promise, FailCallback onFail);

	std::shared_ptr<NetworkClient> client_;
};

} // namespace NetCore
