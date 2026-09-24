// Host tests for the game client's pure half (src/game/GameProtocol.h) and the
// key-window sample cadence (src/game/TouchCadence.h).
//
// The bodies are graded as exact strings: they ARE the contract with
// valar-eam-feed src/routes/game.ts, and a body that parses but says something
// else (a rounded deviation, a T in local time) is the failure to catch.

#include "../../src/game/GameProtocol.h"
#include "../../src/game/TouchCadence.h"

#include <cstdio>
#include <cstring>

using namespace game;

static int g_failures = 0;
static int g_checks = 0;
static const char* g_case = "";

#define CHECK(cond, msg)                                                        \
  do {                                                                          \
    g_checks += 1;                                                              \
    if (!(cond)) {                                                              \
      g_failures += 1;                                                          \
      std::printf("  FAIL %s\n       %s\n       at %s:%d\n", g_case, msg,       \
                  __FILE__, __LINE__);                                          \
    }                                                                           \
  } while (0)

#define CASE(name)                                                              \
  g_case = name;                                                                \
  std::printf("  ---- %s\n", name);

static void Commit() {
  char b[256];
  CASE("commit: an execution carries its T as ISO-8601 UTC, on the whole minute");
  // 2026-09-23T15:03:00.000Z
  const int64_t t = 1790175780000LL;
  size_t n = BuildCommitBody("eam-7ca8cd24", MsgClass::Execution, 7, t, b, sizeof(b));
  CHECK(n == std::strlen(b), "the returned length is not the string's");
  CHECK(std::strcmp(b, "{\"msg_id\":\"eam-7ca8cd24\",\"class\":\"execution\",\"config_epoch\":7,"
                       "\"t_at\":\"2026-09-23T15:03:00.000Z\"}") == 0, b);

  CASE("commit: NAM and FDM send t_at null (the derivation gives them no T)");
  BuildCommitBody("eam-1", MsgClass::Nam, 1, t, b, sizeof(b));
  CHECK(std::strcmp(b, "{\"msg_id\":\"eam-1\",\"class\":\"nam\",\"config_epoch\":1,\"t_at\":null}") == 0, b);
  BuildCommitBody("eam-1", MsgClass::Fdm, 1, 0, b, sizeof(b));
  CHECK(std::strcmp(b, "{\"msg_id\":\"eam-1\",\"class\":\"fdm\",\"config_epoch\":1,\"t_at\":null}") == 0, b);

  CASE("commit: refuses what the server would 400, rather than sending it");
  CHECK(BuildCommitBody("eam-1", MsgClass::Execution, 1, 0, b, sizeof(b)) == 0, "an execution with no T was built");
  CHECK(b[0] == '\0', "a refused body left text behind");
  CHECK(BuildCommitBody("eam\"1", MsgClass::Nam, 1, 0, b, sizeof(b)) == 0, "a quote in msg_id was built");
  CHECK(BuildCommitBody("", MsgClass::Nam, 1, 0, b, sizeof(b)) == 0, "an empty msg_id was built");
  CHECK(BuildCommitBody(nullptr, MsgClass::Nam, 1, 0, b, sizeof(b)) == 0, "a null msg_id was built");
  char small[20];
  CHECK(BuildCommitBody("eam-7ca8cd24", MsgClass::Nam, 1, 0, small, sizeof(small)) == 0, "an overflow was built");
  CHECK(small[0] == '\0', "an overflowed body left text behind");
}

static void Execute() {
  char b[96];
  CASE("execute: deviation in whole ms, rounded the way the server's Math.round does");
  CHECK(DeviationMsForServer(0) == 0, "0 us");
  CHECK(DeviationMsForServer(1499) == 1, "1.499 ms -> 1");
  CHECK(DeviationMsForServer(1500) == 2, "1.5 ms -> 2 (half up)");
  CHECK(DeviationMsForServer(-1500) == -1, "-1.5 ms -> -1 (half UP, toward +inf, as JS)");
  CHECK(DeviationMsForServer(-1501) == -2, "-1.501 ms -> -2");
  CHECK(DeviationMsForServer(-499) == 0, "-0.499 ms -> 0");
  CHECK(DeviationMsForServer(-500) == 0, "-0.5 ms -> 0 (JS: Math.round(-0.5) is -0)");
  CHECK(DeviationMsForServer(-1000000) == -1000, "-1 s");
  CHECK(DeviationMsForServer(999500) == 1000, "0.9995 s -> 1000");

  CASE("execute: the body");
  BuildExecuteBody(-40400, true, b, sizeof(b));
  CHECK(std::strcmp(b, "{\"deviation_ms\":-40,\"enable_ok\":true}") == 0, b);
  BuildExecuteBody(1500, false, b, sizeof(b));
  CHECK(std::strcmp(b, "{\"deviation_ms\":2,\"enable_ok\":false}") == 0, b);
  char small[10];
  CHECK(BuildExecuteBody(0, true, small, sizeof(small)) == 0, "an overflow was built");
}

static void Replies() {
  CASE("replies: 2xx is Ok; 409 stale_config refetches /config");
  CHECK(ClassifyReply(200, nullptr) == Reply::Ok, "200");
  CHECK(ClassifyReply(409, "stale_config") == Reply::RefetchConfig, "stale_config");
  CASE("replies: other 4xx are verdicts; no round trip, 5xx and 429 are not");
  CHECK(ClassifyReply(409, "already_committed") == Reply::Refused, "already_committed");
  CHECK(ClassifyReply(409, "too_late") == Reply::Refused, "too_late");
  CHECK(ClassifyReply(400, "past_ack_cutoff") == Reply::Refused, "past_ack_cutoff");
  CHECK(ClassifyReply(403, "no_seat") == Reply::Refused, "no_seat");
  CHECK(ClassifyReply(401, "unauthorized") == Reply::Refused, "unauthorized");
  CHECK(ClassifyReply(0, nullptr) == Reply::Retry, "no round trip");
  CHECK(ClassifyReply(-1, nullptr) == Reply::Retry, "a transport error");
  CHECK(ClassifyReply(503, "game_hold") == Reply::Retry, "503");
  CHECK(ClassifyReply(429, nullptr) == Reply::Retry, "429");
}

static void Outcomes() {
  CASE("outcomes: PENDING keeps polling");
  CHECK(!ResolveOutcome("PENDING", false, nullptr).resolved, "PENDING resolved");
  CASE("outcomes: LAUNCHED is Seconded with a seconder, Launched without (dead-man)");
  Resolution r = ResolveOutcome("LAUNCHED", true, nullptr);
  CHECK(r.resolved && r.outcome == VoteOutcome::Seconded, "seconded launch");
  r = ResolveOutcome("LAUNCHED", false, nullptr);
  CHECK(r.resolved && r.outcome == VoteOutcome::Launched, "dead-man launch");
  CASE("outcomes: INHIBITED carries the inhibitor's reason, or says inhibited");
  r = ResolveOutcome("INHIBITED", false, "wrong target");
  CHECK(r.resolved && r.outcome == VoteOutcome::Inhibited && std::strcmp(r.reason, "wrong target") == 0, "reason");
  r = ResolveOutcome("INHIBITED", false, "");
  CHECK(r.reason != nullptr && std::strcmp(r.reason, "inhibited") == 0, "empty reason");
  CASE("outcomes: FAILED and PREEMPTED end as Failed; ABORTED is Aborted, never Failed");
  r = ResolveOutcome("FAILED", false, nullptr);
  CHECK(r.resolved && r.outcome == VoteOutcome::Failed, "FAILED");
  r = ResolveOutcome("ABORTED", false, nullptr);
  CHECK(r.resolved && r.outcome == VoteOutcome::Aborted && std::strcmp(r.reason, "aborted") == 0, "ABORTED");
  r = ResolveOutcome("PREEMPTED", false, nullptr);
  CHECK(r.resolved && r.outcome == VoteOutcome::Failed && std::strcmp(r.reason, "preempted") == 0, "PREEMPTED");
  CASE("outcomes: an unknown word resolves nothing (never guesses a launch)");
  CHECK(!ResolveOutcome("LAUNCHED_LATER", true, nullptr).resolved, "an unknown word resolved");
  CHECK(!ResolveOutcome(nullptr, true, nullptr).resolved, "null resolved");
}

static void Cadence() {
  CASE("cadence: intervals between samples; the first sample is only an origin");
  TouchCadence c;
  c.Add(1000);
  CHECK(c.Count() == 0, "the origin counted as an interval");
  c.Add(11000);
  c.Add(21000);
  c.Add(33000);
  CHECK(c.Count() == 3, "three intervals");
  CHECK(c.MinUs() == 10000 && c.MaxUs() == 12000, "min/max");
  CHECK(c.MeanUs() == 10666, "mean");
  CHECK(c.PercentileUs(950) == 12000, "p95 is capped at the max, not the bin edge above it");
  CHECK(c.PercentileUs(500) == 10250, "p50 is the upper edge of the 10 ms bin");

  CASE("cadence: Break() starts a new origin; the gap between touches is not an interval");
  c.Break();
  c.Add(900000);
  CHECK(c.Count() == 3, "the gap counted");
  c.Add(910000);
  CHECK(c.Count() == 4 && c.MaxUs() == 12000, "the run after the break");

  CASE("cadence: time going backwards is not an interval");
  c.Add(5);
  CHECK(c.Count() == 4, "a backwards step counted");

  CASE("cadence: a slow sample lands in the last bin and still reports its true max");
  TouchCadence s;
  s.Add(0);
  s.Add(200000);
  CHECK(s.MaxUs() == 200000 && s.PercentileUs(950) == 200000, "a 200 ms gap");
  CHECK(TouchCadence().MeanUs() == 0 && TouchCadence().PercentileUs(950) == 0, "empty");
}

int main() {
  std::printf("GameProtocol + TouchCadence\n");
  Commit();
  Execute();
  Replies();
  Outcomes();
  Cadence();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
