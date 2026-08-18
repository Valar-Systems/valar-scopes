// GameFormat — see the header. Integer arithmetic end to end.

#include "GameFormat.h"

namespace game {
namespace {

constexpr uint64_t kUsPerSecond = 1000000u;

uint32_t Pow10(uint8_t d) {
  uint32_t v = 1;
  while (d > 0) {
    v *= 10u;
    d -= 1;
  }
  return v;
}

/// Decimal digits of `v`, zero-padded to `width`, written backwards into `buf`.
/// Returns the number of characters written, or 0 if it would not fit.
size_t WriteDigits(uint64_t v, uint8_t width, char* buf, size_t cap) {
  char tmp[24];
  size_t len = 0;
  do {
    tmp[len] = (char)('0' + (int)(v % 10u));
    len += 1;
    v /= 10u;
  } while (v > 0 && len < sizeof(tmp));
  while (len < width && len < sizeof(tmp)) {
    tmp[len] = '0';
    len += 1;
  }
  if (len > cap) return 0;
  for (size_t i = 0; i < len; i += 1) buf[i] = tmp[len - 1 - i];
  return len;
}

}  // namespace

Sense DeviationSense(int64_t deviation_us) {
  if (deviation_us < 0) return Sense::Early;
  if (deviation_us > 0) return Sense::Late;
  return Sense::Exact;
}

uint8_t BucketDecimals(uint32_t bucket_us) {
  if (bucket_us == 0) return 0;
  // The smallest number of decimals whose quantum divides the bucket exactly.
  // 200000 us -> 100000 divides it -> 1 decimal, which is what `String(0.2)`
  // gives the server. 50000 -> 10000 -> 2, matching `String(0.05)`.
  for (uint8_t d = 0; d < 6; d += 1) {
    const uint32_t quantum = (uint32_t)(kUsPerSecond / Pow10(d));
    if (bucket_us % quantum == 0) return d;
  }
  return 6;
}

bool FormatDeviation(int64_t deviation_us, uint32_t bucket_us, char* out, size_t n) {
  if (out == NULL || n == 0) return false;
  out[0] = '\0';
  if (bucket_us == 0) return false;

  // Magnitude taken in unsigned arithmetic so INT64_MIN has nowhere to overflow
  // to. `-INT64_MIN` is undefined; this is not.
  const uint64_t abs_us = deviation_us < 0
                              ? (uint64_t)(-(deviation_us + 1)) + 1u
                              : (uint64_t)deviation_us;

  const uint64_t buckets = (abs_us + (uint64_t)bucket_us - 1u) / (uint64_t)bucket_us;
  // Refuse rather than wrap. This guards the ARITHMETIC only -- the server's
  // own |deviation| bound is the server's business and importing it here would
  // be a second copy of a policy the device does not own.
  if (bucket_us != 0 && buckets > UINT64_MAX / (uint64_t)bucket_us) return false;

  const uint64_t value_us = buckets * (uint64_t)bucket_us;
  const uint8_t decimals = BucketDecimals(bucket_us);
  // Exact by construction: `decimals` was chosen as the smallest count whose
  // quantum divides the bucket, and value_us is a whole number of buckets. So
  // there is nothing to round here, and nothing that could round differently
  // from the server.
  const uint64_t quantum = kUsPerSecond / (uint64_t)Pow10(decimals);
  const uint64_t whole = value_us / kUsPerSecond;
  const uint64_t frac = (value_us % kUsPerSecond) / quantum;

  size_t at = 0;
  const size_t wrote = WriteDigits(whole, 1, out + at, n - at - 1);
  if (wrote == 0) {
    out[0] = '\0';
    return false;
  }
  at += wrote;
  if (decimals > 0) {
    if (at + 1 >= n) {
      out[0] = '\0';
      return false;
    }
    out[at] = '.';
    at += 1;
    const size_t f = WriteDigits(frac, decimals, out + at, n - at - 1);
    if (f == 0) {
      out[0] = '\0';
      return false;
    }
    at += f;
  }
  out[at] = '\0';
  return true;
}

}  // namespace game
