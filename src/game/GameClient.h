#pragma once

// GameClient — the drill's conversation with the game server (valar-eam-feed
// src/routes/game.ts): the seat claim, the vote commit, the execute, and the poll
// that turns the server's resolution into Event::VoteResolved.
//
// ===========================================================================
// NOTHING HERE IS ON THE RENDER PATH. Every request runs on this client's own
// worker task (core 0), and the loop only ever does non-blocking queue calls: it
// hands a request over and, on a later pass, picks up a parsed result. A drill
// phase never waits on a round trip to draw -- Committed shows AWAITING FLEET and
// keeps rendering while the poll runs behind it.
//
// Its OWN worker, not EamFeedClient's: that worker runs one feed GET at a time,
// several seconds each over TLS, and an execute queued behind it would reach the
// server late (executeVote's arrival bound is the half window + executeSlackS).
// The two still share HttpRequestManager's mutex, so an execute can wait out one
// in-flight feed request; that is the bound, and it is logged.
//
// LOCAL SERVERS ONLY. The base URL is the build flag GAME_API_BASE and nothing
// else, and it must be plain http:// -- a LAN dev server. Production stays
// enrollment-closed, this client carries no production URL, and a build that
// points it at one does not compile (see GameClient.cpp). Without the flag the
// client is inert: no task, no requests, and the drill runs exactly as before.
//
// Every decision about a body or a reply is src/game/GameProtocol.h (pure,
// host-tested); this file does HTTP, JSON, and bookkeeping.
//
// FEATURE_EAM_GAME only.
// ===========================================================================

#if defined(FEATURE_EAM_GAME)

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "HttpRequestManager.h"
#include "Derive.h"
#include "DrillMachine.h"

namespace game {

class GameClient {
public:
    explicit GameClient(HttpRequestManager& httpManager) : http(httpManager) {}

    /// Spawn the worker and load the seat token. Inert (and says so) without
    /// GAME_API_BASE.
    void Begin();
    bool Enabled() const { return enabled; }
    /// The server this client talks to, for EamFeedClient's /config fetch. Empty when inert.
    static const char* ApiBase();

    /// Loop, every pass: apply one finished request, dispatch the next due one.
    void Poll(uint64_t nowUs);

    /// A new drill: forget the last one's vote.
    void StartDrill(const String& msgId, MsgClass cls, int64_t tAtMs);
    /// The player acked (§4 "acking commits you"): POST /votes. Executions only --
    /// the server refuses NAM/FDM commits as not_execution_traffic.
    void Commit(uint32_t configEpoch);
    /// The config epoch moved and the message still derives the same class and T:
    /// retry a commit the server refused as stale_config.
    void RecommitAfterRefetch(uint32_t configEpoch);
    /// The drill's derivation no longer matches after a refetch: end the vote.
    void RefuseStale();
    /// The key turn was measured: POST /votes/:id/execute (after the commit, if
    /// that is still in flight).
    void Execute(int64_t deviationUs, bool enableOk);
    /// The player aborted after the ack (§4: acking commits you). POST
    /// /votes/:id/abort, so the server learns of it now rather than at the deadline.
    /// A commit not yet sent is simply cancelled; one in flight is aborted when its
    /// vote id arrives -- even if the drill has been dismissed by then.
    void Abort();
    /// The drill is over locally (dismissed, withdrawn, a new offer): stop polling.
    void EndDrill();

    /// True once after a 409 stale_config: refetch /config now.
    bool ConsumeConfigRefetch();
    /// True while a commit waits on a refetched config.
    bool AwaitingRefetch() const { return commitState == CommitState::Stale; }
    /// The vote's resolution, once, for Event::VoteResolved. `reason` is valid
    /// until the next call.
    bool TakeResolution(VoteOutcome& outcome, const char*& reason);

private:
    enum class Kind : uint8_t { Claim, Commit, Execute, Status, Abort };
    enum class CommitState : uint8_t { None, Due, InFlight, Done, Stale, Refused };

    struct Request {
        Kind kind;
        String url;
        String body;          // empty -> GET
        String token;
    };
    struct Result {
        Kind kind;
        int status = 0;
        String error;         // the reply's `error`, if any
        String token;         // Claim
        String voteId;        // Commit
        String outcome;       // Execute / Status
        bool seconded = false;
        String inhibitReason;
        uint32_t requestMs = 0;
    };

    HttpRequestManager& http;
    bool enabled = false;
    String base;              // GAME_API_BASE + /api/v1/missileer
    String token;             // the seat token: never logged
    TaskHandle_t task = nullptr;
    QueueHandle_t reqQueue = nullptr;
    QueueHandle_t resQueue = nullptr;
    bool inFlight = false;
    uint64_t claimDueUs = 0;
    uint32_t claimFails = 0;

    // The drill's vote.
    String msgId;
    MsgClass cls = MsgClass::Nam;
    int64_t tAtMs = 0;
    uint32_t epoch = 0;
    CommitState commitState = CommitState::None;
    uint64_t commitDueUs = 0;
    uint32_t commitTries = 0;
    String voteId;
    bool executeDue = false, executeInFlight = false, executed = false;
    int64_t deviationUs = 0;
    bool enableOk = false;
    uint32_t executeTries = 0;
    uint64_t executeDueUs = 0;
    uint64_t statusDueUs = 0;
    bool statusInFlight = false;
    uint32_t drillGen = 0;    // results of an ended drill are discarded
    uint32_t reqGen = 0;

    // An abort to send: the vote id, or the drill generation whose in-flight commit is
    // to be aborted when its id comes back. Survives EndDrill on purpose.
    String abortVoteId;
    uint32_t abortGen = 0xFFFFFFFFu;
    uint32_t abortTries = 0;

    bool resolutionReady = false;
    VoteOutcome resolution = VoteOutcome::None;
    String resolutionReason;
    bool configRefetch = false;

    static void Trampoline(void* arg);
    void RunWorker();
    void Perform(const Request& req, Result& res);
    bool Send(Kind kind, const String& path, const String& body);
    void Apply(const Result& res, uint64_t nowUs);
    void Resolve(VoteOutcome o, const char* reason);
};

}  // namespace game

#endif // FEATURE_EAM_GAME
