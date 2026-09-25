// TouchCadence — how often the key window's touch samples actually arrived.
//
// ===========================================================================
// THE KEY TURN IS SCORED AT A SAMPLE'S TIMESTAMP, so the sample interval is the
// resolution of the measurement. Before the key-window sampler, touch was read
// once per render pass -- ~48 ms on the S3-128 (the [health] frame avg) -- and a
// deviation could not be finer than that however the server bucketed it.
//
// This accumulates the intervals the sampler achieved and reports them, so the
// number in the serial log is measured, not assumed. Pure (<stdint.h> and
// <stddef.h> only), graded by test/host/test_touch_cadence.cpp.
// ===========================================================================

#ifndef BLIPSCOPE_GAME_TOUCHCADENCE_H
#define BLIPSCOPE_GAME_TOUCHCADENCE_H

#include <stddef.h>
#include <stdint.h>

namespace game {

class TouchCadence {
 public:
  /// Histogram resolution and span: 250 us bins to 64 ms. Anything slower lands
  /// in the last bin -- a key-window sample 64 ms apart is already a failure the
  /// log needs to show, not resolve.
  static const uint32_t kBinUs = 250;
  static const size_t kBins = 256;

  /// One sample at `t_us`. The first sample of a run only sets the origin.
  void Add(uint64_t t_us) {
    if (have_last_ && t_us >= last_us_) {
      const uint64_t d = t_us - last_us_;
      const uint32_t d32 = d > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(d);
      n_ += 1;
      sum_us_ += d;
      if (n_ == 1 || d32 < min_us_) min_us_ = d32;
      if (d32 > max_us_) max_us_ = d32;
      size_t b = d32 / kBinUs;
      if (b >= kBins) b = kBins - 1;
      bins_[b] += 1;
    }
    last_us_ = t_us;
    have_last_ = true;
  }

  /// End a run (the finger lifted, the window closed): the next Add starts a
  /// new origin, so the gap between two touches is not counted as an interval.
  void Break() { have_last_ = false; }

  void Reset() { *this = TouchCadence(); }

  uint32_t Count() const { return n_; }
  uint32_t MinUs() const { return n_ ? min_us_ : 0; }
  uint32_t MaxUs() const { return max_us_; }
  uint32_t MeanUs() const { return n_ ? static_cast<uint32_t>(sum_us_ / n_) : 0; }

  /// The interval at or below which `permille` of the intervals fell, to the
  /// upper edge of its bin. 950 = p95.
  uint32_t PercentileUs(uint32_t permille) const {
    if (n_ == 0) return 0;
    const uint64_t want = (static_cast<uint64_t>(n_) * permille + 999) / 1000;
    uint64_t seen = 0;
    for (size_t b = 0; b < kBins; b += 1) {
      seen += bins_[b];
      if (seen >= want) {
        // The last bin is open-ended: its only honest upper edge is the max.
        if (b == kBins - 1) return max_us_;
        const uint32_t edge = static_cast<uint32_t>((b + 1) * kBinUs);
        return edge < max_us_ ? edge : max_us_;
      }
    }
    return max_us_;
  }

 private:
  uint64_t last_us_ = 0;
  bool have_last_ = false;
  uint32_t n_ = 0;
  uint64_t sum_us_ = 0;
  uint32_t min_us_ = 0;
  uint32_t max_us_ = 0;
  uint32_t bins_[kBins] = {0};
};

}  // namespace game

#endif  // BLIPSCOPE_GAME_TOUCHCADENCE_H
