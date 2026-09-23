// GENERATED FILE -- DO NOT EDIT.
//
// valar-eam-feed  scripts/emit-derivation-fixture.ts
//   spec          docs/game-derivation.md
//   epoch         1
//
// Every expected value below is the SERVER's own deriveMessage() output -- the
// function POST /votes re-derives with and rejects a mismatch against. Grade
// the port with whole-value equality; never loosen a comparison to pass.

#ifndef VALAR_TEST_DERIVATION_FIXTURE_H
#define VALAR_TEST_DERIVATION_FIXTURE_H

#include <stddef.h>
#include <stdint.h>

namespace derivation_fixture {

enum class Class : uint8_t { kNam, kFdm, kExecution };
enum class Tier : uint8_t { kNone, kNormal, kSnap };

/// The parameters, as /config serves them: ORDERED, integer ppm.
struct Weight { Class cls; uint32_t ppm; };
struct TierParam { Tier tier; uint32_t ppm; uint32_t min_s; uint32_t max_s; };
constexpr uint32_t kEpoch = 1;
constexpr Weight kWeights[] = {
    { Class::kNam, 830000 },
    { Class::kFdm, 120000 },
    { Class::kExecution, 50000 },
};
constexpr TierParam kTiers[] = {
    { Tier::kNormal, 900000, 300, 900 },
    { Tier::kSnap, 100000, 90, 150 },
};

struct Case {
    const char* name;
    const char* msg_id;     // UTF-8
    const char* heard_at;   // exactly as served
    uint32_t class_u32, tier_u32, offset_u32;
    uint32_t class_ppm, tier_ppm;
    Class cls;
    Tier tier;              // kNone unless execution
    int32_t offset_s;       // -1 unless execution
    int64_t t_at_ms;        // Unix ms; -1 unless execution
    const char* t_at;       // ISO-8601 as the server renders it; "" unless execution
};

constexpr Case kCases[] = {
    // a msg_id in each class
    { "class-nam", "eam-00000000", "2026-09-22T14:00:00.000Z", 897477005u, 137118417u, 1464039877u, 208960u, 31925u, Class::kNam, Tier::kNone, -1, -1LL, "" },
    // a msg_id in each class
    { "class-fdm", "eam-00000018", "2026-09-22T14:00:00.000Z", 3631915437u, 540330381u, 488934687u, 845621u, 125805u, Class::kFdm, Tier::kNone, -1, -1LL, "" },
    // a msg_id in each class
    { "class-execution", "eam-00000007", "2026-09-22T14:00:00.000Z", 4087624560u, 3173629240u, 2248379884u, 951724u, 738918u, Class::kExecution, Tier::kNormal, 784, 1790086440000LL, "2026-09-22T14:14:00.000Z" },
    // u_ppm exactly on a cumulative boundary falls into the NEXT bucket
    { "class-boundary-830000", "eam-000b7419", "2026-09-22T14:00:00.000Z", 3564826304u, 2632672042u, 949350957u, 830000u, 612966u, Class::kFdm, Tier::kNone, -1, -1LL, "" },
    // u_ppm exactly on a cumulative boundary falls into the NEXT bucket
    { "class-boundary-950000", "eam-002d46a3", "2026-09-22T14:00:00.000Z", 4080219778u, 894979739u, 1173521277u, 950000u, 208378u, Class::kExecution, Tier::kNormal, 777, 1790086380000LL, "2026-09-22T14:13:00.000Z" },
    // u_ppm exactly on a cumulative boundary falls into the NEXT bucket
    { "tier-boundary-900000", "eam-000792ed", "2026-09-22T14:00:00.000Z", 4145166907u, 3865473508u, 2840313441u, 965121u, 900000u, Class::kExecution, Tier::kSnap, 111, 1790085720000LL, "2026-09-22T14:02:00.000Z" },
    // T at minS for each tier
    { "tier-normal-minS", "eam-000012bb", "2026-09-22T14:00:00.000Z", 4132476559u, 3185075061u, 3640279200u, 962167u, 741583u, Class::kExecution, Tier::kNormal, 300, 1790085900000LL, "2026-09-22T14:05:00.000Z" },
    // T at maxS-1 for each tier
    { "tier-normal-maxS-1", "eam-00000c0a", "2026-09-22T14:00:00.000Z", 4256530617u, 1215608654u, 3548942399u, 991050u, 283030u, Class::kExecution, Tier::kNormal, 899, 1790086500000LL, "2026-09-22T14:15:00.000Z" },
    // T at minS for each tier
    { "tier-snap-minS", "eam-000014fe", "2026-09-22T14:00:00.000Z", 4287016177u, 4022898060u, 3377488020u, 998148u, 936653u, Class::kExecution, Tier::kSnap, 90, 1790085720000LL, "2026-09-22T14:02:00.000Z" },
    // T at maxS-1 for each tier
    { "tier-snap-maxS-1", "eam-00000af7", "2026-09-22T14:00:00.000Z", 4148091908u, 4097619850u, 1395750239u, 965802u, 954051u, Class::kExecution, Tier::kSnap, 149, 1790085780000LL, "2026-09-22T14:03:00.000Z" },
    // heard_at + offset already on a whole minute: ceil must not add a minute
    { "ceil-exact-minute", "eam-00000007", "2026-09-22T14:06:56.000Z", 4087624560u, 3173629240u, 2248379884u, 951724u, 738918u, Class::kExecution, Tier::kNormal, 784, 1790086800000LL, "2026-09-22T14:20:00.000Z" },
    // T 59.5 s past a minute boundary ceils to the next minute
    { "ceil-59.5s-past", "eam-00000007", "2026-09-22T14:07:55.500Z", 4087624560u, 3173629240u, 2248379884u, 951724u, 738918u, Class::kExecution, Tier::kNormal, 784, 1790086860000LL, "2026-09-22T14:21:00.000Z" },
    // T 0.5 s past a minute boundary ceils to the next minute
    { "ceil-0.5s-past", "eam-00000007", "2026-09-22T14:06:56.500Z", 4087624560u, 3173629240u, 2248379884u, 951724u, 738918u, Class::kExecution, Tier::kNormal, 784, 1790086860000LL, "2026-09-22T14:21:00.000Z" },
    // SHA-256 over the UTF-8 bytes of msg_id
    { "utf8-msg-id", "eam-\xc3""\xa9""\xe2""\x9c""\x93""-utf8", "2026-09-22T14:00:00.000Z", 4066608062u, 3451153175u, 2130526656u, 946830u, 803534u, Class::kFdm, Tier::kNone, -1, -1LL, "" },
};
constexpr size_t kCaseCount = sizeof(kCases) / sizeof(kCases[0]);

}  // namespace derivation_fixture

#endif  // VALAR_TEST_DERIVATION_FIXTURE_H
