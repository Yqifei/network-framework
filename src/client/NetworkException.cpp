#include <client/NetworkException.h>

#include <utility>

namespace NetCore {

NetworkException::NetworkException(NetworkClientError error)
	: error_(std::move(error))
	, what_(error_.message().toStdString())
{
}

} // namespace NetCore
