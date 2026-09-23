// Derive — see the header. The only include is the header itself.

#include "Derive.h"

namespace game {

namespace {

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4). Small, allocation-free, and graded by known-answer
// vectors in the host test before the fixture is trusted to mean anything.
// ---------------------------------------------------------------------------

const uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline uint32_t Rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

void Block(uint32_t h[8], const uint8_t* b) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) {
    w[i] = (static_cast<uint32_t>(b[4 * i]) << 24) | (static_cast<uint32_t>(b[4 * i + 1]) << 16)
         | (static_cast<uint32_t>(b[4 * i + 2]) << 8) | static_cast<uint32_t>(b[4 * i + 3]);
  }
  for (int i = 16; i < 64; i++) {
    const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; i++) {
    const uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t t1 = hh + S1 + ch + kK[i] + w[i];
    const uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
    const uint32_t mj = (a & bb) ^ (a & c) ^ (bb & c);
    const uint32_t t2 = S0 + mj;
    hh = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
  }
  h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

uint32_t ReadBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
       | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

/// u_ppm = (u64(u32) * 1_000_000) >> 32 (ruling). Always < 1_000_000.
uint32_t ToPpm(uint32_t u32) {
  return static_cast<uint32_t>((static_cast<uint64_t>(u32) * 1000000ull) >> 32);
}

// ---------------------------------------------------------------------------
// Civil dates <-> days since 1970-01-01, proleptic Gregorian (H. Hinnant's
// algorithms). Integer-only and exact across the whole range used here.
// ---------------------------------------------------------------------------

int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2 ? 1 : 0;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void CivilFromDays(int64_t z, int64_t* y, unsigned* m, unsigned* d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = static_cast<int64_t>(yoe) + era * 400 + (*m <= 2 ? 1 : 0);
}

bool IsLeap(int64_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

unsigned DaysInMonth(int64_t y, unsigned m) {
  static const unsigned kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && IsLeap(y) ? 29u : kDays[m - 1];
}

/// Exactly `n` ASCII digits at `s`, or false.
bool Digits(const char* s, int n, unsigned* out) {
  unsigned v = 0;
  for (int i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + static_cast<unsigned>(s[i] - '0');
  }
  *out = v;
  return true;
}

/// First entry where u_ppm < cumulative -- STRICT (ruling): a draw exactly on a
/// boundary belongs to the NEXT bucket. -1 if the list does not cover the draw,
/// which ParamsSound() rules out before this is reached.
template <typename T>
int Walk(const T* list, size_t n, uint32_t u_ppm) {
  uint64_t cumulative = 0;
  for (size_t i = 0; i < n; i++) {
    cumulative += list[i].ppm;
    if (u_ppm < cumulative) return static_cast<int>(i);
  }
  return -1;
}

const int64_t kMinuteMs = 60000;

}  // namespace

void Sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  size_t i = 0;
  for (; i + 64 <= len; i += 64) Block(h, data + i);
  uint8_t tail[128] = {0};
  const size_t rem = len - i;
  for (size_t j = 0; j < rem; j++) tail[j] = data[i + j];
  tail[rem] = 0x80;
  const size_t tail_len = rem + 1 + 8 <= 64 ? 64 : 128;
  const uint64_t bits = static_cast<uint64_t>(len) * 8u;
  for (int k = 0; k < 8; k++) tail[tail_len - 1 - k] = static_cast<uint8_t>(bits >> (8 * k));
  Block(h, tail);
  if (tail_len == 128) Block(h, tail + 64);
  for (int k = 0; k < 8; k++) {
    out[4 * k] = static_cast<uint8_t>(h[k] >> 24);
    out[4 * k + 1] = static_cast<uint8_t>(h[k] >> 16);
    out[4 * k + 2] = static_cast<uint8_t>(h[k] >> 8);
    out[4 * k + 3] = static_cast<uint8_t>(h[k]);
  }
}

bool ParseIsoUtcMs(const char* s, int64_t* out_ms) {
  if (s == nullptr) return false;
  unsigned y, mo, d, hh, mi, ss;
  if (!Digits(s, 4, &y) || s[4] != '-' || !Digits(s + 5, 2, &mo) || s[7] != '-'
      || !Digits(s + 8, 2, &d) || s[10] != 'T' || !Digits(s + 11, 2, &hh) || s[13] != ':'
      || !Digits(s + 14, 2, &mi) || s[16] != ':' || !Digits(s + 17, 2, &ss)) {
    return false;
  }
  if (mo < 1 || mo > 12 || d < 1 || d > DaysInMonth(y, mo) || hh > 23 || mi > 59 || ss > 59) return false;
  const char* p = s + 19;
  unsigned ms = 0;
  if (*p == '.') {
    // One to three fractional digits, read as milliseconds. More is refused
    // rather than truncated: the server renders exactly three.
    int n = 0;
    p += 1;
    while (p[n] >= '0' && p[n] <= '9') n += 1;
    if (n < 1 || n > 3) return false;
    Digits(p, n, &ms);
    for (int k = n; k < 3; k++) ms *= 10;
    p += n;
  }
  if (p[0] != 'Z' || p[1] != '\0') return false;
  const int64_t days = DaysFromCivil(y, mo, d);
  *out_ms = ((days * 24 + hh) * 60 + mi) * 60000 + static_cast<int64_t>(ss) * 1000 + ms;
  return true;
}

size_t FormatIsoUtcMs(int64_t ms, char* out, size_t cap) {
  if (cap < 25) return 0;
  int64_t days = ms / 86400000;
  int64_t rem = ms % 86400000;
  if (rem < 0) { rem += 86400000; days -= 1; }
  int64_t y;
  unsigned mo, d;
  CivilFromDays(days, &y, &mo, &d);
  const unsigned hh = static_cast<unsigned>(rem / 3600000);
  const unsigned mi = static_cast<unsigned>((rem / 60000) % 60);
  const unsigned ss = static_cast<unsigned>((rem / 1000) % 60);
  const unsigned mss = static_cast<unsigned>(rem % 1000);
  const unsigned yy = static_cast<unsigned>(y);
  const unsigned fields[] = {yy / 1000 % 10, yy / 100 % 10, yy / 10 % 10, yy % 10};
  size_t n = 0;
  for (unsigned f : fields) out[n++] = static_cast<char>('0' + f);
  auto two = [&](char sep, unsigned v) { out[n++] = sep; out[n++] = static_cast<char>('0' + v / 10); out[n++] = static_cast<char>('0' + v % 10); };
  two('-', mo);
  two('-', d);
  two('T', hh);
  two(':', mi);
  two(':', ss);
  out[n++] = '.';
  out[n++] = static_cast<char>('0' + mss / 100);
  out[n++] = static_cast<char>('0' + mss / 10 % 10);
  out[n++] = static_cast<char>('0' + mss % 10);
  out[n++] = 'Z';
  out[n] = '\0';
  return n;
}

bool ParamsSound(const DeriveParams& p) {
  if (p.n_weights == 0 || p.n_weights > kMaxWeights || p.n_tiers == 0 || p.n_tiers > kMaxTiers) return false;
  uint64_t sum = 0;
  for (size_t i = 0; i < p.n_weights; i++) sum += p.weights[i].ppm;
  if (sum != 1000000u) return false;
  sum = 0;
  for (size_t i = 0; i < p.n_tiers; i++) {
    if (p.tiers[i].max_s <= p.tiers[i].min_s) return false;
    sum += p.tiers[i].ppm;
  }
  return sum == 1000000u;
}

Derivation Derive(const char* msg_id, size_t id_len, const char* heard_at, const DeriveParams& p) {
  Derivation r;
  if (!ParamsSound(p)) {
    r.status = DeriveStatus::BadParams;
    return r;
  }
  // SHA-256 over the UTF-8 bytes of msg_id: no epoch, no salt (ruling).
  uint8_t dg[32];
  Sha256(reinterpret_cast<const uint8_t*>(msg_id), id_len, dg);
  r.class_u32 = ReadBe32(dg);
  r.tier_u32 = ReadBe32(dg + 4);
  r.offset_u32 = ReadBe32(dg + 8);
  r.class_ppm = ToPpm(r.class_u32);
  r.tier_ppm = ToPpm(r.tier_u32);

  r.cls = p.weights[Walk(p.weights, p.n_weights, r.class_ppm)].cls;
  r.status = DeriveStatus::Ok;
  // T FOR EXECUTION ONLY (ruling).
  if (r.cls != MsgClass::Execution) return r;

  // Anchored to the SERVED heard_at, never the device's arrival time (§6, ruling).
  int64_t heard_ms = 0;
  if (!ParseIsoUtcMs(heard_at, &heard_ms)) {
    r.status = DeriveStatus::BadHeardAt;
    return r;
  }
  const TierParam& t = p.tiers[Walk(p.tiers, p.n_tiers, r.tier_ppm)];
  r.tier = t.tier;
  // offset_s = minS + (u_offset mod (maxS - minS)): whole seconds, maxS exclusive.
  r.offset_s = static_cast<int32_t>(t.min_s + r.offset_u32 % (t.max_s - t.min_s));
  // CEIL to the next whole Zulu minute; an instant already on one stays (ruling).
  const int64_t raw = heard_ms + static_cast<int64_t>(r.offset_s) * 1000;
  int64_t q = raw / kMinuteMs;  // truncates toward zero, which is ceil for raw < 0
  if (raw % kMinuteMs > 0) q += 1;
  r.t_at_ms = q * kMinuteMs;
  return r;
}

}  // namespace game
