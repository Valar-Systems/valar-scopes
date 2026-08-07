/**
 * HeapProbe -- does ESP.getMaxAllocHeap() measure the memory a TLS handshake
 * actually needs?
 *
 * WHY THIS EXISTS. A 54 h soak on the s3-128 (bench-logs/s3-128-2026-08-04-1436)
 * failed its OTA version check twice, 24 h apart, identically:
 *
 *     [health] ALLOC FAILED: 16717 B (caps 0x804) in heap_caps_calloc
 *     (-32512) SSL - Memory allocation failed
 *     [ota] version check: HTTP -1
 *
 * At that moment the health line was reporting largest=31732 -- a figure that
 * had been CONSTANT for all 54 hours. A 16.7 KB request cannot fail while
 * 31.7 KB is free and contiguous, so one of those two numbers is not measuring
 * what it is being read as measuring.
 *
 * The suspicion, from the framework source:
 *
 *     Esp.cpp:172  getMaxAllocHeap() -> heap_caps_get_largest_free_block(
 *                                           MALLOC_CAP_INTERNAL)
 *
 * MALLOC_CAP_INTERNAL is (1<<11) = 0x800. The allocation that failed asked for
 * 0x804 = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT. The gate and the allocator are
 * not asking the same question: INTERNAL alone will happily count memory that
 * cannot serve a byte-addressable buffer.
 *
 * That is a THEORY FROM READING HEADERS and it is not worth acting on until the
 * chip says so. This probe makes the chip say so.
 *
 * WHAT IT DOES. Three parts:
 *
 *   1. Reports both figures at boot, for every mask that matters, plus a region
 *      dump -- so any INTERNAL-but-not-8BIT pool shows up by name and size.
 *   2. Reproduces the exact failing allocation (16,717 B at caps 0x804).
 *   3. THE ONE THAT DECIDES IT: eats 8-bit internal DRAM in 4 KB bites and
 *      prints both numbers after every bite. If getMaxAllocHeap() tracks the
 *      8BIT figure down, it is a fair gate and the soak failure is a timing
 *      artefact. If it PLATEAUS while the 8BIT figure goes to zero, then the
 *      gate is structurally blind to the only pressure that can break a
 *      handshake -- and ENRICH_TLS_HEAP_FLOOR (16000, three call sites in
 *      AircraftManager) is guarding enrichment with a number that cannot move.
 *
 * Build/flash (COM118 IS THE SOAK BOARD -- do not touch it):
 *     pio run -e probe-s3-128-heap -t upload --upload-port COM119
 */

#include <Arduino.h>
#include <esp_heap_caps.h>

namespace {

constexpr uint32_t kOtaAllocBytes = 16717;   // the exact size that failed, twice
constexpr uint32_t kCapsOta       = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;  // 0x804
constexpr uint32_t kEnrichFloor   = 16000;   // ENRICH_TLS_HEAP_FLOOR, mirrored

struct Mask { const char* name; uint32_t caps; };
const Mask kMasks[] = {
    {"INTERNAL          (getMaxAllocHeap)", MALLOC_CAP_INTERNAL},
    {"INTERNAL|8BIT     (what TLS asks)  ", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT},
    {"8BIT                               ", MALLOC_CAP_8BIT},
    {"DEFAULT                            ", MALLOC_CAP_DEFAULT},
    {"INTERNAL|32BIT                     ", MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT},
    {"DMA                                ", MALLOC_CAP_DMA},
    {"SPIRAM                             ", MALLOC_CAP_SPIRAM},
};

void Report(const char* when)
{
    Serial.printf("\n--- %s ---\n", when);
    Serial.println("mask                                   largest       free");
    for (const auto& m : kMasks) {
        Serial.printf("  %s %9u %10u\n", m.name,
                      (unsigned)heap_caps_get_largest_free_block(m.caps),
                      (unsigned)heap_caps_get_free_size(m.caps));
    }
}

/** The whole question in one line, printed after every bite. */
void Line(int step, uint32_t held)
{
    const uint32_t internalOnly = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const uint32_t eightBit     = heap_caps_get_largest_free_block(kCapsOta);
    Serial.printf("PROBE,%d,heldKB,%lu,getMaxAllocHeap,%lu,largest8BIT,%lu,delta,%ld,"
                  "gateSaysOk,%d,tlsWouldFit,%d\n",
                  step, (unsigned long)(held / 1024),
                  (unsigned long)internalOnly, (unsigned long)eightBit,
                  (long)internalOnly - (long)eightBit,
                  internalOnly >= kEnrichFloor ? 1 : 0,
                  eightBit     >= kOtaAllocBytes ? 1 : 0);
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\n\n================ HEAP PROBE ================");
    Serial.printf("MALLOC_CAP_8BIT=0x%03X  MALLOC_CAP_INTERNAL=0x%03X  ota caps=0x%03X\n",
                  MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL, (unsigned)kCapsOta);
    Serial.printf("ENRICH_TLS_HEAP_FLOOR=%lu, the failing OTA alloc was %lu B\n",
                  (unsigned long)kEnrichFloor, (unsigned long)kOtaAllocBytes);

    Report("PART 1: at boot, idle");

    Serial.println("\nregions visible to INTERNAL but NOT to INTERNAL|8BIT:");
    Serial.println("(if this list is non-empty, getMaxAllocHeap can report memory TLS cannot use)");
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);

    // PART 2 -- the exact allocation, unchanged.
    Serial.println("\n--- PART 2: reproduce the failing allocation ---");
    void* p = heap_caps_calloc(1, kOtaAllocBytes, kCapsOta);
    Serial.printf("heap_caps_calloc(1, %lu, 0x%03X) = %s\n",
                  (unsigned long)kOtaAllocBytes, (unsigned)kCapsOta, p ? "OK" : "FAILED");
    if (p) heap_caps_free(p);

    // PART 3 -- the experiment that decides it. Eat 8-bit internal DRAM and see
    // whether the gate's number follows it down or sits still.
    Serial.println("\n--- PART 3: consume 8-bit internal DRAM, watch both numbers ---");
    Serial.println("PROBE,step,heldKB,getMaxAllocHeap,largest8BIT,delta,gateSaysOk,tlsWouldFit");

    // 64 bites was a cap, not an exhaustion -- the first run stopped because it
    // ran out of slots at 256 KB, which proves nothing. Eat until malloc ACTUALLY
    // refuses, then ask the real question below.
    constexpr int kMaxBites = 200;
    constexpr size_t kBite  = 4096;
    static void* held[kMaxBites] = {nullptr};
    int    n = 0;
    uint32_t bytes = 0;

    Line(0, 0);
    while (n < kMaxBites) {
        void* b = heap_caps_malloc(kBite, kCapsOta);
        if (!b) break;
        held[n++] = b;
        bytes += kBite;
        if (n % 8 == 0 || n > 60) Line(n, bytes);   // thin the middle, keep the tail
    }

    Serial.printf("\nmalloc(4096, 0x804) REFUSED after %d bites (%lu KB held)%s\n",
                  n, (unsigned long)(bytes / 1024),
                  n == kMaxBites ? "  <-- hit the slot cap, NOT exhaustion" : "");

    // THE MONEY SHOT. Reproduce the soak exactly: ask for the OTA's 16,717 B while
    // reading the number the gate reads. If getMaxAllocHeap() still reports a
    // comfortable figure and this fails, the metric and the allocator disagree,
    // and every gate keyed to it is decoration.
    const uint32_t gateNow = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    void* ota = heap_caps_calloc(1, kOtaAllocBytes, kCapsOta);
    Serial.printf("\n*** getMaxAllocHeap()=%lu (floor >= ENRICH_TLS_HEAP_FLOOR=%lu? %s)\n",
                  (unsigned long)gateNow, (unsigned long)kEnrichFloor,
                  gateNow >= kEnrichFloor ? "YES, gate says PROCEED" : "no, gate would block");
    Serial.printf("*** the OTA-sized alloc (%lu B, caps 0x804) = %s\n",
                  (unsigned long)kOtaAllocBytes, ota ? "OK" : "FAILED");
    Serial.printf("*** VERDICT: %s\n", (gateNow >= kEnrichFloor && !ota)
                  ? "the gate says PROCEED while the allocation FAILS -- reproduced"
                  : "gate and allocator agree at this point");
    if (ota) heap_caps_free(ota);

    Report("PART 3: with 8-bit internal DRAM exhausted");

    Serial.println("\n>>> THE VERDICT IS THE LAST PROBE LINE ABOVE:");
    Serial.println(">>>   gateSaysOk=1 while tlsWouldFit=0  ==  the gate is BLIND.");
    Serial.println(">>>   both fall together                ==  the gate is FAIR.");

    for (int i = 0; i < n; ++i) heap_caps_free(held[i]);
    Serial.println("\n================ done, freed ================");
}

void loop()
{
    delay(10000);
}
