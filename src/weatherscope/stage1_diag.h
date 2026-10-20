#pragma once
// Weatherscope Stage 1 DIAGNOSTIC run (reviewer ruling 2026-10-20; docs/weatherscope-stage1.md, "Predictions
// B, addendum 4"). Included by stage1_main.cpp ONLY under WX_STAGE1_DIAG, which only the bench env
// weatherscope-s3-128-diag sets: the Stage 1 env compiles none of this, and no shipping env compiles
// src/weatherscope/ at all.
//
// It runs AFTER the measured boot path, which is the cold run, and changes nothing that path does.
// OPTIMIZES NOTHING: every variant below is a measurement. A faster one is a finding for Stage 2's
// design, not a change to Stage 1.
//
//   D-flash   decode the embedded crop (flash) into the PSRAM grid            -- Stage 1's path
//   D-ram     decode an internal-RAM copy of the crop into the PSRAM grid     -- how Stage 2 receives it
//   D-ram-int the same copy, decoded into an internal-RAM grid                -- isolates the PSRAM fill
//   I-double  the indices in double                                           -- Stage 1's BuildView
//   I-float   the same expressions in float
//   R-hline   render through writeFastHLine                                   -- Stage 1's Render
//   R-direct  render by writing the sprite buffer directly, one 16-bit store per pixel
// Each runs kReps times (warm); the median and max are reported against the cold boot numbers.
// No #include here: this file is included INSIDE stage1_main.cpp's anonymous namespace, which
// includes <algorithm> for it (under the same flag).

constexpr int kReps = 11;

struct DiagStat { uint32_t median = 0, max = 0; };
struct Diag {
    bool ran = false;
    const char* error = nullptr;
    DiagStat dFlash, dRam, dRamInt, iDouble, iFloat, rHline, rDirect;
    char shaDFlash[17] = "", shaDRam[17] = "", shaDRamInt[17] = "", shaIDouble[17] = "", shaIFloat[17] = "";
    char spriteHline[17] = "", spriteDirect[17] = "";
    int floatIndexDiff = -1, floatPixelDiff = -1;
} diag;

DiagStat Summarize(uint32_t* v, int n)
{
    std::sort(v, v + n);
    DiagStat s;
    s.median = v[n / 2];
    s.max = v[n - 1];
    return s;
}

void HexOf(const uint8_t digest[32], char out[17])
{
    for (int i = 0; i < 8; ++i) snprintf(out + 2 * i, 3, "%02x", digest[i]);
}

// sha256 of the 57,600-byte code buffer this grid and these indices give (CodeAt over every pixel).
void CodeBufferSha(const uint8_t* g, const wxrender::View& v, char out[17])
{
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    uint8_t row[wxrender::kSize];
    for (int y = 0; y < wxrender::kSize; ++y) {
        for (int x = 0; x < wxrender::kSize; ++x) row[x] = wxrender::CodeAt(hdr, g, v, x, y);
        mbedtls_sha256_update(&sha, row, sizeof row);
    }
    uint8_t d[32];
    mbedtls_sha256_finish(&sha, d);
    mbedtls_sha256_free(&sha);
    HexOf(d, out);
}

void SpriteSha(char out[17])
{
    uint8_t d[32];
    mbedtls_sha256((const unsigned char*)canvas.getBuffer(), (size_t)wxrender::kSize * wxrender::kSize * 2, d, 0);
    HexOf(d, out);
}

// I-float: BuildView's expressions, in the same order, in float.
void BuildViewFloat(const wxcrop::Header& h, wxrender::View* v)
{
    const float latNw = h.latNwMicro / 1e6f, lonNw = h.lonNwMicro / 1e6f;
    const float cellLat = h.cellLatMicro / 1e6f, cellLon = h.cellLonMicro / 1e6f;
    const float homeLat = latNw - (h.rows - 1) / 2.0f * cellLat;
    const float homeLon = lonNw + (h.cols - 1) / 2.0f * cellLon;
    const float rLat = (float)wxrender::kRadiusKm / (float)wxrender::kKmPerDeg;
    float c = cosf(homeLat * ((float)wxrender::kPi / 180.0f));
    if (c < 0.01f) c = 0.01f;
    const float rLon = (float)wxrender::kRadiusKm / ((float)wxrender::kKmPerDeg * c);
    const float C = (float)wxrender::kCentre;
    for (int y = 0; y < wxrender::kSize; ++y) {
        const float lat = homeLat + 1.0f * (C - (y + 0.5f)) / C * rLat;
        v->row[y] = (int16_t)floorf((latNw - lat) / cellLat + 0.5f);
    }
    for (int x = 0; x < wxrender::kSize; ++x) {
        const float lon = homeLon + ((x + 0.5f) - C) / C * rLon;
        v->col[x] = (int16_t)floorf((lon - lonNw) / cellLon + 0.5f);
    }
}

// R-direct: the same colours as Render(), stored straight into the 16-bit sprite buffer. LovyanGFX keeps a
// 16-bit sprite's pixels byte-swapped (big-endian RGB565); the sprite-sha identity check catches it if not.
void RenderDirect()
{
    uint16_t pal[16];
    for (int i = 0; i < 16; ++i) {
        const uint16_t c = canvas.color565(kPalette[i][0], kPalette[i][1], kPalette[i][2]);
        pal[i] = (uint16_t)((c >> 8) | (c << 8));
    }
    uint16_t* buf = (uint16_t*)canvas.getBuffer();
    for (int y = 0; y < wxrender::kSize; ++y)
        for (int x = 0; x < wxrender::kSize; ++x) {
            const uint8_t code = wxrender::CodeAt(hdr, grid, view, x, y);
            buf[y * wxrender::kSize + x] = pal[code == wxrender::kOffDisc ? 0 : (code & 15)];
        }
}

void RunDiagnostics()
{
    if (!grid || !rep.sprite) { diag.error = "no grid or sprite"; return; }
    const uint8_t* flashCrop = kdfw_crop_start;
    const size_t n = rep.cropBytes;
    uint8_t* ramCopy = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t* gridInt = (uint8_t*)heap_caps_malloc(hdr.Cells(), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!ramCopy || !gridInt) { diag.error = "internal allocation failed"; heap_caps_free(ramCopy); heap_caps_free(gridInt); return; }
    memcpy(ramCopy, flashCrop, n);   // once, outside the timing

    static wxrender::View vd, vf;
    uint32_t t[kReps];
    for (int i = 0; i < kReps; ++i) { const uint32_t s = micros(); wxcrop::DecodeInto(flashCrop, n, grid, hdr.Cells()); t[i] = micros() - s; }
    diag.dFlash = Summarize(t, kReps);
    CodeBufferSha(grid, view, diag.shaDFlash);
    for (int i = 0; i < kReps; ++i) { const uint32_t s = micros(); wxcrop::DecodeInto(ramCopy, n, grid, hdr.Cells()); t[i] = micros() - s; }
    diag.dRam = Summarize(t, kReps);
    CodeBufferSha(grid, view, diag.shaDRam);
    for (int i = 0; i < kReps; ++i) { const uint32_t s = micros(); wxcrop::DecodeInto(ramCopy, n, gridInt, hdr.Cells()); t[i] = micros() - s; }
    diag.dRamInt = Summarize(t, kReps);
    CodeBufferSha(gridInt, view, diag.shaDRamInt);

    for (int i = 0; i < kReps; ++i) { const uint32_t s = micros(); wxrender::BuildView(hdr, &vd); t[i] = micros() - s; }
    diag.iDouble = Summarize(t, kReps);
    CodeBufferSha(grid, vd, diag.shaIDouble);
    for (int i = 0; i < kReps; ++i) { const uint32_t s = micros(); BuildViewFloat(hdr, &vf); t[i] = micros() - s; }
    diag.iFloat = Summarize(t, kReps);
    CodeBufferSha(grid, vf, diag.shaIFloat);
    diag.floatIndexDiff = 0;
    for (int k = 0; k < wxrender::kSize; ++k) diag.floatIndexDiff += (vf.row[k] != vd.row[k]) + (vf.col[k] != vd.col[k]);
    diag.floatPixelDiff = 0;
    for (int y = 0; y < wxrender::kSize; ++y)
        for (int x = 0; x < wxrender::kSize; ++x)
            diag.floatPixelDiff += wxrender::CodeAt(hdr, grid, vf, x, y) != wxrender::CodeAt(hdr, grid, vd, x, y);

    for (int i = 0; i < kReps; ++i) { const uint32_t s = micros(); Render(); t[i] = micros() - s; }
    diag.rHline = Summarize(t, kReps);
    SpriteSha(diag.spriteHline);
    for (int i = 0; i < kReps; ++i) { const uint32_t s = micros(); RenderDirect(); t[i] = micros() - s; }
    diag.rDirect = Summarize(t, kReps);
    SpriteSha(diag.spriteDirect);
    Render();   // leave the sprite as Stage 1 drew it (it is not pushed again)

    heap_caps_free(ramCopy);
    heap_caps_free(gridInt);
    diag.ran = true;
}

void PrintDiagnostics()
{
    if (!diag.ran) { Serial.printf("WXD,error,%s\n", diag.error ? diag.error : "not run"); return; }
    Serial.printf("WXD,cold,decode_us=%lu,index_us=%lu,render_us=%lu\n", (unsigned long)rep.decodeUs,
                  (unsigned long)rep.indexUs, (unsigned long)rep.renderUs);
    const struct { const char* name; const DiagStat* s; } rows[] = {
        {"d_flash", &diag.dFlash}, {"d_ram", &diag.dRam}, {"d_ram_int", &diag.dRamInt}, {"i_double", &diag.iDouble},
        {"i_float", &diag.iFloat}, {"r_hline", &diag.rHline}, {"r_direct", &diag.rDirect}};
    for (const auto& r : rows)
        Serial.printf("WXD,%s,median=%lu,max=%lu,n=%d\n", r.name, (unsigned long)r.s->median, (unsigned long)r.s->max, kReps);
    Serial.printf("WXD,code_sha,d_flash=%s,d_ram=%s,d_ram_int=%s,i_double=%s,i_float=%s\n", diag.shaDFlash, diag.shaDRam,
                  diag.shaDRamInt, diag.shaIDouble, diag.shaIFloat);
    Serial.printf("WXD,float_index_diff,%d,pixels=%d\n", diag.floatIndexDiff, diag.floatPixelDiff);
    Serial.printf("WXD,sprite_sha,r_hline=%s,r_direct=%s\n", diag.spriteHline, diag.spriteDirect);
}
