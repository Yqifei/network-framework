# network-framework

基于 Qt 的声明式 HTTP 客户端框架：编译期请求声明、JSON 自动序列化、Promise 异步、拦截器链、Builder 客户端。

## 技术栈

- C++17
- Qt 5.15+
- CMake 3.16+
- [QtPromise](https://github.com/simonbrunel/qtpromise) v0.7.0（内置于 third_party/）

## 构建

```bash
cmake --preset default
cmake --build out/build --config Debug
ctest --test-dir out/build -C Debug --output-on-failure
```

构建前设置环境变量 `QTDIR` 指向 Qt 5.15 安装目录，例如 `E:/QT/5.15.2/MSVC2019_64`。

## 规划

- [ ] JSON 自动序列化
- [ ] 编译期请求声明
- [ ] Promise 异步请求
- [ ] 拦截器链
- [ ] HttpClient Builder
- [ ] 示例与文档

## License

MIT
