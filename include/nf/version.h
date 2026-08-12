#pragma once

#define NF_VERSION_MAJOR 0
#define NF_VERSION_MINOR 1
#define NF_VERSION_PATCH 0
#define NF_VERSION_STR "0.1.0"

namespace nf {

constexpr int versionMajor() { return NF_VERSION_MAJOR; }
constexpr int versionMinor() { return NF_VERSION_MINOR; }
constexpr int versionPatch() { return NF_VERSION_PATCH; }
const char *versionString();

} // namespace nf
