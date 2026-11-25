// Host test: aircraft labels default to the type only (the Basic preset), with no migration.
//
// THE OWNER'S RULING (2026-11-25): "On a new set-up, aircraft labels default to the type only (the Basic preset). Every
// other field stays one tick away and the presets stay as they are. A saved setting is never rewritten." And: "no
// migration". Registered as _handoffs/p4/typeonly/REG-type-only.txt, T1-T4.
//
// IT READS THE SHIPPING SOURCE, NOT A COPY. The field table lives in src/AircraftInfoFields.cpp, which needs Arduino
// types and cannot be compiled here, so a test that transcribed its defaultOn column would keep passing while the real
// column changed. This parses the real table and the real preset buttons at run time, from the repository root given as
// argv[1]. ResolveToggle (ConfigMigration.h) and labellines::Select (LabelLines.h) are pure and are the real code.
//
// Exit 0 all good, 1 a test failed, 2 the rig cannot read what it tests.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../../include/ConfigMigration.h"
#include "../../include/LabelLines.h"

static int failures = 0;

static void check(bool cond, const char* test, const char* what)
{
    if (!cond) {
        printf("  FAIL  %s  %s\n", test, what);
        ++failures;
    }
}

static bool readFile(const std::string& path, std::string& out)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

struct Field {
    std::string key;
    bool defaultOn;
};

// The rows of AIRCRAFT_INFO_FIELDS[]: `{ "info-x", "Label", <defaultOn>, <needsLookup>, fmtX },`. Comment lines between
// rows are skipped because a row is only a line whose first non-blank characters are `{ "info-`.
static std::vector<Field> parseTable(const std::string& src)
{
    std::vector<Field> fields;
    const size_t start = src.find("AIRCRAFT_INFO_FIELDS[] = {");
    if (start == std::string::npos) return fields;
    const size_t end = src.find("\n};", start);
    std::istringstream lines(src.substr(start, end == std::string::npos ? std::string::npos : end - start));
    std::string line;
    while (std::getline(lines, line)) {
        const size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos || line.compare(b, 8, "{ \"info-") != 0) continue;
        const size_t k0 = line.find('"', b) + 1, k1 = line.find('"', k0);
        const size_t l0 = line.find('"', k1 + 1), l1 = line.find('"', l0 + 1);   // the on-page label
        const size_t c = line.find(',', l1);                                      // after the label
        const size_t v0 = line.find_first_not_of(" \t", c + 1);
        Field f;
        f.key = line.substr(k0, k1 - k0);
        if (line.compare(v0, 4, "true") == 0) f.defaultOn = true;
        else if (line.compare(v0, 5, "false") == 0) f.defaultOn = false;
        else { fields.clear(); return fields; }   // a row this parser does not understand: refuse, do not guess
        fields.push_back(f);
    }
    return fields;
}

static std::vector<std::string> parsePresets(const std::string& src)
{
    std::vector<std::string> out;
    const std::string tag = "data-preset=\"";
    size_t p = 0;
    while ((p = src.find(tag, p)) != std::string::npos) {
        p += tag.size();
        const size_t e = src.find('"', p);
        out.push_back(src.substr(p, e - p));
        p = e;
    }
    return out;
}

// The lines a board draws, given each field's enabled state (every enabled field is assumed to format to something).
static std::vector<std::string> linesFor(const std::vector<Field>& t, const std::vector<bool>& enabled)
{
    std::vector<const char*> keys;
    bool willDraw[64];
    for (size_t i = 0; i < t.size(); ++i) {
        keys.push_back(t[i].key.c_str());
        willDraw[i] = enabled[i];
    }
    int out[labellines::MAX_LINES];
    const int n = labellines::Select(keys.data(), willDraw, (int)t.size(), out);
    std::vector<std::string> lines;
    for (int i = 0; i < n; ++i) lines.push_back(t[out[i]].key);
    return lines;
}

static std::string join(const std::vector<std::string>& v)
{
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? " / " : "") + v[i];
    return s.empty() ? "(none)" : s;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: test_type_only_default <repository root>\n");
        return 2;
    }
    const std::string root = argv[1];
    std::string fieldsSrc, pageSrc;
    if (!readFile(root + "/src/AircraftInfoFields.cpp", fieldsSrc) ||
        !readFile(root + "/src/ConfigurationWebServer.cpp", pageSrc)) {
        printf("RIG: cannot read the sources under %s\n", root.c_str());
        return 2;
    }
    const std::vector<Field> t = parseTable(fieldsSrc);
    const std::vector<std::string> presets = parsePresets(pageSrc);
    printf("read: %u fields, %u presets\n", (unsigned)t.size(), (unsigned)presets.size());
    // THE ANCHOR: the table has its fifteen rows and the page its three presets, or nothing below means anything.
    if (t.size() != 15 || presets.size() != 3) {
        printf("RIG: expected 15 fields and 3 presets\n");
        return 2;
    }

    // T1 THE DEFAULT SET is exactly {info-type}: the Basic preset.
    {
        std::vector<std::string> on;
        for (size_t i = 0; i < t.size(); ++i)
            if (t[i].defaultOn) on.push_back(t[i].key);
        printf("T1 default-on fields: %s\n", join(on).c_str());
        check(on.size() == 1 && on[0] == "info-type", "T1", "the default set is exactly {info-type}");
    }

    // T2 A NEVER-SAVED BOARD: every key unset, so each resolves to its default, and the radar draws one line, the type.
    {
        std::vector<bool> en;
        for (size_t i = 0; i < t.size(); ++i) en.push_back(configmigration::ResolveToggle(nullptr, t[i].defaultOn));
        const std::vector<std::string> lines = linesFor(t, en);
        printf("T2 never-saved board draws: %s\n", join(lines).c_str());
        check(lines.size() == 1 && lines[0] == "info-type", "T2", "a never-saved board draws exactly the type");
    }

    // T3 A SAVED BOARD IS UNTOUCHED: v19's defaults as a Save froze them (five "true", ten "false") still draw v19's
    // three lines, whatever the table's defaults now are. Explicit values win (ResolveToggle).
    {
        const char* savedTrue[] = {"info-callsign", "info-type", "info-operator", "info-speed", "info-baroalt"};
        std::vector<bool> en;
        for (size_t i = 0; i < t.size(); ++i) {
            bool isTrue = false;
            for (size_t j = 0; j < sizeof(savedTrue) / sizeof(savedTrue[0]); ++j)
                if (t[i].key == savedTrue[j]) isTrue = true;
            en.push_back(configmigration::ResolveToggle(isTrue ? "true" : "false", t[i].defaultOn));
        }
        const std::vector<std::string> lines = linesFor(t, en);
        printf("T3 saved v19 board draws: %s\n", join(lines).c_str());
        check(lines.size() == 3 && lines[0] == "info-type" && lines[1] == "info-callsign" && lines[2] == "info-baroalt",
              "T3", "a saved board keeps type / callsign / baro alt");
    }

    // T4 THE PRESETS ARE BYTE-EQUAL, in order: None, Basic, Spotter.
    {
        printf("T4 presets: \"%s\" | \"%s\" | \"%s\"\n", presets[0].c_str(), presets[1].c_str(), presets[2].c_str());
        check(presets[0] == "none" && presets[1] == "info-type" && presets[2] == "info-type info-callsign", "T4",
              "the presets are unchanged");
    }

    printf(failures ? "\ntype-only default: %d FAILURE(S)\n" : "\ntype-only default: all good\n", failures);
    return failures ? 1 : 0;
}
