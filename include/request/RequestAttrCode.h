#pragma once

#include <QNetworkRequest>

namespace NetCore {
	inline constexpr int kRequestBodyAttrCode = QNetworkRequest::User + 1;
	inline constexpr int kRequestTimeoutAttrCode = QNetworkRequest::User + 2;
} // namespace NetCore
