#pragma once

#include <Arduino.h>
#include <ctype.h>
#include <math.h>

// Forgiving latitude/longitude parsing for the config form.
//
// WHY THIS EXISTS. The location fields are where first-time owners strand: the
// page used to serve <input type="number" step="0.000001">, and a paste from
// gps-coordinates.org (15 decimal places) FAILS the browser's own step
// constraint. The browser then blocks submission before the submit event fires,
// so the page's JS never ran and nothing we wrote could report it -- the save
// button simply appeared to do nothing. The fields are plain text now and this
// does the understanding instead.
//
// This is the SERVER-SIDE half. The config page's shell JS (bpN/bpOne/bpPair in
// ConfigurationWebServer.cpp) does the same job in the browser so the customer
// gets an immediate echo of what was understood; this pass exists because a
// client with JS disabled, a saved bookmarklet or a curl one-liner can POST
// straight to /save, and NVS should never end up holding a string the radar
// can't turn into a position. Keep the two in step.
//
// Where they differ, this one is the STRICTER of the pair, deliberately: a value
// the browser accepted and this rejects yields a clear error, whereas the
// reverse would store something the customer was never shown.
namespace CoordParse {

inline bool isHemi(char c) { return c == 'N' || c == 'S' || c == 'E' || c == 'W'; }

// Fold the punctuation people actually paste down to plain ASCII, uppercased.
// Mirrors bpN() in the page JS. Input is UTF-8 from the form POST, so the
// multi-byte forms are matched by their byte sequences.
inline String Fold(const String& in)
{
    String out;
    out.reserve(in.length());
    const uint8_t* p = reinterpret_cast<const uint8_t*>(in.c_str());
    const size_t n = in.length();
    for (size_t i = 0; i < n;) {
        const uint8_t c = p[i];
        if (c < 0x80) {
            char ch = static_cast<char>(c);
            // ASCII prime marks and whitespace are all just separators here.
            if (ch == '\'' || ch == '"' || ch == '\t' || ch == '\r' || ch == '\n') ch = ' ';
            out += static_cast<char>(toupper(static_cast<unsigned char>(ch)));
            i++;
            continue;
        }
        // U+00B0 DEGREE / U+00BA MASCULINE ORDINAL -- separators.
        if (c == 0xC2 && i + 1 < n && (p[i + 1] == 0xB0 || p[i + 1] == 0xBA)) { out += ' '; i += 2; continue; }
        if (c == 0xE2 && i + 2 < n && p[i + 1] == 0x80) {
            const uint8_t t = p[i + 2];
            // U+2032 PRIME / U+2019 RIGHT SINGLE QUOTE / U+2033 DOUBLE PRIME /
            // U+201D RIGHT DOUBLE QUOTE -- minute and second marks.
            if (t == 0xB2 || t == 0x99 || t == 0xB3 || t == 0x9D) { out += ' '; i += 3; continue; }
            // U+2013 EN DASH / U+2014 EM DASH -- what a word processor turns a
            // hyphen into, and still meant as a minus sign.
            if (t == 0x93 || t == 0x94) { out += '-'; i += 3; continue; }
        }
        // U+2212 MINUS SIGN: the one non-ASCII character whose MEANING matters.
        // Folding it to a separator would silently flip a western longitude to
        // the eastern hemisphere, so it maps to '-'.
        if (c == 0xE2 && i + 2 < n && p[i + 1] == 0x88 && p[i + 2] == 0x92) { out += '-'; i += 3; continue; }
        // Anything else non-ASCII becomes a character the filter below rejects:
        // an unrecognised symbol must fail the value, not disappear from it.
        out += '?';
        i++;
    }
    out.trim();
    return out;
}

// Parse one coordinate. Accepts decimal degrees, degrees + decimal minutes, and
// degrees/minutes/seconds, with the hemisphere letter at either end. Returns
// false (leaving `out` untouched) for anything it cannot read with certainty.
inline bool Parse(const String& raw, bool isLat, double& out)
{
    const String s = Fold(raw);
    if (s.isEmpty()) return false;

    char hemi = 0;
    int hemiCount = 0;
    for (unsigned int i = 0; i < s.length(); i++) {
        const char c = s[i];
        if (isHemi(c)) {
            if (hemiCount == 0) hemi = c;
            hemiCount++;
            continue;
        }
        if (!(isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+' || c == ' '))
            return false;
    }
    if (hemiCount > 1) return false;
    // A latitude cannot be E/W and a longitude cannot be N/S. This is what
    // catches a pair pasted into the boxes the wrong way round.
    if (hemi != 0 && isLat && (hemi == 'E' || hemi == 'W')) return false;
    if (hemi != 0 && !isLat && (hemi == 'N' || hemi == 'S')) return false;

    double term[3] = { 0, 0, 0 };
    int nt = 0;
    unsigned int i = 0;
    const unsigned int n = s.length();
    while (i < n) {
        const char c = s[i];
        if (c == ' ' || isHemi(c)) { i++; continue; }
        if (nt >= 3) return false; // more than deg/min/sec is not a coordinate
        const unsigned int start = i;
        if (s[i] == '-' || s[i] == '+') i++;
        int digits = 0, dots = 0;
        while (i < n && (isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) {
            if (s[i] == '.') dots++; else digits++;
            i++;
        }
        if (digits == 0 || dots > 1) return false;
        // Whatever stopped the scan must be a legal separator; otherwise this was
        // something like "44-3" and taking the leading term would be a guess.
        if (i < n && !(s[i] == ' ' || isHemi(s[i]))) return false;
        term[nt++] = atof(s.substring(start, i).c_str());
    }
    if (nt == 0) return false;

    const double mins = nt > 1 ? term[1] : 0.0;
    const double secs = nt > 2 ? term[2] : 0.0;
    // Only the degrees term carries a sign; a negative or overflowing minutes or
    // seconds term is a typo, not a coordinate.
    if (mins < 0.0 || secs < 0.0 || mins >= 60.0 || secs >= 60.0) return false;

    double v = fabs(term[0]) + mins / 60.0 + secs / 3600.0;
    if (term[0] < 0.0 || hemi == 'S' || hemi == 'W') v = -v;
    if (!(fabs(v) <= (isLat ? 90.0 : 180.0))) return false;

    out = v;
    return true;
}

// Canonical stored form: 6 dp is ~11 cm, far past anything a desk radar can use,
// and trailing zeros are noise in a text box the customer reads back.
inline String Format(double v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6f", v);
    String s(buf);
    if (s.indexOf('.') >= 0) {
        while (s.endsWith("0")) s.remove(s.length() - 1);
        if (s.endsWith(".")) s.remove(s.length() - 1);
    }
    if (s == "-0") s = "0"; // snprintf renders a tiny negative as "-0.000000"
    return s;
}

} // namespace CoordParse
