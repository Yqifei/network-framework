#pragma once

#include <QtGlobal>

#if defined(NETWORK_STATIC)
#  define NETWORK_EXPORT
#elif defined(NETWORK_LIBRARY)
#  define NETWORK_EXPORT Q_DECL_EXPORT
#else
#  define NETWORK_EXPORT Q_DECL_IMPORT
#endif
