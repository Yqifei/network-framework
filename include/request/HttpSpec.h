#pragma once

#include <QByteArray>
#include <QHash>
#include <QHttpMultiPart>
#include <QJsonValue>
#include <QList>
#include <QNetworkRequest>
#include <QString>
#include <chrono>
#include <variant>

#include <core/global.h>
#include <request/HttpRequest.h>
#include <serializer/JsonSerializer.h>
#include <serializer/SerializerException.h>

namespace NetCore {

// sendRequest<T> 的运行时版本：URL 和参数在编译期定不下来时用它，
// 内部走的是和编译期版本完全相同的那条链路
class NETWORK_EXPORT HttpSpec {
public:
	~HttpSpec();

	static HttpSpec get(const QString& url);
	static HttpSpec post(const QString& url);
	static HttpSpec put(const QString& url);
	static HttpSpec del(const QString& url);
	static HttpSpec patch(const QString& url);

	HttpSpec& header(const QByteArray& key, const QByteArray& value);
	HttpSpec& query(const QString& key, const QString& value);
	HttpSpec& bodyJson(const QJsonValue& json);

	template <typename T>
	HttpSpec& bodyModel(const T& model)
	{
		JsonSerializer serializer;
		try {
			body_ = serializer.serializeToBytes(model);
			content_type_ = "application/json";
		}
		catch (const SerializerException&) {
			Q_ASSERT(false && "Failed to serialize model");
		}
		return *this;
	}

	HttpSpec& bodyForm(const QHash<QString, QString>& kv);
	HttpSpec& bodyRaw(const QByteArray& data, const QByteArray& contentType);
	HttpSpec& filePart(const QByteArray& fieldName, const FileValue& file);
	HttpSpec& formPart(const QByteArray& fieldName, const QString& value);
	HttpSpec& timeout(std::chrono::milliseconds ms);

	HttpMethod method() const noexcept;
	const QByteArray& body() const noexcept;
	int timeoutOverride() const noexcept;
	bool isMultipart() const noexcept;

protected:
	QNetworkRequest toNetworkRequest() const;

	// 每次调用都造一份新的：重试时上一份已经跟着 reply 一起销毁了
	QHttpMultiPart* buildMultipart() const;

private:
	explicit HttpSpec(const QString& url, HttpMethod method);

	QString url_;
	HttpMethod method_;
	QHash<QByteArray, QByteArray> headers_;
	QHash<QString, QString> query_params_;
	QByteArray body_;
	QByteArray content_type_ = "application/json";
	int timeout_override_ms_ = -1;

	struct MultipartEntry {
		QByteArray fieldName;
		std::variant<FileValue, QString> value;
	};
	QList<MultipartEntry> parts_;

	friend class NetworkClient;
};

} // namespace NetCore
