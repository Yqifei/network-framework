#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslSocket>
#include <QTextCodec>
#include <QTimer>
#include <QtDebug>
#include <chrono>
#include <memory>

#include <client/Interceptor.h>
#include <client/NetworkClient.h>
#include <client/NetworkException.h>
#include <core/Version.h>
#include <request/HttpRequest.h>
#include <request/HttpSpec.h>
#include <service/NetworkService.h>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

using namespace NetCore;

// HttpSpec 走绝对 URL，和 client 的 baseUrl 一起从 HTTPBIN_URL 读，
// 便于指向本地 docker 实例（README 里有说明）
static QString kHttpBinBase;

// ======================================================================
// httpbin 回显模型：只声明关心的字段（IgnoreUnknownKeys 默认开启）
// ======================================================================
class HttpBinEcho
{
    Q_GADGET
    Q_PROPERTY(QString url MEMBER url)
    Q_PROPERTY(QString origin MEMBER origin)
    Q_PROPERTY(QString data MEMBER data)
    Q_PROPERTY(QJsonObject args MEMBER args)
    Q_PROPERTY(QJsonObject headers MEMBER headers)
    Q_PROPERTY(QJsonObject form MEMBER form)
    Q_PROPERTY(QJsonObject files MEMBER files)
    Q_PROPERTY(QJsonObject json MEMBER json)

public:
    QString url;
    QString origin;
    QString data;
    QJsonObject args;
    QJsonObject headers;
    QJsonObject form;
    QJsonObject files;
    QJsonObject json;
};
Q_DECLARE_METATYPE(HttpBinEcho)

// ======================================================================
// 自定义日志拦截器 — 洋葱模型示例
// ======================================================================
class LogInterceptor : public HttpInterceptor
{
public:
    QtPromise::QPromise<QByteArray> intercept(
        const QNetworkRequest& req, NextHandler next) override
    {
        qDebug() << "\n[LOG] >>>>> Request:" << req.url().toString();
        const auto startTime = QDateTime::currentMSecsSinceEpoch();
        return next()
        .then([startTime, url = req.url().toString()](const QByteArray& data) {
            const auto elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;
            qDebug() << "[LOG] <<<<<< Response:" << url
                     << "| size:" << data.size() << "bytes"
                     << "| time:" << elapsed << "ms";
            return data;
        })
        .fail([startTime, url = req.url().toString()](const NetworkException& ex) {
            const auto elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;
            qDebug() << "[LOG] <<<<<< FAILED:" << url
                     << "| error:" << ex.error().message()
                     << "| time:" << elapsed << "ms";
            return QtPromise::QPromise<QByteArray>::reject(ex);
        });
    }
};

// ======================================================================
// 编译期 API 声明
// ======================================================================
using GetEcho = HttpRequest<HttpMethod::GET, STR("get"), HttpBinEcho,
    Query<STR("page"), TypeInt>,
    Query<STR("tag"), TypeString>>;

using PostJson = HttpRequest<HttpMethod::POST, STR("post"), HttpBinEcho,
    Header<STR("Content-Type"), TypeString>,
    Body<TypeBinary>>;

using PostForm = HttpRequest<HttpMethod::POST, STR("post"), HttpBinEcho,
    Header<STR("Content-Type"), TypeString>,
    Form<STR("user"), TypeString>,
    Form<STR("pw"), TypeString>>;

using UploadFile = HttpMultipartRequest<HttpMethod::POST, STR("post"), HttpBinEcho,
    Form<STR("file"), TypeFile>,
    Form<STR("tag"), TypeString>>;

using PutRaw = HttpRequest<HttpMethod::PUT, STR("put"), HttpBinEcho,
    Header<STR("Content-Type"), TypeString>,
    Body<TypeBinary>>;

using PatchJson = HttpRequest<HttpMethod::PATCH, STR("patch"), HttpBinEcho,
    Header<STR("Content-Type"), TypeString>,
    Body<TypeBinary>>;

using DeleteThing = HttpRequest<HttpMethod::DEL, STR("delete"), void>;

using Get500 = HttpRequest<HttpMethod::GET, STR("status/500"), void>;

using GetSlow = HttpRequest<HttpMethod::GET, STR("delay/5"), void>;

using GetRedirect = HttpRequest<HttpMethod::GET, STR("redirect/3"), HttpBinEcho>;

using Get404 = HttpRequest<HttpMethod::GET, STR("status/404"), void>;

using GetBytes = HttpRequest<HttpMethod::GET, STR("bytes/16"), void>;

// ======================================================================
// Service 层
// ======================================================================
class DemoService : public NetworkService
{
public:
    using NetworkService::NetworkService;

    QHash<QByteArray, QByteArray> commonHeaders() const override
    {
        return { { "X-App-Version", "1.0.0" } };
    }

    // 1. GET 回显：Query 编译期拼接 + runtime Authorization（自动加 Bearer 前缀）
    QtPromise::QPromise<HttpBinEcho> echoGet()
    {
        auto inst = GetEcho::make(2, "demo");
        inst.runtime_headers.insert("Authorization", "demo-token");
        return httpPromise<GetEcho>(inst);
    }

    // 2. POST JSON body
    QtPromise::QPromise<HttpBinEcho> postJson(const QByteArray& payload)
    {
        return httpPromise<PostJson>(PostJson::make("application/json", payload));
    }

    // 3. POST 表单（urlencoded）
    QtPromise::QPromise<HttpBinEcho> postForm()
    {
        return httpPromise<PostForm>(PostForm::make(
            "application/x-www-form-urlencoded", "admin", "123456"));
    }

    // 4. multipart 文件上传
    QtPromise::QPromise<HttpBinEcho> uploadFile()
    {
        const auto file = FileValue::fromBytes(
            "hello httpbin", QStringLiteral("note.txt"), QStringLiteral("text/plain"));
        return httpPromise<UploadFile>(UploadFile::make(file, "demo"));
    }

    // 5. PUT 原始字节 / PATCH JSON / DELETE void
    QtPromise::QPromise<HttpBinEcho> putRaw(const QByteArray& data)
    {
        return httpPromise<PutRaw>(PutRaw::make("text/plain", data));
    }

    QtPromise::QPromise<HttpBinEcho> patchJson(const QByteArray& payload)
    {
        return httpPromise<PatchJson>(PatchJson::make("application/json", payload));
    }

    void deleteThing()
    {
        httpCall<DeleteThing>(DeleteThing::make(),
            []() { qDebug() << "[delete] success (void 响应)"; },
            [](const NetworkClientError& e) {
                qWarning() << "[delete] fail:" << e.message();
            });
    }

    // 6. HttpSpec 动态请求
    QtPromise::QPromise<QByteArray> anythingProbe()
    {
        return httpSpecPromise(HttpSpec::get(kHttpBinBase + "anything/network-framework/probe")
            .query(QStringLiteral("tag"), QStringLiteral("demo"))
            .header("X-Trace-Id", "nf-001"));
    }

    // 7. 500：黑名单外，RetryInterceptor 会重试（1 首试 + 2 重试）
    QtPromise::QPromise<void> status500()
    {
        return httpPromise<Get500>(Get500::make());
    }

    // 8. /delay/5 + 2s 请求级超时覆盖：真正触发 abort
    QtPromise::QPromise<void> fetchSlow()
    {
        auto inst = GetSlow::make();
        inst.withTimeout(2000);
        return httpPromise<GetSlow>(inst);
    }

    // 9. 重定向：自动跟随 3 跳
    QtPromise::QPromise<HttpBinEcho> redirectProbe()
    {
        return httpPromise<GetRedirect>(GetRedirect::make());
    }

    // 10. 404：黑名单内，不重试，快速失败
    QtPromise::QPromise<void> status404()
    {
        return httpPromise<Get404>(Get404::make());
    }

    // 11. Cookie 会话：/cookies/set 设置后自动跟随重定向，新请求自动带上
    QtPromise::QPromise<QByteArray> setSessionCookie()
    {
        return httpSpecPromise(HttpSpec::get(kHttpBinBase + "cookies/set?session=demo123"));
    }

    QtPromise::QPromise<QByteArray> readCookies()
    {
        return httpSpecPromise(HttpSpec::get(kHttpBinBase + "cookies"));
    }

    // 12. 随机二进制（void 响应）与 gzip（QNAM 自动解压）
    QtPromise::QPromise<void> fetchBytes()
    {
        return httpPromise<GetBytes>(GetBytes::make());
    }

    QtPromise::QPromise<QByteArray> fetchGzip()
    {
        return httpSpecPromise(HttpSpec::get(kHttpBinBase + "gzip"));
    }
};

// ======================================================================
// main
// ======================================================================
int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
#endif
    QCoreApplication app(argc, argv);
    qDebug() << "NetworkLib" << versionString();
    if (!QSslSocket::supportsSsl()) {
        qWarning() << "没有可用的 OpenSSL，https 请求会失败。"
                      "把 libssl-1_1-x64.dll 和 libcrypto-1_1-x64.dll 放到 exe 旁边即可";
    }

    kHttpBinBase = qEnvironmentVariable(
        "HTTPBIN_URL", QStringLiteral("https://httpbin.org/"));
    if (!kHttpBinBase.endsWith(QLatin1Char('/'))) kHttpBinBase += QLatin1Char('/');

    RetryInterceptor::Policy retryPolicy;
    retryPolicy.maxRetries = 2;
    retryPolicy.delay = std::chrono::milliseconds(300);

    // 顺序有讲究：Retry 在外层、Log 在内层。
    // 每个请求只经过 Log 一次，但每次重试尝试都会重新进入内层 -> 重试过程可见
    auto client = NetworkClientBuilder()
        .baseUrl(kHttpBinBase)
        .totalTimeout(std::chrono::seconds(15))
        .followRedirects(QNetworkRequest::NoLessSafeRedirectPolicy, 5)
        .addInterceptor(std::make_shared<RetryInterceptor>(retryPolicy))
        .addInterceptor(std::make_shared<LogInterceptor>())
        .build();

    DemoService service(client);

    qDebug() << "\n======= 1. GET 回显 (Query / Header / Bearer) =========";
    service.echoGet()
        .then([](const HttpBinEcho& echo) {
            qDebug() << "[get] args 原文:"
                     << QJsonDocument(echo.args).toJson(QJsonDocument::Compact).constData();
            qDebug() << "[get] page:" << echo.args.value("page").toString().toInt()
                     << "| tag:" << echo.args.value("tag").toString()
                     << "| X-App-Version:" << echo.headers.value("X-App-Version").toString()
                     << "| Authorization:" << echo.headers.value("Authorization").toString();
        })
        .fail([](const NetworkException& ex) {
            qWarning() << "[get] fail:" << ex.error().message();
        });

    qDebug() << "\n======= 2. POST JSON =========";
    {
        QJsonObject payload;
        payload["title"] = "Hello NetworkLib";
        payload["body"] = "posted from the example";
        const QByteArray postBody = QJsonDocument(payload).toJson();
        service.postJson(postBody)
            .then([](const HttpBinEcho& echo) {
                qDebug() << "[post-json] 回显:"
                         << QJsonDocument(echo.json).toJson(QJsonDocument::Compact).constData();
            })
            .fail([](const NetworkException& ex) {
                qWarning() << "[post-json] fail:" << ex.error().message();
            });
    }

    qDebug() << "\n======= 3. POST 表单 (urlencoded) =========";
    service.postForm()
        .then([](const HttpBinEcho& echo) {
            qDebug() << "[post-form] user:" << echo.form.value("user").toString()
                     << "| pw:" << echo.form.value("pw").toString();
        })
        .fail([](const NetworkException& ex) {
            qWarning() << "[post-form] fail:" << ex.error().message();
        });

    qDebug() << "\n======= 4. Multipart 文件上传 =========";
    service.uploadFile()
        .then([](const HttpBinEcho& echo) {
            qDebug() << "[upload] files.file:" << echo.files.value("file").toString()
                     << "| form.tag:" << echo.form.value("tag").toString();
        })
        .fail([](const NetworkException& ex) {
            qWarning() << "[upload] fail:" << ex.error().message();
        });

    qDebug() << "\n======= 5. PUT / PATCH / DELETE =========";
    service.putRaw("hello httpbin")
        .then([](const HttpBinEcho& echo) {
            qDebug() << "[put] 回显 data:" << echo.data;
        })
        .fail([](const NetworkException& ex) {
            qWarning() << "[put] fail:" << ex.error().message();
        });
    {
        QJsonObject patchPayload;
        patchPayload["sold"] = true;
        service.patchJson(QJsonDocument(patchPayload).toJson())
            .then([](const HttpBinEcho& echo) {
                qDebug() << "[patch] 回显:"
                         << QJsonDocument(echo.json).toJson(QJsonDocument::Compact).constData();
            })
            .fail([](const NetworkException& ex) {
                qWarning() << "[patch] fail:" << ex.error().message();
            });
    }
    service.deleteThing();

    qDebug() << "\n======= 6. HttpSpec 动态请求 =========";
    service.anythingProbe()
        .then([](const QByteArray& raw) {
            const QJsonObject root = QJsonDocument::fromJson(raw).object();
            qDebug() << "[spec] method:" << root.value("method").toString()
                     << "| url:" << root.value("url").toString()
                     << "| X-Trace-Id:"
                     << root.value("headers").toObject().value("X-Trace-Id").toString();
        })
        .fail([](const NetworkException& ex) {
            qWarning() << "[spec] fail:" << ex.error().message();
        });

    qDebug() << "\n======= 7. 重试 (500: 1 首试 + 2 重试) =========";
    service.status500()
        .then([]() { qDebug() << "[retry-500] 意外成功"; })
        .fail([](const NetworkException& ex) {
            qDebug() << "[retry-500] 重试耗尽后失败:" << ex.error().message()
                     << "\n            上方 [LOG] 应出现 3 次请求(每次间隔 300ms)";
        });

    qDebug() << "\n======= 8. 超时 (/delay/5 vs withTimeout(2000)) =========";
    service.fetchSlow()
        .then([]() { qDebug() << "[timeout] 意外成功"; })
        .fail([](const NetworkException& ex) {
            qDebug() << "[timeout] 2s 超时覆盖触发:" << ex.error().message()
                     << "\n            (注意: 超时默认也参与重试, 所以共 3 次尝试)";
        });

    qDebug() << "\n======= 9. 重定向 (自动跟随 3 跳) =========";
    service.redirectProbe()
        .then([](const HttpBinEcho& echo) {
            qDebug() << "[redirect] 最终落地 URL:" << echo.url;
        })
        .fail([](const NetworkException& ex) {
            qWarning() << "[redirect] fail:" << ex.error().message();
        });

    qDebug() << "\n======= 10. 错误分类 (404: 黑名单, 不重试) =========";
    service.status404()
        .then([]() { qDebug() << "[404] 意外成功"; })
        .fail([](const NetworkException& ex) {
            const auto& err = ex.error();
            qDebug() << "[404] caught!"
                     << "| isHttpError:" << err.isHttpError()
                     << "| isTimeout:" << err.isTimeout();
            if (const auto http = err.asHttp())
                qDebug() << "      HTTP status" << http->status;
        });

    qDebug() << "\n======= 11. Cookie 会话 (同一 client 持久化) =========";
    service.setSessionCookie()
        .then([&service](const QByteArray& first) {
            qDebug() << "[cookies] 设置后(自动跟随重定向到 /cookies):"
                     << first.left(100).constData();
            return service.readCookies();
        })
        .then([](const QByteArray& second) {
            qDebug() << "[cookies] 新请求自动带上:" << second.left(100).constData();
        })
        .fail([](const NetworkException& ex) {
            qWarning() << "[cookies] fail:" << ex.error().message();
        });

    qDebug() << "\n======= 12. 二进制与 gzip =========";
    service.fetchBytes()
        .then([]() {
            qDebug() << "[bytes] 16 字节随机数据, void 响应跳过反序列化";
        })
        .fail([](const NetworkException& ex) {
            qWarning() << "[bytes] fail:" << ex.error().message();
        });
    service.fetchGzip()
        .then([](const QByteArray& raw) {
            qDebug() << "[gzip] QNAM 自动解压:" << raw.left(80).constData();
        })
        .fail([](const NetworkException& ex) {
            qWarning() << "[gzip] fail:" << ex.error().message();
        });

    QTimer::singleShot(20000, &app, &QCoreApplication::quit);
    return app.exec();
}
#include "main.moc"
