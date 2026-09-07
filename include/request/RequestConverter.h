#pragma once

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QIODevice>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <request/HttpRequest.h>
#include <request/RequestAttrCode.h>
#include <serializer/JsonSerializer.h>

namespace NetCore {

	struct ValueConverter {
		static QString toString(std::string_view v)
		{
			return QString::fromUtf8(v.data(), static_cast<int>(v.size()));
		}

		static QString toString(int64_t v) { return QString::number(v); }

		static QString toString(double v) { return QString::number(v); }

		static QString toString(bool v) { return v ? "true" : "false"; }

		static QString toString(QString v) { return v; }
	};

	class RequestConverter {
	public:
		RequestConverter() = delete;
		~RequestConverter() = delete;

		template <typename ParamValueTuple, class Fn>
		static void forEachHttpRequestParam(ParamValueTuple&& param_values, Fn&& fn)
		{
			std::apply([&](auto &&...param_value) {
				(fn(param_value), ...);
				}, std::forward<ParamValueTuple>(param_values));
		}

		// 请求体构建：按 body 参数的 value_tag 编译期分派序列化策略
		template <typename RequestMeta>
		static QByteArray buildBody(const HttpRequestInstance<RequestMeta>& request_ins)
		{
			QByteArray out_body;

			// body 参数最多 1 个，HttpRequest 里有 static_assert 保证
			if constexpr (std::tuple_size_v<typename RequestMeta::body_params> > 0) {
				forEachHttpRequestParam(request_ins.body.values, [&out_body](auto&& pv) {
					using PV = std::decay_t<decltype(pv)>;

					if constexpr (IsModelType_v<typename PV::value_tag>) {
						// TypeModel<T>：走 Qt 元对象反射序列化为 JSON
						JsonSerializer serializer;
						out_body = serializer.serializeToBytes(pv.value);
					}
					else if constexpr (std::is_same_v<typename PV::value_tag, TypeBinary>) {
						// TypeBinary：原样传递（protobuf 等二进制协议）
						out_body = pv.value;
					}
					else {
						// 其他（TypeString 等）：转为 UTF-8 字节
						out_body.append(ValueConverter::toString(pv.value).toUtf8());
					}
				});
			}

			// Form 参数 URL 编码（仅非 multipart 请求）
			if constexpr (!is_multipart_request_v<RequestMeta>
				&& std::tuple_size_v<typename RequestMeta::form_params> > 0) {
				QUrlQuery form_data;
				forEachHttpRequestParam(request_ins.form.values, [&form_data](auto&& pv) {
					form_data.addQueryItem(
						ValueConverter::toString(pv.key),
						ValueConverter::toString(pv.value));
				});
				out_body.append(form_data.toString(QUrl::FullyEncoded).toUtf8());
			}

			return out_body;
		}

		// multipart/form-data 构建：unique_ptr 保证文件打开失败抛异常时自动释放
		template <typename RequestMeta>
		static QHttpMultiPart* buildMultipart(const HttpRequestInstance<RequestMeta>& request_ins)
		{
			std::unique_ptr<QHttpMultiPart> mp(new QHttpMultiPart(QHttpMultiPart::FormDataType));

			forEachHttpRequestParam(request_ins.form.values, [raw_mp = mp.get()](auto&& pv) {
				using PV = std::decay_t<decltype(pv)>;
				using ValueType = typename PV::value_type;
				const QByteArray name(pv.key.data(), static_cast<int>(pv.key.size()));
				QHttpPart part;

				if constexpr (std::is_same_v<ValueType, FileValue>) {
					const FileValue& fv = pv.value;
					// 手动构造的 FileValue 可能没填 fileName，从路径兜底
					const QString filename = fv.fileName.isEmpty()
						? QFileInfo(fv.filePath).fileName()
						: fv.fileName;

					part.setHeader(QNetworkRequest::ContentDispositionHeader,
						"form-data; name=\"" + name + "\"; filename=\"" + filename.toUtf8() + "\"");
					if (!fv.contentType.isEmpty()) {
						part.setHeader(QNetworkRequest::ContentTypeHeader, fv.contentType);
					}

					if (!fv.filePath.isEmpty()) {
						// QFile 挂 mp 为 parent：mp 销毁时自动关闭并释放
						auto* file = new QFile(fv.filePath, raw_mp);
						if (!file->open(QIODevice::ReadOnly)) {
							throw std::runtime_error(
								"RequestConverter: cannot open file: " + fv.filePath.toStdString());
						}
						part.setBodyDevice(file);
					} else {
						part.setBody(fv.data);
					}
				}
				else if constexpr (std::is_same_v<ValueType, QByteArray>) {
					part.setHeader(QNetworkRequest::ContentDispositionHeader,
						"form-data; name=\"" + name + "\"");
					part.setBody(pv.value);
				}
				else {
					part.setHeader(QNetworkRequest::ContentDispositionHeader,
						"form-data; name=\"" + name + "\"");
					part.setBody(ValueConverter::toString(pv.value).toUtf8());
				}
				raw_mp->append(part);
			});

			return mp.release();
		}

		// HttpRequestInstance -> QNetworkRequest
		template <typename RequestMeta>
		static QNetworkRequest convertToQNetworkRequest(
			const QString& base_url,
			const HttpRequestInstance<RequestMeta>& request_ins)
		{
			QNetworkRequest q_request;

			// 路径参数替换：api/users/{id} -> api/users/42
			QString path = QString::fromUtf8(RequestMeta::path_cstr);
			forEachHttpRequestParam(request_ins.path.values, [&path](auto&& pv) {
				const QString key = ValueConverter::toString(pv.key);
				const QString value = ValueConverter::toString(pv.value);
				path.replace("{" + key + "}", value);
			});

			// Query 参数
			QUrlQuery query;
			forEachHttpRequestParam(request_ins.query.values, [&query](auto&& pv) {
				query.addQueryItem(
					ValueConverter::toString(pv.key),
					ValueConverter::toString(pv.value));
			});

			// 组装完整 URL
			QUrl url(base_url + path);
			if (!query.isEmpty()) url.setQuery(query);
			q_request.setUrl(url);

			// 运行时 headers；Authorization 自动加 "Bearer " 前缀
			for (auto it = request_ins.runtime_headers.cbegin();
				it != request_ins.runtime_headers.cend(); ++it)
			{
				if (it.key() == "Authorization") {
					q_request.setRawHeader(it.key(), "Bearer " + it.value());
					continue;
				}
				q_request.setRawHeader(it.key(), it.value());
			}

			// 编译期声明的 Header 参数
			forEachHttpRequestParam(request_ins.headers.values, [&q_request](auto&& pv) {
				q_request.setRawHeader(
					QByteArray(pv.key.data(), static_cast<int>(pv.key.size())),
					ValueConverter::toString(pv.value).toUtf8());
			});

			// Body 存入自定义属性；拦截器链只传递 QNetworkRequest
			auto body = buildBody(request_ins);
			if (!body.isEmpty()) {
				q_request.setAttribute(
					static_cast<QNetworkRequest::Attribute>(kRequestBodyAttrCode),
					body);
			}

			// 请求级超时覆盖
			if (request_ins.timeout_override_ms >= 0) {
				q_request.setAttribute(
					static_cast<QNetworkRequest::Attribute>(kRequestTimeoutAttrCode),
					request_ins.timeout_override_ms);
			}

			return q_request;
		}
	};

} // namespace NetCore
