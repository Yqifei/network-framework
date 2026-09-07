# network-framework

基于 Qt 的声明式 HTTP 客户端框架：编译期请求声明、JSON 自动序列化、Promise 异步、拦截器链、Builder 客户端。

## 技术栈

- C++17
- Qt 5.15+
- CMake 3.16+
- [QtPromise](https://github.com/simonbrunel/qtpromise) v0.7.0（内置于 third_party/）

## 快速开始

```cpp
auto client = NetworkClientBuilder()
    .baseUrl(QStringLiteral("https://jsonplaceholder.typicode.com/"))
    .build();

client->sendRequest(GetTodo::make(1))
    .then([](TodoResponse* todo) { qDebug() << todo->title; });
```

## 声明 API

请求是一个类型，URL、方法、参数位置、响应类型都在编译期确定：

```cpp
using GetTodo = HttpRequest<HttpMethod::GET, STR("todos/{id}"), TodoResponse*,
    Path<STR("id"), TypeInt>>;

using Search = HttpRequest<HttpMethod::GET, STR("items"), SearchResult,
    Query<STR("q"), TypeString>,
    Query<STR("page"), TypeInt>>;

using CreateUser = HttpRequest<HttpMethod::POST, STR("users"), UserResponse,
    Body<TypeModel<UserRequest>>>;

using Upload = HttpMultipartRequest<HttpMethod::POST, STR("files"), void,
    Form<STR("file"), TypeFile>,
    Form<STR("name"), TypeString>>;
```

`make()` 的参数顺序和声明顺序一致，类型对不上直接编译报错：

```cpp
auto request = GetTodo::make(1);
auto search = Search::make("qt", 2);
```

编译期约束（违反时 static_assert 报错）：

- `Body` 和 `Form` 不能同时出现
- `Body` 最多一个
- multipart 只能是 POST / PUT / PATCH

响应类型走 Qt 元对象系统反射，用 `Q_GADGET`（值类型）或 `Q_OBJECT`（声明为 `T*`，堆上创建）：

```cpp
class TodoResponse : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int id MEMBER id)
    Q_PROPERTY(QString title MEMBER title)

public:
    using QObject::QObject;

    int id = 0;
    QString title;
};
```

属性名要和 JSON 的键一致，反序列化是按属性名去 JSON 里找的。响应不关心时写 `void`，会跳过反序列化。

顶层响应必须是 JSON 对象，不能直接写 `QList<T>`；数组要包一层，作为对象的属性（`Q_PROPERTY(QList<Item> items ...)`）。Qt5 下自定义 Gadget 的列表还需要注册一次转换器：

```cpp
QMetaType::registerConverter<QVariantList, QList<Item>>();
```

## 配置客户端

```cpp
auto client = NetworkClientBuilder()
    .baseUrl(QStringLiteral("https://api.example.com/"))
    .totalTimeout(std::chrono::seconds(10))
    .addInterceptor(std::make_shared<RetryInterceptor>())
    .followRedirects(QNetworkRequest::NoLessSafeRedirectPolicy, 3)
    .build();
```

`build()` 之后配置项是私有的，外部改不动。单个请求要覆盖超时：

```cpp
auto request = GetTodo::make(1);
request.withTimeout(2000);   // 毫秒；withNoTimeout() 表示不限时
```

## 拦截器

洋葱模型：请求由外向内穿过每层拦截器，响应沿原路返回。拿到 `next` 之后可以透传、短路、后置处理，或者多次调用来重试。

```cpp
class AuthInterceptor : public HttpInterceptor
{
public:
    QPromise<QByteArray> intercept(const QNetworkRequest& req, NextHandler next) override
    {
        QNetworkRequest signed_req = req;
        signed_req.setRawHeader("Authorization", "Bearer " + token_);
        return next();
    }
};
```

`addInterceptor` 的顺序就是执行顺序：`[Log, Auth, Retry]` 会按 Log → Auth → Retry → 实际请求 的次序进入。

内置的 `RetryInterceptor` 用黑名单策略——默认重试，只排除重试无意义的错误（4xx、SSL 握手失败等），429 和 5xx 都会重试：

```cpp
RetryInterceptor::Policy policy;
policy.maxRetries = 3;
policy.delay = std::chrono::milliseconds(500);
policy.shouldRetry = [](const NetworkClientError& e) { return e.isTimeout(); };
```

## 错误处理

错误分五类，用 `std::variant` 承载各自的细节，不需要 dynamic_cast：

```cpp
client->sendRequest(GetTodo::make(1))
    .then([](TodoResponse* todo) { /* ... */ })
    .fail([](const NetworkException& ex) {
        const auto& error = ex.error();

        if (error.isTimeout())        { /* 超时 */ }
        if (error.isUnauthorized())   { /* 401，去刷新 token */ }
        if (auto http = error.asHttp())      { qDebug() << http->status << http->rawBody; }
        if (auto net = error.asNetwork())    { qDebug() << net->code; }
        if (auto logic = error.asLogic())    { qDebug() << logic->serverCode; }

        qDebug() << error.message();
    });
```

| 分类 | 含义 |
| --- | --- |
| Timeout | 超时 |
| Network | 传输层失败（连不上、DNS、SSL） |
| Http | 服务端返回了非 2xx |
| Serialization | 响应体解析不了 |
| Logic | 业务错误码 |

## 动态请求

URL 或参数编译期定不下来时（调试、一次性请求、URL 含用户输入）用 `HttpSpec`，走的是和编译期版本完全相同的链路：

```cpp
auto spec = HttpSpec::post(QStringLiteral("https://api.example.com/search"))
    .header("X-Trace-Id", traceId)
    .query(QStringLiteral("lang"), QStringLiteral("zh"))
    .bodyJson(payload)
    .timeout(std::chrono::seconds(5));

client->sendSpec(spec).then([](const QByteArray& raw) { /* ... */ });
client->sendSpec<SearchResult>(spec).then([](const SearchResult& r) { /* ... */ });
```

`bodyModel()` / `bodyForm()` / `bodyRaw()` / `filePart()` / `formPart()` 分别对应模型、表单、原始字节和 multipart。

## Service 层

按业务模块继承 `NetworkService`，共用同一个 client，并统一注入通用 Header：

```cpp
class TodoService : public NetworkService
{
public:
    using NetworkService::NetworkService;

    QHash<QByteArray, QByteArray> commonHeaders() const override
    {
        return { { "Accept", "application/json" } };
    }

    void printTodo(int id)
    {
        httpCall<GetTodo>(GetTodo::make(id),
            [](TodoResponse* todo) { qDebug() << todo->title; todo->deleteLater(); },
            [](const NetworkClientError& e) { qWarning() << e.message(); });
    }

    QPromise<TodoResponse*> fetchTodo(int id)
    {
        return httpPromise<GetTodo>(GetTodo::make(id));
    }
};
```

三种风格按场景选：`httpCall` 回调（最省事）、`httpPromise` 链式异步、`httpSpecPromise` 动态请求。

完整可运行的例子在 `examples/main.cpp`。

## 构建

```bash
cmake --preset default
cmake --build out/build --config Debug
ctest --test-dir out/build -C Debug --output-on-failure
```

构建前设置环境变量 `QTDIR` 指向 Qt 5.15 安装目录，例如 `E:/QT/5.15.2/MSVC2019_64`。

部分测试会真实访问 httpbin.org，网络不通时会自动跳过。也可以指向本地实例：

```bash
docker run -p 8080:80 kennethreitz/httpbin
set HTTPBIN_URL=http://127.0.0.1:8080/
```

## 已实现

- [x] 编译期字符串与请求声明
- [x] JSON 自动序列化（Qt 元对象反射）
- [x] 请求转换与 multipart
- [x] 五级错误分类
- [x] 生命周期追踪与拦截器链
- [x] Promise 异步客户端与 Builder
- [x] HttpSpec 动态请求
- [x] Service 层与示例

## License

MIT
