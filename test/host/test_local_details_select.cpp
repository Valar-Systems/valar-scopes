// Host test: the config page's Aircraft details list marks exactly ONE option selected, and its hint says what Blipscope
// Cloud sends (the owner, 2026-11-26: "hint: v20", with "the one-line fix to the Aircraft details list"). Registered as
// _handoffs/p4/privacy/REG-hint-v20.txt, L0-L3.
//
// IT READS THE SHIPPING SOURCE, NOT A COPY. ConfigurationWebServer.cpp needs the ESP Async Web Server and cannot be
// compiled here, so this parses the real <select> and the real template-processor lines at run time, from the repository
// root given as argv[1], and evaluates those lines for each stored value. A line it cannot parse is the rig (exit 2),
// never a guess.
//
// Exit 0 all good, 1 a test failed, 2 the rig cannot read what it tests.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool cond, const char* test, const std::string& what)
{
    if (!cond) {
        printf("  FAIL  %s  %s\n", test, what.c_str());
        ++failures;
    }
}

static int rig(const std::string& why)
{
    printf("RIG: %s\n", why.c_str());
    return 2;
}

static size_t count(const std::string& hay, const std::string& needle)
{
    size_t n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size())) ++n;
    return n;
}

// One processor line: if (var == "LD_X")  return <cond> ? "<whenTrue>" : "<whenFalse>";
// <cond> is `localDetails == "v"` or `(localDetails == "v1" || localDetails == "v2")`.
struct Rule {
    std::vector<std::string> values;   // the stored values for which <cond> holds
    std::string whenTrue, whenFalse;
};

static bool quoted(const std::string& s, size_t& p, std::string& out)
{
    if (p >= s.size() || s[p] != '"') return false;
    const size_t e = s.find('"', p + 1);
    if (e == std::string::npos) return false;
    out = s.substr(p + 1, e - p - 1);
    p = e + 1;
    return true;
}

static void skipSpace(const std::string& s, size_t& p)
{
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
}

static bool expect(const std::string& s, size_t& p, const std::string& lit)
{
    skipSpace(s, p);
    if (s.compare(p, lit.size(), lit) != 0) return false;
    p += lit.size();
    return true;
}

static bool parseRule(const std::string& line, Rule& r)
{
    size_t p = line.find("return ");
    if (p == std::string::npos) return false;
    p += 7;
    skipSpace(line, p);
    const bool paren = p < line.size() && line[p] == '(';
    if (paren) ++p;
    for (;;) {
        std::string v;
        if (!expect(line, p, "localDetails")) return false;
        if (!expect(line, p, "==")) return false;
        skipSpace(line, p);
        if (!quoted(line, p, v)) return false;
        r.values.push_back(v);
        skipSpace(line, p);
        if (paren && line.compare(p, 2, "||") == 0) { p += 2; continue; }
        break;
    }
    if (paren && !expect(line, p, ")")) return false;
    if (!expect(line, p, "?")) return false;
    skipSpace(line, p);
    if (!quoted(line, p, r.whenTrue)) return false;
    if (!expect(line, p, ":")) return false;
    skipSpace(line, p);
    if (!quoted(line, p, r.whenFalse)) return false;
    return expect(line, p, ";");
}

static std::string attrFor(const Rule& r, const std::string& stored)
{
    for (const std::string& v : r.values)
        if (v == stored) return r.whenTrue;
    return r.whenFalse;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: test_local_details_select <repository root>\n");
        return 2;
    }
    std::ifstream f((std::string(argv[1]) + "/src/ConfigurationWebServer.cpp").c_str(), std::ios::binary);
    if (!f) return rig("cannot read src/ConfigurationWebServer.cpp");
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string src = ss.str();

    // L0 THE ANCHOR: the select holds exactly %LD_UNSET%, %LD_CLOUD%, %LD_OFF%, in that order; one processor line each.
    const size_t s0 = src.find("<select name=\"local-details\"");
    if (s0 == std::string::npos || count(src, "<select name=\"local-details\"") != 1) return rig("the local-details select");
    const size_t s1 = src.find("</select>", s0);
    if (s1 == std::string::npos) return rig("the local-details select's end");
    const std::string sel = src.substr(s0, s1 - s0);
    std::vector<std::string> order;
    for (size_t p = sel.find("%LD_"); p != std::string::npos; p = sel.find("%LD_", p + 1)) {
        const size_t e = sel.find('%', p + 1);
        if (e == std::string::npos) return rig("an unterminated placeholder");
        order.push_back(sel.substr(p + 1, e - p - 1));
        p = e;
    }
    const char* const names[] = {"LD_UNSET", "LD_CLOUD", "LD_OFF"};
    if (order.size() != 3 || order[0] != names[0] || order[1] != names[1] || order[2] != names[2])
        return rig("the select's placeholders are not exactly LD_UNSET, LD_CLOUD, LD_OFF");
    Rule rules[3];
    for (int i = 0; i < 3; ++i) {
        const std::string key = std::string("if (var == \"") + names[i] + "\")";
        if (count(src, key) != 1) return rig(key + " is not exactly one line");
        const size_t a = src.find(key);
        const size_t e = src.find('\n', a);
        if (!parseRule(src.substr(a, e - a), rules[i])) return rig(key + " has a shape this test cannot read");
    }
    printf("read: the select's 3 options and their 3 processor lines\n");

    // L1 EXACTLY ONE selected, and L2 the right one, for each stored value.
    struct Want { const char* stored; int option; };
    const Want wants[] = {{"", 0}, {"cloud", 1}, {"off", 2}, {"adsbdb", 0}, {"x", 0}};
    for (const Want& w : wants) {
        int selected = 0, which = -1;
        std::string shown;
        for (int i = 0; i < 3; ++i) {
            const std::string a = attrFor(rules[i], w.stored);
            if (a == "selected") { ++selected; which = i; shown += std::string(shown.empty() ? "" : "+") + names[i]; }
            else if (!a.empty()) return rig(std::string(names[i]) + " returns \"" + a + "\", neither selected nor empty");
        }
        printf("stored \"%s\": selected %s\n", w.stored, shown.empty() ? "(none)" : shown.c_str());
        check(selected == 1, "L1", std::string("stored \"") + w.stored + "\": exactly one option selected");
        check(selected == 1 && which == w.option, "L2",
              std::string("stored \"") + w.stored + "\": " + names[w.option] + " is the one selected");
    }

    // L3 THE HINT: what Blipscope Cloud sends, once; the old \"tapped aircraft\" wording gone.
    const std::string now = "asks us about each aircraft in view (nearest first) and any you tap";
    const std::string was = "sends the tapped aircraft's hex";
    printf("hint: new %u, old %u\n", (unsigned)count(src, now), (unsigned)count(src, was));
    check(count(src, now) == 1 && count(src, was) == 0, "L3", "the Aircraft details hint is the owner's v20 sentence");

    printf(failures ? "\nlocal-details select: %d FAILURE(S)\n" : "\nlocal-details select: all good\n", failures);
    return failures ? 1 : 0;
}
