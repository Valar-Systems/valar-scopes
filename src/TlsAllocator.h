#pragma once

#include <stddef.h>   // size_t
#include <stdint.h>   // uint32_t

/**
 * Where mbedTLS gets its memory.
 *
 * THE DEFECT THIS EXISTS FOR (issue #245). The prebuilt Arduino libs ship
 * CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y, so every mbedTLS allocation is pinned to
 * INTERNAL RAM. The TLS input record buffer is one contiguous 16 KB block
 * (CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN is not set, so both buffers are the
 * 16384 default); with struct overhead that is the 16,717 B measured in
 * HeapHealth.h. Internal CONTIGUOUS heap decays from ~44,000 B at boot to
 * ~12,276 B after a day of ordinary uptime -- free memory stays fine, it is
 * fragmentation -- and from that moment no TLS handshake can ever be made again.
 *
 * The device does not look broken when this happens. Positions are ungated, so
 * the radar keeps drawing a full sky at a healthy frame rate; only the two
 * enrichment-gated paths die, which the owner sees as every aircraft card losing
 * its type, operator and photograph, permanently, until someone power-cycles a
 * device they have no reason to suspect. Observed on COM119 2026-08-24 with
 * ball=0/48, tlsOk=0, enrichReqs=0.
 *
 * THE FIX. Every SKU is an S3 with 8 MB of PSRAM and roughly 8.2 MB of it free.
 * MBEDTLS_PLATFORM_MEMORY is defined in the platform's esp_config.h, so the
 * allocator can be replaced AT RUNTIME -- which matters, because the Arduino
 * mbedTLS is prebuilt and its sdkconfig is not ours to change. ESP-IDF ships
 * CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC as a supported build-time equivalent; this
 * is the same idea installed from application code.
 *
 * A THRESHOLD, NOT A BLANKET REDIRECT. Only allocations at or above
 * TLS_PSRAM_THRESHOLD go to PSRAM -- in practice the two record buffers, which
 * are the only ones big enough to fail. Everything smaller stays internal, where
 * it is cheap, cache-free, and is not what fragments. This also bounds the risk:
 * the S3 crypto accelerator uses DMA and PSRAM carries alignment constraints
 * there, so the fewer buffers that move, the smaller the surface. If a DMA path
 * cannot take a PSRAM buffer the handshake fails immediately and loudly, which
 * is the good kind of failure and is why this is bench-proved rather than
 * reasoned into place.
 */
namespace tlsalloc {

/// At or above this many bytes, an mbedTLS allocation is served from PSRAM.
/// Well under the 16 KB record buffers and well above the routine small fry.
constexpr size_t TLS_PSRAM_THRESHOLD = 4096;

/// Install the allocator. MUST be called before any TLS context exists --
/// mbedTLS frees with whatever is installed at free() time, so switching after
/// a context is live would hand an internal pointer to the PSRAM path.
/// No-op (and says so) when built without BLIPSCOPE_TLS_PSRAM.
void Install();

/// How many allocations each region has served, for the [health] line. Counting
/// is the point: "the fix is installed" and "the fix is being used" are
/// different claims, and only the second one is evidence.
uint32_t PsramAllocs();
uint32_t InternalAllocs();
uint32_t PsramFallbacks();

} // namespace tlsalloc
