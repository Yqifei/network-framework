#include <QCoreApplication>
#include <QSslSocket>
#include <QTimer>
#include <QtDebug>
#include <chrono>
#include <memory>

#include <client/Interceptor.h>
#include <client/NetworkClient.h>
#include <core/Version.h>
#include <request/HttpSpec.h>
#include <service/NetworkService.h>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

using namespace NetCore;

// 属性名必须和 JSON 的键一致，反序列化是按属性名去 JSON 里找的
class TodoResponse : public QObject
{
	Q_OBJECT
	Q_PROPERTY(int userId MEMBER userId)
	Q_PROPERTY(int id MEMBER id)
	Q_PROPERTY(QString title MEMBER title)
	Q_PROPERTY(bool completed MEMBER completed)

public:
	using QObject::QObject;

	int userId = 0;
	int id = 0;
	QString title;
	bool completed = false;
};

// 编译期声明一个 API：GET todos/{id}，响应反序列化成 TodoResponse*
using GetTodo = HttpRequest<HttpMethod::GET, STR("todos/{id}"), TodoResponse*,
	Path<STR("id"), TypeInt>>;

class TodoService : public NetworkService
{
public:
	using NetworkService::NetworkService;

	QHash<QByteArray, QByteArray> commonHeaders() const override
	{
		return { { "Accept", "application/json" } };
	}

	// 风格 1：回调
	void printTodo(int id)
	{
		httpCall<GetTodo>(GetTodo::make(id),
			[](TodoResponse* todo) {
				qDebug() << "[callback]" << todo->id << todo->title;
				todo->deleteLater();
			},
			[](const NetworkClientError& error) {
				qWarning() << "[callback] 失败:" << error.message();
			});
	}

	// 风格 2：Promise
	QtPromise::QPromise<TodoResponse*> fetchTodo(int id)
	{
		return httpPromise<GetTodo>(GetTodo::make(id));
	}

	// 风格 3：运行时拼 URL
	QtPromise::QPromise<QByteArray> rawTodo(int id)
	{
		return httpSpecPromise(HttpSpec::get(
			QStringLiteral("https://jsonplaceholder.typicode.com/todos/%1").arg(id)));
	}
};

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
	SetConsoleOutputCP(CP_UTF8);
#endif

	QCoreApplication app(argc, argv);
	qDebug() << "NetworkLib" << versionString();

	if (!QSslSocket::supportsSsl()) {
		qWarning() << "没有可用的 OpenSSL，https 请求会失败。"
					  "把 libssl-1_1-x64.dll 和 libcrypto-1_1-x64.dll 放到 exe 旁边即可";
	}

	auto client = NetworkClientBuilder()
		.baseUrl(QStringLiteral("https://jsonplaceholder.typicode.com/"))
		.totalTimeout(std::chrono::seconds(10))
		.addInterceptor(std::make_shared<RetryInterceptor>())
		.build();

	TodoService service(client);

	service.printTodo(1);

	service.fetchTodo(2)
		.then([](TodoResponse* todo) {
			qDebug() << "[promise]" << todo->id << todo->title;
			todo->deleteLater();
		})
		.fail([]() { qWarning() << "[promise] 失败"; });

	service.rawTodo(3)
		.then([](const QByteArray& raw) { qDebug() << "[spec]" << raw.constData(); })
		.fail([]() { qWarning() << "[spec] 失败"; });

	// 演示程序，给够时间收结果就退出
	QTimer::singleShot(12000, &app, &QCoreApplication::quit);
	return app.exec();
}

#include "main.moc"
