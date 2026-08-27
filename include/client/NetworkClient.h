#pragma once

#include <QByteArray>
#include <QHttpMultiPart>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtPromise>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <client/Interceptor.h>
#include <client/NetworkClientError.h>
#include <client/NetworkException.h>
#include <core/global.h>
#include <request/HttpRequest.h>
#include <request/HttpSpec.h>
#include <request/RequestAttrCode.h>
#include <request/RequestConverter.h>
#include <serializer/JsonSerializer.h>
#include <serializer/SerializerException.h>

namespace NetCore {

using ProgressCallback = std::function<void(qint64 sent, qint64 total)>;

class NETWORK_EXPORT NetworkClient : public QObject {
	Q_OBJECT

public:
	explicit NetworkClient(QObject* parent = nullptr);
	~NetworkClient() override;

	NetworkClient(const NetworkClient&) = delete;
	NetworkClient& operator=(const NetworkClient&) = delete;

	// 编排：转换 -> terminus -> 拦截器链 -> 发请求 -> 桥接 -> 反序列化
	template <class RequestMeta>
	QtPromise::QPromise<typename RequestMeta::response_type>
	sendRequest(const HttpRequestInstance<RequestMeta>& req_ins,
		ProgressCallback progress_callback = nullptr)
	{
		using ResponseType = typename RequestMeta::response_type;

		QNetworkRequest q_req =
			RequestConverter::convertToQNetworkRequest(base_url_, req_ins);

		// 拦截器可能把 terminus 延迟到 client 析构之后才调用，所以捕获 QPointer 而非 this
		QPointer<NetworkClient> weak_self = this;
		NextHandler terminus;

		if constexpr (is_multipart_request_v<RequestMeta>) {
			terminus = [weak_self, q_req, req_ins, progress_callback]()
				-> QtPromise::QPromise<QByteArray> {
				if (!weak_self) return rejectDestroyed();
				return weak_self->sendMultipartRequest(
					q_req, RequestMeta::method, req_ins, progress_callback);
			};
		}
		else {
			terminus = [weak_self, q_req]() -> QtPromise::QPromise<QByteArray> {
				if (!weak_self) return rejectDestroyed();
				return weak_self->sendRequest(q_req, RequestMeta::method);
			};
		}

		return executeWithInterceptors(q_req, std::move(terminus))
			.then([](const QByteArray& data) {
				return deserializeResponse<ResponseType>(data);
			});
	}

	// 运行时动态请求，和 sendRequest 走同一条链路
	QtPromise::QPromise<QByteArray> sendSpec(const HttpSpec& spec,
		ProgressCallback progress_callback = nullptr);

	// ResponseType 只出现在返回类型里推导不出来，所以下面这行不会递归调到自己
	template <typename ResponseType>
	QtPromise::QPromise<ResponseType> sendSpec(const HttpSpec& spec,
		ProgressCallback progress_callback = nullptr)
	{
		return sendSpec(spec, std::move(progress_callback))
			.then([](const QByteArray& data) {
				return deserializeResponse<ResponseType>(data);
			});
	}

private:
	template <typename ResponseType>
	static auto deserializeResponse(const QByteArray& data)
	{
		// void 响应（DELETE、健康检查等）直接跳过，分支根本不会被编译
		if constexpr (std::is_void_v<ResponseType>) {
			Q_UNUSED(data)
			return;
		}
		else {
			try {
				JsonSerializer serializer;
				// QObject 响应声明为 T*：反序列化到堆上，所有权交给调用方
				if constexpr (std::is_pointer_v<ResponseType>) {
					return serializer.deserializeFromBytes<
						std::remove_pointer_t<ResponseType>>(data);
				}
				else {
					return serializer.deserializeFromBytes<ResponseType>(data);
				}
			}
			catch (const DeserializerException& ex) {
				throw NetworkException(NetworkClientError::serialization(
					QStringLiteral("Failed to deserialize: ")
						+ QString::fromUtf8(ex.what()),
					data));
			}
		}
	}

	template <class RequestMeta>
	QtPromise::QPromise<QByteArray> sendMultipartRequest(const QNetworkRequest& req,
		HttpMethod method, const HttpRequestInstance<RequestMeta>& req_ins,
		ProgressCallback progress_callback)
	{
		if (method != HttpMethod::POST && method != HttpMethod::PUT
			&& method != HttpMethod::PATCH) {
			return rejectMultipartMethod();
		}

		QHttpMultiPart* mp = nullptr;
		try {
			mp = RequestConverter::buildMultipart(req_ins);
		}
		catch (const std::exception& ex) {
			return rejectWith(NetworkClientError::serialization(
				QString::fromUtf8(ex.what())));
		}
		return sendMultipartRawRequest(req, method, mp, std::move(progress_callback));
	}

	QtPromise::QPromise<QByteArray> executeWithInterceptors(
		const QNetworkRequest& req, NextHandler terminus) const;

	QtPromise::QPromise<QByteArray> sendRequest(
		const QNetworkRequest& req, HttpMethod method) const;

	QtPromise::QPromise<QByteArray> sendMultipartRawRequest(const QNetworkRequest& req,
		HttpMethod method, QHttpMultiPart* mp, ProgressCallback progress_callback);

	QtPromise::QPromise<QByteArray> attachReplyHandler(
		QNetworkReply* reply, ProgressCallback progress_callback) const;

	static QtPromise::QPromise<QByteArray> rejectWith(NetworkClientError error);
	static QtPromise::QPromise<QByteArray> rejectDestroyed();
	static QtPromise::QPromise<QByteArray> rejectMultipartMethod();

	QString base_url_;
	QList<std::shared_ptr<HttpInterceptor>> interceptors_;
	int timeout_ms_ = 30000;
	std::unique_ptr<QNetworkAccessManager> network_manager_;
	int max_redirects_ = 0;

	friend class NetworkClientBuilder;
};

// 配置与使用分离：build() 之后 client 的配置项外部改不动
class NETWORK_EXPORT NetworkClientBuilder {
public:
	NetworkClientBuilder() = default;

	NetworkClientBuilder& baseUrl(const QString& url)
	{
		base_url_ = url;
		return *this;
	}

	NetworkClientBuilder& totalTimeout(std::chrono::milliseconds timeout)
	{
		total_timeout_ = timeout;
		return *this;
	}

	NetworkClientBuilder& addInterceptor(std::shared_ptr<HttpInterceptor> interceptor)
	{
		interceptors_.append(std::move(interceptor));
		return *this;
	}

	NetworkClientBuilder& followRedirects(
		QNetworkRequest::RedirectPolicy policy = QNetworkRequest::NoLessSafeRedirectPolicy,
		int max_redirects = 3)
	{
		redirect_policy_ = policy;
		max_redirects_ = max_redirects;
		return *this;
	}

	std::shared_ptr<NetworkClient> build() const
	{
		auto client = std::make_shared<NetworkClient>();
		client->base_url_ = base_url_;
		client->timeout_ms_ = static_cast<int>(total_timeout_.count());
		client->interceptors_ = interceptors_;
		client->network_manager_->setRedirectPolicy(redirect_policy_);
		if (redirect_policy_ != QNetworkRequest::ManualRedirectPolicy) {
			client->max_redirects_ = max_redirects_;
		}
		return client;
	}

private:
	QString base_url_;
	std::chrono::milliseconds total_timeout_{ 30000 };
	QList<std::shared_ptr<HttpInterceptor>> interceptors_;
	QNetworkRequest::RedirectPolicy redirect_policy_ = QNetworkRequest::ManualRedirectPolicy;
	int max_redirects_ = 3;
};

} // namespace NetCore
