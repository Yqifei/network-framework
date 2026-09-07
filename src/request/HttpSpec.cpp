#include <request/HttpSpec.h>

#include <QFile>
#include <QFileInfo>
#include <QHttpPart>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <memory>
#include <stdexcept>

#include <request/RequestAttrCode.h>

namespace NetCore {

HttpSpec::HttpSpec(const QString& url, HttpMethod method)
	: url_(url)
	, method_(method)
{
}

HttpSpec::~HttpSpec() = default;

HttpSpec HttpSpec::get(const QString& url) { return HttpSpec(url, HttpMethod::GET); }
HttpSpec HttpSpec::post(const QString& url) { return HttpSpec(url, HttpMethod::POST); }
HttpSpec HttpSpec::put(const QString& url) { return HttpSpec(url, HttpMethod::PUT); }
HttpSpec HttpSpec::del(const QString& url) { return HttpSpec(url, HttpMethod::DEL); }
HttpSpec HttpSpec::patch(const QString& url) { return HttpSpec(url, HttpMethod::PATCH); }

HttpSpec& HttpSpec::header(const QByteArray& key, const QByteArray& value)
{
	headers_.insert(key, value);
	return *this;
}

HttpSpec& HttpSpec::query(const QString& key, const QString& value)
{
	query_params_.insert(key, value);
	return *this;
}

HttpSpec& HttpSpec::bodyJson(const QJsonValue& json)
{
	if (json.isObject()) {
		body_ = QJsonDocument(json.toObject()).toJson(QJsonDocument::Compact);
	}
	else if (json.isArray()) {
		body_ = QJsonDocument(json.toArray()).toJson(QJsonDocument::Compact);
	}
	else {
		// QJsonDocument 只接受对象和数组，标量先包成数组再把方括号去掉
		const QByteArray wrapped =
			QJsonDocument(QJsonArray{ json }).toJson(QJsonDocument::Compact);
		body_ = wrapped.mid(1, wrapped.size() - 2);
	}
	content_type_ = "application/json";
	return *this;
}

HttpSpec& HttpSpec::bodyForm(const QHash<QString, QString>& kv)
{
	QUrlQuery form;
	for (auto it = kv.cbegin(); it != kv.cend(); ++it) {
		form.addQueryItem(it.key(), it.value());
	}
	body_ = form.toString(QUrl::FullyEncoded).toUtf8();
	content_type_ = "application/x-www-form-urlencoded";
	return *this;
}

HttpSpec& HttpSpec::bodyRaw(const QByteArray& data, const QByteArray& contentType)
{
	body_ = data;
	content_type_ = contentType;
	return *this;
}

HttpSpec& HttpSpec::filePart(const QByteArray& fieldName, const FileValue& file)
{
	parts_.append(MultipartEntry{ fieldName, file });
	return *this;
}

HttpSpec& HttpSpec::formPart(const QByteArray& fieldName, const QString& value)
{
	parts_.append(MultipartEntry{ fieldName, value });
	return *this;
}

HttpSpec& HttpSpec::timeout(std::chrono::milliseconds ms)
{
	timeout_override_ms_ = static_cast<int>(ms.count());
	return *this;
}

HttpMethod HttpSpec::method() const noexcept { return method_; }

const QByteArray& HttpSpec::body() const noexcept { return body_; }

int HttpSpec::timeoutOverride() const noexcept { return timeout_override_ms_; }

bool HttpSpec::isMultipart() const noexcept { return !parts_.isEmpty(); }

QNetworkRequest HttpSpec::toNetworkRequest() const
{
	QUrl url(url_);
	if (!query_params_.isEmpty()) {
		QUrlQuery query;
		for (auto it = query_params_.cbegin(); it != query_params_.cend(); ++it) {
			query.addQueryItem(it.key(), it.value());
		}
		url.setQuery(query);
	}

	QNetworkRequest request(url);

	// multipart 的 Content-Type 由 QHttpMultiPart 自己带 boundary，别覆盖
	if (!isMultipart() && !body_.isEmpty() && !content_type_.isEmpty()) {
		request.setHeader(QNetworkRequest::ContentTypeHeader, content_type_);
	}

	// 显式设置的 header 优先于上面推导出来的
	for (auto it = headers_.cbegin(); it != headers_.cend(); ++it) {
		request.setRawHeader(it.key(), it.value());
	}

	if (timeout_override_ms_ >= 0) {
		request.setAttribute(
			static_cast<QNetworkRequest::Attribute>(kRequestTimeoutAttrCode),
			timeout_override_ms_);
	}

	return request;
}

QHttpMultiPart* HttpSpec::buildMultipart() const
{
	std::unique_ptr<QHttpMultiPart> mp(new QHttpMultiPart(QHttpMultiPart::FormDataType));

	for (const MultipartEntry& entry : parts_) {
		QHttpPart part;

		if (const auto* file = std::get_if<FileValue>(&entry.value)) {
			const QString filename = file->fileName.isEmpty()
				? QFileInfo(file->filePath).fileName()
				: file->fileName;

			part.setHeader(QNetworkRequest::ContentDispositionHeader,
				"form-data; name=\"" + entry.fieldName + "\"; filename=\""
					+ filename.toUtf8() + "\"");
			if (!file->contentType.isEmpty()) {
				part.setHeader(QNetworkRequest::ContentTypeHeader, file->contentType);
			}

			if (!file->filePath.isEmpty()) {
				// QFile 挂 mp 为 parent，mp 销毁时自动关闭并释放
				auto* handle = new QFile(file->filePath, mp.get());
				if (!handle->open(QIODevice::ReadOnly)) {
					throw std::runtime_error(
						"HttpSpec: cannot open file: " + file->filePath.toStdString());
				}
				part.setBodyDevice(handle);
			}
			else {
				part.setBody(file->data);
			}
		}
		else {
			part.setHeader(QNetworkRequest::ContentDispositionHeader,
				"form-data; name=\"" + entry.fieldName + "\"");
			part.setBody(std::get<QString>(entry.value).toUtf8());
		}

		mp->append(part);
	}

	return mp.release();
}

} // namespace NetCore
