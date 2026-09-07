#include <client/NetworkClient.h>

#include <QVariant>
#include <memory>
#include <utility>

namespace NetCore {

NetworkClient::NetworkClient(QObject* parent)
	: QObject(parent)
	, network_manager_(std::make_unique<QNetworkAccessManager>())
{
}

NetworkClient::~NetworkClient() = default;

QtPromise::QPromise<QByteArray> NetworkClient::rejectWith(NetworkClientError error)
{
	return QtPromise::QPromise<QByteArray>::reject(NetworkException(std::move(error)));
}

QtPromise::QPromise<QByteArray> NetworkClient::rejectDestroyed()
{
	return rejectWith(NetworkClientError::logic(0, QStringLiteral("client destroyed")));
}

QtPromise::QPromise<QByteArray> NetworkClient::rejectMultipartMethod()
{
	return rejectWith(NetworkClientError::logic(
		0, QStringLiteral("Multipart only supports POST/PUT/PATCH")));
}

// 从后往前套娃：后包的在外层，调用最外层时就按添加顺序由外向内执行
QtPromise::QPromise<QByteArray> NetworkClient::executeWithInterceptors(
	const QNetworkRequest& req, NextHandler terminus) const
{
	NextHandler handler = std::move(terminus);

	for (int i = interceptors_.size() - 1; i >= 0; --i) {
		auto interceptor = interceptors_[i];
		auto inner = handler;
		handler = [interceptor, req, inner]() {
			return interceptor->intercept(req, inner);
		};
	}

	return handler();
}

QtPromise::QPromise<QByteArray> NetworkClient::sendRequest(
	const QNetworkRequest& req, HttpMethod method) const
{
	QNetworkRequest q_req = req;
	if (max_redirects_ > 0) {
		q_req.setMaximumRedirectsAllowed(max_redirects_);
	}

	// body 是转换阶段塞进自定义属性的，拦截器链只传递 QNetworkRequest
	const QByteArray body = q_req.attribute(
		static_cast<QNetworkRequest::Attribute>(kRequestBodyAttrCode)).toByteArray();

	QNetworkReply* reply = nullptr;
	switch (method) {
	case HttpMethod::GET:
		reply = network_manager_->get(q_req);
		break;
	case HttpMethod::DEL:
		reply = network_manager_->deleteResource(q_req);
		break;
	case HttpMethod::POST:
		reply = network_manager_->post(q_req, body);
		break;
	case HttpMethod::PUT:
		reply = network_manager_->put(q_req, body);
		break;
	case HttpMethod::PATCH:
		reply = network_manager_->sendCustomRequest(q_req, "PATCH", body);
		break;
	}

	if (!reply) {
		return rejectWith(NetworkClientError::logic(
			0, QStringLiteral("Unknown HTTP method")));
	}
	return attachReplyHandler(reply, nullptr);
}

QtPromise::QPromise<QByteArray> NetworkClient::sendMultipartRawRequest(
	const QNetworkRequest& req, HttpMethod method, QHttpMultiPart* mp,
	ProgressCallback progress_callback)
{
	QNetworkRequest q_req = req;
	if (max_redirects_ > 0) {
		q_req.setMaximumRedirectsAllowed(max_redirects_);
	}

	QNetworkReply* reply = nullptr;
	switch (method) {
	case HttpMethod::POST:
		reply = network_manager_->post(q_req, mp);
		break;
	case HttpMethod::PUT:
		reply = network_manager_->put(q_req, mp);
		break;
	case HttpMethod::PATCH:
		reply = network_manager_->sendCustomRequest(q_req, "PATCH", mp);
		break;
	default:
		break;
	}

	if (!reply) {
		mp->deleteLater();
		return rejectMultipartMethod();
	}

	mp->setParent(reply);	// reply 销毁时一并带走
	return attachReplyHandler(reply, std::move(progress_callback));
}

QtPromise::QPromise<QByteArray> NetworkClient::sendSpec(
	const HttpSpec& spec, ProgressCallback progress_callback)
{
	QPointer<NetworkClient> weak_self = this;
	QNetworkRequest net_req = spec.toNetworkRequest();

	if (spec.isMultipart()) {
		// spec 按值捕获：调用方传进来的很可能是临时对象，而 terminus 会被延迟调用
		NextHandler terminus =
			[weak_self, net_req, spec_copy = spec, progress_callback]()
			-> QtPromise::QPromise<QByteArray> {
			if (!weak_self) return rejectDestroyed();

			QHttpMultiPart* mp = nullptr;
			try {
				// 每次重试都得重新造：上一份已经跟着上一个 reply 销毁了
				mp = spec_copy.buildMultipart();
			}
			catch (const std::exception& ex) {
				return rejectWith(NetworkClientError::serialization(
					QString::fromUtf8(ex.what())));
			}
			return weak_self->sendMultipartRawRequest(
				net_req, spec_copy.method(), mp, progress_callback);
		};
		return executeWithInterceptors(net_req, std::move(terminus));
	}

	if (!spec.body().isEmpty()) {
		net_req.setAttribute(
			static_cast<QNetworkRequest::Attribute>(kRequestBodyAttrCode),
			spec.body());
	}

	NextHandler terminus = [weak_self, net_req, method = spec.method()]()
		-> QtPromise::QPromise<QByteArray> {
		if (!weak_self) return rejectDestroyed();
		return weak_self->sendRequest(net_req, method);
	};
	return executeWithInterceptors(net_req, std::move(terminus));
}

QtPromise::QPromise<QByteArray> NetworkClient::attachReplyHandler(
	QNetworkReply* reply, ProgressCallback progress_callback) const
{
	auto promise = QtPromise::QPromise<QByteArray>(
		[this, reply, cb = std::move(progress_callback)](
			const QtPromise::QPromiseResolve<QByteArray>& resolve,
			const QtPromise::QPromiseReject<QByteArray>& reject) {
			if (cb) {
				connect(reply, &QNetworkReply::uploadProgress, this,
					[cb](qint64 sent, qint64 total) { cb(sent, total); });
			}

			// once 用 shared_ptr：下面直调的副本和信号里的副本要共享同一个标志
			auto once = std::make_shared<bool>(false);
			auto handleFinished = [reply, resolve, reject, once]() {
				if (*once) return;
				*once = true;

				reply->deleteLater();	// 不能在信号处理里直接 delete 发信号者

				if (reply->error() != QNetworkReply::NoError) {
					const int status = reply->attribute(
						QNetworkRequest::HttpStatusCodeAttribute).toInt();
					// abort 之后设备已关闭，直接读会报 device not open
					const QByteArray raw_body =
						reply->isOpen() ? reply->readAll() : QByteArray();
					if (status > 0) {
						reject(NetworkException(NetworkClientError::http(
							status, raw_body, reply->errorString())));
					}
					else {
						reject(NetworkException(NetworkClientError::network(
							reply->error(), reply->errorString())));
					}
					return;
				}
				resolve(reply->readAll());
			};

			connect(reply, &QNetworkReply::finished, this, handleFinished);

			// 竞态：finished 可能在 connect 之前就已经发出去了
			if (reply->isFinished()) handleFinished();
		});

	// 请求级超时优先于 client 全局超时
	const QVariant timeout_attr = reply->request().attribute(
		static_cast<QNetworkRequest::Attribute>(kRequestTimeoutAttrCode));
	const int effective_timeout = timeout_attr.isValid()
		? timeout_attr.toInt()
		: timeout_ms_;

	if (effective_timeout <= 0) return promise;

	// 超时回调跑起来时 reply 可能已经 deleteLater 掉了
	QPointer<QNetworkReply> weak_reply = reply;
	return promise.timeout(effective_timeout)
		.fail([weak_reply](const QtPromise::QPromiseTimeoutException&) -> QByteArray {
			if (!weak_reply.isNull()) weak_reply->abort();
			throw NetworkException(NetworkClientError::timeout());
		});
}

} // namespace NetCore
