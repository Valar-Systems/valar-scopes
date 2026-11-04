// Reading a device: src/remote/SseStream.h and src/remote/EsphomeModel.h (docs/remote-edition.md section 5; the
// 21 named checks and 9 sabotages were frozen there, in 3d73ff0, before the code existed).
//
// argv[1] is test/fixtures/remote/, argv[2] is platformio.ini's ArduinoJson pin. Built against the ArduinoJson
// under .pio/libdeps, like test_game_config.cpp, and refusing (exit 2, the RIG) any other version. The fixtures
// are TRANSCRIPTIONS of ESPHome 2026.6.5's web server, stored LF; this sends them CRLF and chunked, as it does.
// Headers are included by bare name so test/host/sabotage_remote_stream.py can put planted copies first.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "SseStream.h"
#include "EsphomeModel.h"

using namespace coverpolicy;

static int checks = 0, failures = 0;

static void check(const char* id, bool ok, const char* what)
{
    ++checks;
    std::printf("  %s  %-3s %s\n", ok ? "ok  " : "FAIL", id, what);
    if (!ok) ++failures;
}

typedef std::pair<std::string, std::string> Ev;

static std::string Dir;

static bool Load(const char* name, std::string& out)
{
    std::ifstream f((Dir + "/" + name).c_str(), std::ios::binary);
    if (!f) { std::printf("  RIG: cannot read %s/%s\n", Dir.c_str(), name); return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    out.clear();
    const std::string raw = ss.str();
    for (size_t i = 0; i < raw.size(); ++i) if (raw[i] != '\r') out.push_back(raw[i]);   // LF, whatever git did
    return true;
}

static std::string CRLF(const std::string& lf)
{
    std::string s;
    for (size_t i = 0; i < lf.size(); ++i) { if (lf[i] == '\n') s.push_back('\r'); s.push_back(lf[i]); }
    return s;
}

// The SSE text's events as blocks, each ending in its blank line (CRLF form).
static std::vector<std::string> Blocks(const std::string& crlf)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t e = crlf.find("\r\n\r\n", start);
        if (e == std::string::npos) break;
        out.push_back(crlf.substr(start, e + 4 - start));
        start = e + 4;
    }
    return out;
}

// As web_server_idf.cpp chunks it: one chunk per event, "%08x" size line.
static std::string Chunked(const std::vector<std::string>& blocks, size_t from, size_t to)
{
    std::string s;
    char hdr[16];
    for (size_t i = from; i < to && i < blocks.size(); ++i) {
        std::snprintf(hdr, sizeof(hdr), "%08x\r\n", (unsigned)blocks[i].size());
        s += hdr;
        s += blocks[i];
        s += "\r\n";
    }
    return s;
}

static std::string Decode(sse::Dechunk& d, const std::string& in)
{
    std::string body;
    d.Feed(in.data(), in.size(), [&body](char c) { body.push_back(c); });
    return body;
}

static std::vector<Ev> Frame(sse::Framer& f, const std::string& in)
{
    std::vector<Ev> evs;
    f.Feed(in.data(), in.size(), [&evs](const std::string& t, const std::string& d) { evs.push_back(Ev(t, d)); });
    return evs;
}

static std::vector<Ev> FrameAll(const std::string& in)
{
    sse::Framer f;
    return Frame(f, in);
}

// Feed SSE text straight into a model, every event at `now`.
static void Feed(esphome::Model& m, const std::string& text, uint32_t now)
{
    const std::vector<Ev> evs = FrameAll(text);
    for (size_t i = 0; i < evs.size(); ++i) esphome::OnEvent(m, evs[i].first, evs[i].second, now);
}

static std::string StateEvent(const std::string& json) { return "event: state\ndata: " + json + "\n\n"; }

static std::string Bare(const char* nameId, const char* id, const std::string& rest)
{
    return StateEvent(std::string("{\"name_id\":\"") + nameId + "\",\"id\":\"" + id + "\"," + rest + "}");
}

static std::string CoverBare(const char* op, const char* pos)
{
    return Bare("cover/Glasscalibur", "cover-glasscalibur",
                std::string("\"value\":") + pos + ",\"state\":\"OPEN\",\"current_operation\":\"" + op + "\",\"position\":" + pos);
}

static std::string Counter(const char* nameId, const char* id, int v)
{
    char rest[64];
    std::snprintf(rest, sizeof(rest), "\"value\":%d,\"state\":\"%d\"", v, v);
    return Bare(nameId, id, rest);
}

static const std::string StateIdle = Bare("text_sensor/State", "text_sensor-state", "\"value\":\"IDLE\",\"state\":\"IDLE\"");

static std::string GC, RO;   // the two fixtures, LF

static esphome::Model LiveGlasscalibur(uint32_t now)
{
    esphome::Model m;
    esphome::OnConnect(m, now);
    Feed(m, GC, now);
    Feed(m, StateIdle, now);
    return m;
}

// ------------------------------------------------------------------------------------------ K, W

static void CaseK()
{
    const std::string crlf = CRLF(GC);
    const std::vector<std::string> blocks = Blocks(crlf);
    const std::string whole = Chunked(blocks, 0, blocks.size());
    std::string joined;
    for (size_t i = 0; i < blocks.size(); ++i) joined += blocks[i];
    bool ok;
    {
        sse::Dechunk d;
        ok = Decode(d, whole) == joined && !d.Broken() && blocks.size() >= 20;
        const std::string three = Chunked(blocks, 0, 3);
        std::string want3;
        for (size_t i = 0; i < 3; ++i) want3 += blocks[i];
        for (size_t cut = 0; cut <= three.size() && ok; ++cut) {
            sse::Dechunk s;
            std::string body = Decode(s, three.substr(0, cut));
            body += Decode(s, three.substr(cut));
            ok = body == want3 && !s.Broken();
        }
    }
    check("K1", ok, "the dump, chunked %08x per event, decodes the same whole and split at every byte of 3 events");

    sse::Dechunk e;
    const std::string payload = "abcdefghijklmnopqrstuvwxyz";
    std::string body = Decode(e, "1A;x=y\r\n" + payload + "\r\n");
    const bool ext = body == payload && !e.Broken();
    body += Decode(e, "zz\r\n0123456789\r\n");
    body += Decode(e, "0000000a\r\n0123456789\r\n");
    check("K2", ext && e.Broken() && body == payload, "an ;extension decodes; a size line that is not hex breaks it, and nothing after is delivered");

    sse::Dechunk z;
    Decode(z, "00000005\r\nhello\r\n");
    Decode(z, "00000000\r\n\r\n");
    check("K3", z.Done() && !z.Broken(), "a size-0 chunk ends the stream");
}

static void CaseW()
{
    const std::string crlf = CRLF(GC);
    const std::vector<Ev> want = FrameAll(crlf);
    bool ok = want.size() >= 20;
    for (size_t cut = 0; cut <= crlf.size() && ok; ++cut) {
        sse::Framer f;
        std::vector<Ev> got = Frame(f, crlf.substr(0, cut));
        const std::vector<Ev> rest = Frame(f, crlf.substr(cut));
        got.insert(got.end(), rest.begin(), rest.end());
        ok = got == want;
    }
    check("W1", ok, "the dump's events framed whole and split at every byte boundary are identical");
    check("W2", FrameAll(GC) == want, "CRLF and LF line endings frame identically");

    const std::vector<Ev> w3 = FrameAll(":a comment\nretry: 5\nid: 7\nevent: ping\ndata: x\n\n");
    check("W3", w3.size() == 1 && w3[0].first == "ping" && w3[0].second == "x", "comments, retry and id leave type and data alone");

    sse::Framer big;
    const std::string line = "data: " + std::string(3067, 'a');   // 3,073 bytes
    const std::vector<Ev> w4 = Frame(big, "event: state\n" + line + "\n\nevent: ping\ndata: ok\n\n");
    check("W4", w4.size() == 1 && w4[0].first == "ping" && w4[0].second == "ok" && big.Dropped() == 1,
          "a 3,073-byte line drops its whole event (counted once); the next arrives intact");

    const std::vector<Ev> w5 = FrameAll("event: e\ndata: a\ndata: b\n\n");
    check("W5", w5.size() == 1 && w5[0].second == "a\nb", "two data lines join with a newline");
}

// ------------------------------------------------------------------------------------------ M

static void CaseM()
{
    {
        esphome::Model m = LiveGlasscalibur(1000);
        char path[96];
        const Device d = esphome::Snapshot(m, 1000);
        check("M1", d.conn == Conn::Live && std::strcmp(m.coverName, "Glasscalibur") == 0 && d.positionPermille == 500
                    && d.op == Op::Idle && !d.alarmPending && m.stallGuard.based && m.stallGuard.base == 3
                    && m.vibration.based && m.vibration.base == 1 && esphome::CommandPath(m, Intent::Open, path, sizeof(path)),
              "Glasscalibur dump + one bare event: Live, by name, 500, Idle, no alarm, counters 3 and 1");
    }
    {
        esphome::Model m;
        esphome::OnConnect(m, 1000);
        Feed(m, GC, 1000);
        const Device d = esphome::Snapshot(m, 1000);
        check("M2", d.conn == Conn::Unknown && d.positionPermille == 500 && d.op == Op::Idle,
              "the dump alone: still Unknown, the snapshot already carries its operation and position");
    }
    {
        esphome::Model m;
        esphome::OnConnect(m, 1000);
        Feed(m, RO, 1000);
        Feed(m, Bare("text_sensor/State", "text_sensor-state", "\"value\":\"IDLE\",\"state\":\"IDLE\""), 1000);
        const Device d = esphome::Snapshot(m, 1000);
        check("M3", d.conn == Conn::Live && std::strcmp(m.coverName, "Ropener") == 0 && !d.alarmPending && d.positionPermille == 0,
              "Ropener dump + one bare event: Live, cover Ropener, no alarm");
    }
    {
        esphome::Model m = LiveGlasscalibur(1000);
        Feed(m, CoverBare("OPENING", "0.52"), 2000);
        const bool a = m.op == Op::Opening && m.lastDirection == Op::Opening && m.motionSeen && m.lastMotionMs == 2000;
        Feed(m, CoverBare("IDLE", "0.50"), 3000);
        const bool b = m.op == Op::Idle && m.lastMotionMs == 3000;
        Feed(m, CoverBare("IDLE", "0.47"), 4000);
        const bool c = m.lastMotionMs == 4000 && m.positionPermille == 470;
        Feed(m, CoverBare("IDLE", "0.47"), 5000);
        check("M4", a && b && c && m.lastMotionMs == 4000, "OPENING stamps motion and direction; IDLE moving stamps; IDLE still does not");
    }
    {
        esphome::Model m = LiveGlasscalibur(1000);
        Feed(m, CoverBare("IDLE", "0.0"), 1100);
        const bool z = m.positionPermille == 0;
        Feed(m, CoverBare("IDLE", "1.0"), 1200);
        const bool o = m.positionPermille == 1000;
        Feed(m, CoverBare("IDLE", "0.4996"), 1300);
        const bool r = m.positionPermille == 500;
        const uint32_t rej = m.rejects;
        Feed(m, CoverBare("OPENING", "-0.1"), 1400);
        const bool n1 = m.positionPermille == 500 && m.op == Op::Idle && m.rejects == rej + 1;
        Feed(m, CoverBare("OPENING", "1.2"), 1500);
        const bool n2 = m.positionPermille == 500 && m.op == Op::Idle && m.rejects == rej + 2;
        check("M5", z && o && r && n1 && n2, "0.0 -> 0, 1.0 -> 1000, 0.4996 -> 500; -0.1 and 1.2 each rejected whole");
    }
    {
        esphome::Model m = LiveGlasscalibur(1000);
        const uint32_t rej = m.rejects;
        Feed(m, CoverBare("WOBBLING", "0.6"), 1100);
        check("M6", m.op == Op::Idle && m.positionPermille == 500 && m.rejects == rej + 1, "an unknown current_operation is rejected whole");
    }
    {
        esphome::Model m = LiveGlasscalibur(1000);
        const char* sg = "sensor/StallGuard Trips";
        const char* sgId = "sensor-stallguard_trips";
        Feed(m, Counter(sg, sgId, 3), 1100);
        const bool a = !m.obstructionPending;
        Feed(m, Counter(sg, sgId, 4), 1200);
        const bool b = m.obstructionPending && esphome::Snapshot(m, 1200).alarmPending;
        esphome::Ack(m);
        const bool c = !m.obstructionPending;
        Feed(m, Counter(sg, sgId, 4), 1300);
        const bool d = !m.obstructionPending;
        Feed(m, Counter(sg, sgId, 0), 1400);
        const bool e = !m.obstructionPending && m.stallGuard.base == 0;
        Feed(m, Counter(sg, sgId, 1), 1500);
        const bool f = m.obstructionPending;
        esphome::Ack(m);
        Feed(m, Counter("sensor/Vibration Stall Trips", "sensor-vibration_stall_trips", 2), 1600);
        check("M7", a && b && c && d && e && f && m.obstructionPending,
              "trip counters: an increase is an obstruction until acked; a reset re-baselines; vibration too");
    }
    {
        esphome::Model m = LiveGlasscalibur(1000);
        Feed(m, Counter("sensor/StallGuard Trips", "sensor-stallguard_trips", 4), 1100);
        esphome::Ack(m);
        esphome::OnDisconnect(m);
        esphome::OnConnect(m, 5000);
        // The reconnect's dump says 6: the fixture with StallGuard's entry changed, asserted to have applied.
        const std::string from = "\"name\":\"StallGuard Trips\",\"icon\":\"\",\"entity_category\":2,\"value\":3,\"state\":\"3\"";
        const std::string to   = "\"name\":\"StallGuard Trips\",\"icon\":\"\",\"entity_category\":2,\"value\":6,\"state\":\"6\"";
        std::string gc6 = GC;
        const size_t at = gc6.find(from);
        const bool planted = at != std::string::npos && gc6.find(from, at + 1) == std::string::npos;
        if (planted) gc6.replace(at, from.size(), to);
        Feed(m, gc6, 5000);
        Feed(m, StateIdle, 5000);
        if (!planted || m.stallGuard.base != 6) std::printf("  RIG: M8's dump did not say 6\n");
        check("M8", planted && !m.obstructionPending && esphome::Snapshot(m, 5000).conn == Conn::Live,
              "a reconnect re-baselines: trips while not listening never fire");
    }
    {
        esphome::Model m = LiveGlasscalibur(1000);
        const char* td = "binary_sensor/Tamper Detected";
        const char* tdId = "binary_sensor-tamper_detected";
        Feed(m, Bare(td, tdId, "\"value\":true,\"state\":\"ON\""), 1100);
        const bool a = esphome::Snapshot(m, 1100).alarmPending;
        esphome::Ack(m);
        Feed(m, Bare(td, tdId, "\"value\":true,\"state\":\"ON\""), 1200);
        const bool b = !esphome::Snapshot(m, 1200).alarmPending;
        Feed(m, Bare(td, tdId, "\"value\":false,\"state\":\"OFF\""), 1300);
        Feed(m, Bare(td, tdId, "\"value\":true,\"state\":\"ON\""), 1400);
        const bool c = esphome::Snapshot(m, 1400).alarmPending;
        esphome::Model n = LiveGlasscalibur(1000);
        Feed(n, Bare("text_sensor/State", "text_sensor-state", "\"value\":\"TAMPERED\",\"state\":\"TAMPERED\""), 1100);
        check("M9", a && b && c && esphome::Snapshot(n, 1100).alarmPending,
              "tamper pends; ack holds while it stays; a new episode pends again; State TAMPERED alone pends");
    }
    {
        esphome::Model m = LiveGlasscalibur(1000);
        Feed(m, Bare("cover/Other Window", "cover-other_window", "\"value\":0,\"state\":\"CLOSED\",\"current_operation\":\"IDLE\",\"position\":0"), 1100);
        char path[96];
        check("M10", m.unsupported && !esphome::CommandPath(m, Intent::Open, path, sizeof(path))
                     && esphome::Snapshot(m, 1100).conn != Conn::Live,
              "a second, different cover: unsupported, no command path, not Live");
    }
    {
        char a[96] = {0}, b[96] = {0}, c[96] = {0}, d[96] = {0};
        esphome::Model byName = LiveGlasscalibur(1000);
        const bool ka = esphome::CommandPath(byName, Intent::Open, a, sizeof(a));
        esphome::Model legacy;
        esphome::OnConnect(legacy, 1000);
        Feed(legacy, StateEvent("{\"id\":\"cover-glasscalibur\",\"value\":0.5,\"state\":\"OPEN\",\"current_operation\":\"IDLE\",\"position\":0.5}"), 1000);
        const bool kb = esphome::CommandPath(legacy, Intent::Open, b, sizeof(b));
        esphome::Model spaced;
        esphome::OnConnect(spaced, 1000);
        Feed(spaced, Bare("cover/Kitchen Window", "cover-kitchen_window", "\"value\":0,\"state\":\"CLOSED\",\"current_operation\":\"IDLE\",\"position\":0"), 1000);
        const bool kc = esphome::CommandPath(spaced, Intent::Stop, c, sizeof(c));
        esphome::Model next;
        esphome::OnConnect(next, 1000);
        Feed(next, StateEvent("{\"id\":\"cover/Glasscalibur\",\"value\":0.5,\"state\":\"OPEN\",\"current_operation\":\"IDLE\",\"position\":0.5}"), 1000);
        const bool kd = esphome::CommandPath(next, Intent::Close, d, sizeof(d));
        check("M11", ka && kb && kc && kd && std::strcmp(a, "/cover/Glasscalibur/open") == 0
                     && std::strcmp(b, "/cover/glasscalibur/open") == 0 && std::strcmp(c, "/cover/Kitchen%20Window/stop") == 0
                     && std::strcmp(d, "/cover/Glasscalibur/close") == 0,
              "by name; legacy id; a space percent-encoded; the 2026.8 id shape");
    }
    {
        esphome::Model m = LiveGlasscalibur(1000);
        const Device before = esphome::Snapshot(m, 1000);
        const uint32_t p = m.parses, rej = m.rejects;
        Feed(m, Bare("sensor/Accel X", "sensor-accel_x", "\"value\":0.4,\"state\":\"0.4 m/s²\""), 1000);
        Feed(m, "event: log\ndata: [D][main:563]: MOTOR STALLED - backing off\n\n", 1000);
        Feed(m, "event: sorting_group\ndata: {\"name\":\"Control\",\"sorting_weight\":10}\n\n", 1000);
        Feed(m, "event: ping\ndata: {\"uptime\":99}\n\n", 1000);
        const Device after = esphome::Snapshot(m, 1000);
        check("M12", m.parses == p && m.rejects == rej && after.conn == before.conn && after.op == before.op
                     && after.positionPermille == before.positionPermille && after.alarmPending == before.alarmPending
                     && after.lastMotionMs == before.lastMotionMs,
              "accelerometer, log, sorting_group and ping events change nothing and are never parsed");
    }
    {
        esphome::Model m = LiveGlasscalibur(1000);
        const bool a = esphome::ConnOf(m, 1000 + 24999) == Conn::Live && esphome::ConnOf(m, 1000 + 25000) == Conn::Offline;
        esphome::Model p = LiveGlasscalibur(1000);
        Feed(p, "event: ping\ndata: {\"uptime\":20}\n\n", 21000);
        const bool b = esphome::ConnOf(p, 26000) == Conn::Live;
        esphome::Model q = LiveGlasscalibur(1000);
        Feed(q, CoverBare("OPENING", "0.55"), 2000);
        esphome::OnDisconnect(q);
        const Device d = esphome::Snapshot(q, 2100);
        check("M13", a && b && d.conn == Conn::Offline && d.op == Op::Opening,
              "silent 24,999 ms Live, 25,000 Offline; a ping keeps it Live; Offline keeps the last OPENING");
    }
}

int main(int argc, char** argv)
{
    if (argc < 3 || argv[2][0] == '\0') {
        std::printf("  RIG: usage: test_remote_stream <test/fixtures/remote> <ArduinoJson pin>\n");
        return 2;
    }
    std::printf("Remote event stream (src/remote/SseStream.h, src/remote/EsphomeModel.h)\n");
    std::printf("  ArduinoJson %s (platformio.ini pins %s)\n", ARDUINOJSON_VERSION, argv[2]);
    if (std::strcmp(ARDUINOJSON_VERSION, argv[2]) != 0) {
        std::printf("  RIG: the ArduinoJson under .pio/libdeps is not the version platformio.ini pins\n");
        return 2;
    }
    Dir = argv[1];
    if (!Load("glasscalibur-connect.sse", GC) || !Load("ropener-connect.sse", RO)) return 2;
    CaseK();
    CaseW();
    CaseM();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
