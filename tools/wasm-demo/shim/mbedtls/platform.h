#pragma once
#include <cstddef>
// Blipscope web demo: there is no mbedTLS; TlsAllocator's hook is accepted and never called.
inline int mbedtls_platform_set_calloc_free(void* (*)(size_t, size_t), void (*)(void*)) { return 0; }
