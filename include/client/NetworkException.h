#pragma once

#include <exception>
#include <string>

#include <client/NetworkClientError.h>

namespace NetCore {

// NetworkClientError 的异常包装，供 QtPromise .fail() 按类型精确捕获
class NETWORK_EXPORT NetworkException : public std::exception {
public:
	explicit NetworkException(NetworkClientError error);

	const NetworkClientError& error() const noexcept { return error_; }
	const char* what() const noexcept override { return what_.c_str(); }

private:
	NetworkClientError error_;
	std::string what_;	// 持久存储 message，保证 what() 返回值不悬垂
};

} // namespace NetCore
