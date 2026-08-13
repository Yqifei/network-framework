#pragma once

#define NETCORE_VERSION_MAJOR 0
#define NETCORE_VERSION_MINOR 1
#define NETCORE_VERSION_PATCH 0
#define NETCORE_VERSION_STR "0.1.0"

namespace NetCore {

constexpr int versionMajor() { return NETCORE_VERSION_MAJOR; }
constexpr int versionMinor() { return NETCORE_VERSION_MINOR; }
constexpr int versionPatch() { return NETCORE_VERSION_PATCH; }
const char *versionString();

} // namespace NetCore
