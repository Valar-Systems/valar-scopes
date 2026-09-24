#include "GameClient.h"

#if defined(FEATURE_EAM_GAME)

#include <ArduinoJson.h>
#include <Preferences.h>

#include "DeviceIdentity.h"
#include "GameProtocol.h"

// ---------------------------------------------------------------------------
// THE BASE URL: a build flag, local only.
//
//   PLATFORMIO_BUILD_FLAGS="-DGAME_API_BASE='\"http://192.168.1.20:3000\"' -DGAME_CAPSULE_ID=111"
//   pio run -e missileer-s3-128
//
// (PlatformIO shlex-splits the variable: the single quotes keep the C string's
// double quotes.)
//
// GAME_ROLE defaults to MCCC. Nothing in platformio.ini sets any of these, so a
// normal build has no game server and the client is inert.
// ---------------------------------------------------------------------------
#if defined(GAME_API_BASE)
namespace {
constexpr bool StartsWith(const char* s, const char* p) {
    return *p == '\0' ? true : (*s == *p && StartsWith(s + 1, p + 1));
}
}  // namespace
// Production is https and enrollment-closed. A plain-http base is a LAN dev server;
// anything else is refused at compile time rather than at the first request.
static_assert(StartsWith(GAME_API_BASE, "http://"),
              "GAME_API_BASE must be a local plain-http dev server (production stays enrollment-closed)");
#if !defined(GAME_CAPSULE_ID)
#error "GAME_API_BASE needs GAME_CAPSULE_ID: the capsule this bench device claims a seat in"
#endif
#if !defined(GAME_ROLE)
#define GAME_ROLE "MCCC"
#endif
#endif

namespace game {

namespace {
constexpr uint64_t kStatusEveryUs = 3000000ull;   // GET /votes/:id while a vote is open
constexpr uint64_t kRetryUs = 2000000ull;
constexpr uint32_t kMaxTries = 5;
constexpr uint64_t kClaimRetryUs = 30000000ull;
}  // namespace

const char* GameClient::ApiBase()
{
#if defined(GAME_API_BASE)
    return GAME_API_BASE;
#else
    return "";
#endif
}

void GameClient::Begin()
{
#if !defined(GAME_API_BASE)
    Serial.println("[game] no GAME_API_BASE: votes are not sent (the drill runs locally)");
    return;
#else
    if (task != nullptr) return;
    String b = GAME_API_BASE;
    while (b.endsWith("/")) b.remove(b.length() - 1);
    base = b + "/api/v1/missileer";

    // The token is bound to the server that issued it: a bench device moved to a
    // different dev server claims afresh rather than presenting a foreign token.
    Preferences p;
    p.begin("game", true);
    if (p.getString("base", "") == base) token = p.getString("tok", "");
    p.end();

    reqQueue = xQueueCreate(1, sizeof(Request*));
    resQueue = xQueueCreate(1, sizeof(Result*));
    xTaskCreatePinnedToCore(Trampoline, "game_http", 8192, this, 1, &task, 0);
    enabled = true;
    Serial.printf("[game] server %s, seat %s\n", base.c_str(), token.isEmpty() ? "to claim" : "held");
#endif
}

void GameClient::Trampoline(void* arg)
{
    static_cast<GameClient*>(arg)->RunWorker();
}

void GameClient::RunWorker()
{
    for (;;) {
        Request* req = nullptr;
        if (xQueueReceive(reqQueue, &req, portMAX_DELAY) != pdTRUE || req == nullptr) continue;
        Result* res = new Result();
        res->kind = req->kind;
        const uint32_t t0 = millis();
        Perform(*req, *res);
        res->requestMs = millis() - t0;
        delete req;
        xQueueSend(resQueue, &res, portMAX_DELAY);
    }
}

void GameClient::Perform(const Request& req, Result& res)
{
    std::vector<std::pair<String, String>> headers;
    if (!req.token.isEmpty()) headers.push_back({"Authorization", "Bearer " + req.token});
    HttpResult r;
    if (req.body.isEmpty()) {
        r = http.Get(req.url, {}, headers);
    } else {
        headers.push_back({"Content-Type", "application/json"});
        r = http.Post(req.url, req.body, headers);
    }
    res.status = r.success ? r.statusCode : 0;
    if (!r.success || r.response.isEmpty()) return;

    JsonDocument doc;
    if (deserializeJson(doc, r.response)) return;
    res.error = doc["error"] | "";
    switch (req.kind) {
        case Kind::Claim:   res.token = doc["token"] | ""; break;
        case Kind::Commit:  res.voteId = doc["vote_id"] | ""; break;
        case Kind::Execute: res.outcome = doc["outcome"] | ""; break;
        case Kind::Status:
            res.outcome = doc["outcome"] | "";
            res.seconded = doc["seconded"] | false;
            res.inhibitReason = doc["inhibit_reason"] | "";
            break;
    }
}

bool GameClient::Send(Kind kind, const String& path, const String& body)
{
    Request* req = new Request{kind, base + path, body, token};
    if (xQueueSend(reqQueue, &req, 0) != pdTRUE) {
        delete req;
        return false;
    }
    inFlight = true;
    reqGen = drillGen;
    return true;
}

void GameClient::StartDrill(const String& id, MsgClass c, int64_t tAt)
{
    EndDrill();
    msgId = id;
    cls = c;
    tAtMs = tAt;
}

void GameClient::Commit(uint32_t configEpoch)
{
    if (!enabled || cls != MsgClass::Execution || commitState != CommitState::None) return;
    epoch = configEpoch;
    commitState = CommitState::Due;
    commitDueUs = 0;
    commitTries = 0;
}

void GameClient::RecommitAfterRefetch(uint32_t configEpoch)
{
    if (commitState != CommitState::Stale) return;
    Serial.printf("[game] recommit %s under epoch %u\n", msgId.c_str(), (unsigned)configEpoch);
    epoch = configEpoch;
    commitState = CommitState::Due;
    commitDueUs = 0;
}

void GameClient::RefuseStale()
{
    if (commitState != CommitState::Stale) return;
    commitState = CommitState::Refused;
    Resolve(VoteOutcome::Failed, "config changed");
}

void GameClient::Execute(int64_t devUs, bool ok)
{
    if (!enabled || executeDue || executed) return;
    if (commitState == CommitState::None) return;  // never committed: nothing to execute
    deviationUs = devUs;
    enableOk = ok;
    executeDue = true;
    executeTries = 0;
    executeDueUs = 0;
}

void GameClient::Abort()
{
    if (!enabled) return;
    switch (commitState) {
        case CommitState::Due:
            commitState = CommitState::None;  // never sent: there is no vote to abort
            return;
        case CommitState::InFlight:
            abortGen = drillGen;               // abort it when the id comes back
            break;
        case CommitState::Done:
            if (!voteId.isEmpty()) abortVoteId = voteId;
            break;
        default:
            return;                            // None/Stale/Refused: nothing on the server
    }
    abortTries = 0;
    executeDue = false;                        // an abort is the last word on this vote
    Serial.printf("[game] abort %s\n", voteId.isEmpty() ? "(on commit)" : voteId.c_str());
}

void GameClient::EndDrill()
{
    drillGen += 1;
    msgId = "";
    commitState = CommitState::None;
    voteId = "";
    executeDue = executeInFlight = executed = false;
    statusInFlight = false;
    resolutionReady = false;
    resolution = VoteOutcome::None;
    resolutionReason = "";
}

bool GameClient::ConsumeConfigRefetch()
{
    const bool r = configRefetch;
    configRefetch = false;
    return r;
}

bool GameClient::TakeResolution(VoteOutcome& outcome, const char*& reason)
{
    if (!resolutionReady) return false;
    resolutionReady = false;
    outcome = resolution;
    reason = resolutionReason.isEmpty() ? nullptr : resolutionReason.c_str();
    return true;
}

void GameClient::Resolve(VoteOutcome o, const char* reason)
{
    resolution = o;
    resolutionReason = reason ? reason : "";
    resolutionReady = true;
    Serial.printf("[game] vote %s resolved: outcome=%d%s%s\n", voteId.isEmpty() ? "-" : voteId.c_str(),
                  (int)o, reason ? " " : "", reason ? reason : "");
}

void GameClient::Poll(uint64_t nowUs)
{
    if (!enabled) return;

    Result* res = nullptr;
    if (xQueueReceive(resQueue, &res, 0) == pdTRUE && res != nullptr) {
        inFlight = false;
        // A result for a drill that has since ended is dropped -- except a claim,
        // which belongs to the device, not the drill.
        if (res->kind == Kind::Commit && reqGen == abortGen && !res->voteId.isEmpty()) {
            // The player aborted while this commit was in flight: abort the vote it made.
            abortVoteId = res->voteId;
            abortGen = 0xFFFFFFFFu;
        }
        if (res->kind == Kind::Claim || res->kind == Kind::Abort || reqGen == drillGen) Apply(*res, nowUs);
        delete res;
    }
    if (inFlight) return;

    // Priority: an abort, the execute (time-bound at the server), the commit, the seat, the poll.
    if (!abortVoteId.isEmpty() && !token.isEmpty()) {
        if (Send(Kind::Abort, "/votes/" + abortVoteId + "/abort", "{}"))
            Serial.printf("[game] abort -> POST /votes/%s/abort\n", abortVoteId.c_str());
        return;
    }
    if (executeDue && !voteId.isEmpty() && nowUs >= executeDueUs) {
        char body[96];
        if (BuildExecuteBody(deviationUs, enableOk, body, sizeof(body)) &&
            Send(Kind::Execute, "/votes/" + voteId + "/execute", body)) {
            executeDue = false;
            executeInFlight = true;
            Serial.printf("[game] execute %s %s\n", voteId.c_str(), body);
        }
        return;
    }
    // A commit needs a seat: with no token it waits, and the claim below goes first.
    if (commitState == CommitState::Due && nowUs >= commitDueUs && !token.isEmpty()) {
        char body[256];
        if (!BuildCommitBody(msgId.c_str(), cls, epoch, tAtMs, body, sizeof(body))) {
            commitState = CommitState::Refused;
            Resolve(VoteOutcome::Failed, "vote not sendable");
            return;
        }
        if (Send(Kind::Commit, "/votes", body)) {
            commitState = CommitState::InFlight;
            Serial.printf("[game] commit %s\n", body);
        }
        return;
    }
    if (token.isEmpty() && nowUs >= claimDueUs) {
#if defined(GAME_API_BASE)
        String body = String("{\"device_id\":\"") + DeviceIdentity::LeaderboardId()
                      + "\",\"capsule_id\":" + String((int)GAME_CAPSULE_ID)
                      + ",\"role\":\"" + GAME_ROLE + "\"}";
        if (Send(Kind::Claim, "/seats/claim", body)) {
            Serial.printf("[game] claiming seat: capsule %d %s\n", (int)GAME_CAPSULE_ID, GAME_ROLE);
        }
#endif
        return;
    }
    if (executed && !voteId.isEmpty() && !resolutionReady && resolution == VoteOutcome::None
        && nowUs >= statusDueUs) {
        if (Send(Kind::Status, "/votes/" + voteId, "")) statusInFlight = true;
    }
}

void GameClient::Apply(const Result& res, uint64_t nowUs)
{
    const Reply reply = ClassifyReply(res.status, res.error.c_str());
    switch (res.kind) {
        case Kind::Claim:
            if (reply == Reply::Ok && !res.token.isEmpty()) {
                token = res.token;
                Preferences p;
                p.begin("game", false);
                p.putString("base", base);
                p.putString("tok", token);
                p.end();
                claimFails = 0;
                Serial.println("[game] seat claimed; token stored");
            } else {
                claimFails += 1;
                claimDueUs = nowUs + kClaimRetryUs * (claimFails < 10 ? claimFails : 10);
                Serial.printf("[game] seat claim failed: HTTP %d %s (retry in %llus)\n", res.status,
                              res.error.c_str(), (unsigned long long)((claimDueUs - nowUs) / 1000000ull));
            }
            return;

        case Kind::Commit:
            Serial.printf("[game] commit -> HTTP %d %s%s (%ums)\n", res.status, res.voteId.c_str(),
                          res.error.c_str(), (unsigned)res.requestMs);
            if (reply == Reply::Ok && !res.voteId.isEmpty()) {
                voteId = res.voteId;
                commitState = CommitState::Done;
                if (executeDue) executeDueUs = 0;  // a key turn that beat the commit goes now
                return;
            }
            if (reply == Reply::RefetchConfig) {
                commitState = CommitState::Stale;
                configRefetch = true;
                return;
            }
            if (res.status == 401) {
                // The server does not know this token (a wiped dev database): claim again.
                token = "";
                commitState = CommitState::Due;
                commitDueUs = nowUs;
                return;
            }
            if (reply == Reply::Retry && ++commitTries < kMaxTries) {
                commitState = CommitState::Due;
                commitDueUs = nowUs + kRetryUs;
                return;
            }
            commitState = CommitState::Refused;
            Resolve(VoteOutcome::Failed, res.error.isEmpty() ? "vote not committed" : res.error.c_str());
            return;

        case Kind::Execute:
            executeInFlight = false;
            Serial.printf("[game] execute -> HTTP %d %s%s (%ums)\n", res.status, res.outcome.c_str(),
                          res.error.c_str(), (unsigned)res.requestMs);
            if (reply == Reply::Ok) {
                executed = true;
                statusDueUs = nowUs + kStatusEveryUs;
                // FAILED comes back at once (outside the window, or enable not ok).
                const Resolution r = ResolveOutcome(res.outcome.c_str(), false, nullptr);
                if (r.resolved) Resolve(r.outcome, r.reason);
                return;
            }
            if (reply == Reply::Retry && ++executeTries < kMaxTries) {
                executeDue = true;
                executeDueUs = nowUs + kRetryUs / 4;
                return;
            }
            Resolve(VoteOutcome::Failed, res.error.isEmpty() ? "execute refused" : res.error.c_str());
            return;

        case Kind::Abort:
            Serial.printf("[game] abort -> HTTP %d %s (%ums)\n", res.status, res.error.c_str(),
                          (unsigned)res.requestMs);
            // Sent, refused as already_resolved, or given up after retries: done either way.
            if (reply == Reply::Retry && ++abortTries < kMaxTries) return;  // keep abortVoteId
            abortVoteId = "";
            return;

        case Kind::Status: {
            statusInFlight = false;
            statusDueUs = nowUs + kStatusEveryUs;
            if (reply != Reply::Ok) return;  // keep polling; the dead-man timer resolves it
            const Resolution r = ResolveOutcome(res.outcome.c_str(), res.seconded, res.inhibitReason.c_str());
            if (r.resolved) Resolve(r.outcome, r.reason);
            return;
        }
    }
}

}  // namespace game

#endif // FEATURE_EAM_GAME
