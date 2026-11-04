#pragma once

// Blipscope web demo (tools/wasm-demo): a stand-in for LovyanGFX that RECORDS every draw call as a
// vector command instead of writing pixels. The page replays the list on a <canvas> at the glass's
// own resolution (CSS px x devicePixelRatio), so a 1 px line on the 240 px panel is a crisp line at
// whatever size the browser shows the disc, never a 240 x 240 bitmap scaled up.
//
// Semantics kept from LovyanGFX, because the layout code depends on them:
//   - colour by argument TYPE (misc/colortype.hpp): uint8_t RGB332, uint16_t/int RGB565, uint32_t
//     RGB888; then quantised to the sprite's depth (565 on every S3 SKU), exactly as the panel shows it;
//   - the default font is Font0, the 6 x 8 glcd font, advance floor(6 * size_x) per glyph
//     (GLCDfont::drawChar), height floor(8 * size_y); textWidth and fontHeight answer from that;
//   - setTextColor(fg) draws no background; setTextColor(fg, bg) fills each glyph cell.
// The page draws glyphs from the same glcd bitmap (device font) or a web font fitted to the same cell.
//
// The real include/LGFX.h compiles against this file unchanged. Every SKU is normalised onto its
// GC9A01 + CST816 block (below): the web build has no panel, and the per-panel blocks only set pins.

#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <type_traits>
#include "Arduino.h"

#undef BLIPSCOPE_PANEL_ST7701
#undef BLIPSCOPE_PANEL_SPD2010
#undef BLIPSCOPE_PANEL_CO5300
#undef BLIPSCOPE_TOUCH_SPD2010
#undef BLIPSCOPE_TOUCH_FT5X06
#ifndef BLIPSCOPE_PANEL_GC9A01
#define BLIPSCOPE_PANEL_GC9A01 1
#endif
#ifndef BLIPSCOPE_TOUCH_CST816
#define BLIPSCOPE_TOUCH_CST816 1
#endif
#ifndef BLIPSCOPE_DISP_SPI_HOST
#define BLIPSCOPE_DISP_SPI_HOST 0
#endif
#ifndef BLIPSCOPE_DISP_FREQ_WRITE
#define BLIPSCOPE_DISP_FREQ_WRITE 40000000
#endif
#ifndef BLIPSCOPE_DISP_PIN_DC
#define BLIPSCOPE_DISP_PIN_DC (-1)
#endif
#ifndef BLIPSCOPE_DISP_PIN_CS
#define BLIPSCOPE_DISP_PIN_CS (-1)
#endif
#ifndef BLIPSCOPE_DISP_PIN_RST
#define BLIPSCOPE_DISP_PIN_RST (-1)
#endif
#ifndef BLIPSCOPE_DISP_PIN_BUSY
#define BLIPSCOPE_DISP_PIN_BUSY (-1)
#endif
#define SPI2_HOST 1
#define SPI3_HOST 2

namespace lgfx {

constexpr uint32_t color888(uint8_t r, uint8_t g, uint8_t b) { return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
constexpr uint16_t color565(uint8_t r, uint8_t g, uint8_t b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
constexpr uint8_t color332(uint8_t r, uint8_t g, uint8_t b) { return (uint8_t)((r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6)); }

struct rgb565_t { uint16_t raw; rgb565_t(uint16_t v = 0) : raw(v) {} uint16_t get() const { return raw; } };
struct rgb888_t { uint32_t raw; rgb888_t(uint32_t v = 0) : raw(v) {} uint32_t get() const { return raw; } };
struct rgb332_t { uint8_t raw; rgb332_t(uint8_t v = 0) : raw(v) {} uint8_t get() const { return raw; } };

inline uint32_t expand565(uint32_t c)
{
    uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}
inline uint32_t expand332(uint32_t c)
{
    uint32_t r = (c >> 5) & 7, g = (c >> 2) & 7, b = c & 3;
    return ((r * 255 / 7) << 16) | ((g * 255 / 7) << 8) | (b * 255 / 3);
}
// LovyanGFX's convert_to_rgb888, by argument type (misc/colortype.hpp TYPECHECK).
template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
inline uint32_t to888(T c)
{
    if (sizeof(T) == 1) return expand332((uint8_t)c);
    if (sizeof(T) == 2) return expand565((uint16_t)c);
    if (sizeof(T) == 4 && std::is_signed<T>::value) return expand565((uint16_t)c);
    return (uint32_t)c & 0xFFFFFF;
}
inline uint32_t to888(const rgb565_t& c) { return expand565(c.get()); }
inline uint32_t to888(const rgb888_t& c) { return c.get() & 0xFFFFFF; }
inline uint32_t to888(const rgb332_t& c) { return expand332(c.get()); }

enum color_depth_t : uint8_t { rgb332_1Byte = 8, rgb565_2Byte = 16, rgb888_3Byte = 24 };

enum textdatum_t : uint8_t {
    top_left = 0, top_center = 1, top_centre = 1, top_right = 2,
    middle_left = 4, middle_center = 5, middle_centre = 5, middle_right = 6,
    bottom_left = 8, bottom_center = 9, bottom_centre = 9, bottom_right = 10,
    baseline_left = 16, baseline_center = 17, baseline_centre = 17, baseline_right = 18
};

struct IFont { int id; int width; int height; };
namespace fonts {
    extern const IFont Font0, Font2, Font4, FreeSans9pt7b, FreeSansBold9pt7b, FreeSansBold12pt7b, AsciiFont8x16;
}

// ---- the display list ----------------------------------------------------------------------
// One flat float array per drawable: [length, op, colour, args...]. Colours are 0xRRGGBB as floats
// (exact below 2^24). Text is [len, OP_TEXT, fg, bg (-1 none), x, y, sx, sy, offset, count] into
// `text`. The page decodes this (site/radar.js); the order is the paint order.
enum Op : int {
    OP_FILL_SCREEN = 0, OP_LINE, OP_CIRCLE, OP_FILL_CIRCLE, OP_FILL_TRI, OP_FILL_RECT, OP_RECT,
    OP_FILL_RRECT, OP_RRECT, OP_FILL_ARC, OP_ARC, OP_TEXT, OP_PIXEL, OP_IMAGE, OP_ELLIPSE, OP_FILL_ELLIPSE
};

struct DisplayList {
    std::vector<float> ops;
    std::string text;
    uint32_t pixelCalls = 0;     // drawPixel / writePixel: what still draws one pixel at a time
    uint32_t imageCalls = 0;     // pushImage / drawJpg / a bitmap sprite composited: raster content
    void clear() { ops.clear(); text.clear(); pixelCalls = 0; imageCalls = 0; }
    void op(std::initializer_list<float> v) { ops.push_back((float)v.size() + 1); ops.insert(ops.end(), v.begin(), v.end()); }
};

class ITouch { public: virtual ~ITouch() {} virtual bool init() { return true; } virtual void wakeup() {} virtual void sleep() {} };

class LGFXBase
{
  public:
    virtual ~LGFXBase() {}
    int32_t width() const { return w_; }
    int32_t height() const { return h_; }
    DisplayList& list() { return dl_; }

    template <typename T> void fillScreen(const T& c) { dl_.op({OP_FILL_SCREEN, q(c)}); }
    template <typename T> void clear(const T& c) { fillScreen(c); }
    void clear() { fillScreen((uint32_t)0); }
    template <typename T> void drawPixel(int32_t x, int32_t y, const T& c) { dl_.pixelCalls++; dl_.op({OP_PIXEL, q(c), (float)x, (float)y}); }
    template <typename T> void writePixel(int32_t x, int32_t y, const T& c) { drawPixel(x, y, c); }
    template <typename T> void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const T& c) { dl_.op({OP_LINE, q(c), (float)x0, (float)y0, (float)x1, (float)y1}); }
    template <typename T> void drawFastHLine(int32_t x, int32_t y, int32_t w, const T& c) { if (w > 0) fillRect(x, y, w, 1, c); }
    template <typename T> void drawFastVLine(int32_t x, int32_t y, int32_t h, const T& c) { if (h > 0) fillRect(x, y, 1, h, c); }
    template <typename T> void drawCircle(int32_t x, int32_t y, int32_t r, const T& c) { dl_.op({OP_CIRCLE, q(c), (float)x, (float)y, (float)r}); }
    template <typename T> void fillCircle(int32_t x, int32_t y, int32_t r, const T& c) { dl_.op({OP_FILL_CIRCLE, q(c), (float)x, (float)y, (float)r}); }
    template <typename T> void drawEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, const T& c) { dl_.op({OP_ELLIPSE, q(c), (float)x, (float)y, (float)rx, (float)ry}); }
    template <typename T> void fillEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, const T& c) { dl_.op({OP_FILL_ELLIPSE, q(c), (float)x, (float)y, (float)rx, (float)ry}); }
    template <typename T> void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, const T& c) { dl_.op({OP_FILL_TRI, q(c), (float)x0, (float)y0, (float)x1, (float)y1, (float)x2, (float)y2}); }
    template <typename T> void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, const T& c) { drawLine(x0, y0, x1, y1, c); drawLine(x1, y1, x2, y2, c); drawLine(x2, y2, x0, y0, c); }
    template <typename T> void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, const T& c) { if (w > 0 && h > 0) dl_.op({OP_FILL_RECT, q(c), (float)x, (float)y, (float)w, (float)h}); }
    template <typename T> void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, const T& c) { if (w > 0 && h > 0) dl_.op({OP_RECT, q(c), (float)x, (float)y, (float)w, (float)h}); }
    template <typename T> void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, const T& c) { if (w > 0 && h > 0) dl_.op({OP_FILL_RRECT, q(c), (float)x, (float)y, (float)w, (float)h, (float)r}); }
    template <typename T> void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, const T& c) { if (w > 0 && h > 0) dl_.op({OP_RRECT, q(c), (float)x, (float)y, (float)w, (float)h, (float)r}); }
    template <typename T> void fillArc(int32_t x, int32_t y, int32_t r0, int32_t r1, float a0, float a1, const T& c) { dl_.op({OP_FILL_ARC, q(c), (float)x, (float)y, (float)r0, (float)r1, a0, a1}); }
    template <typename T> void drawArc(int32_t x, int32_t y, int32_t r0, int32_t r1, float a0, float a1, const T& c) { dl_.op({OP_ARC, q(c), (float)x, (float)y, (float)r0, (float)r1, a0, a1}); }

    // ---- text: Font0 metrics, LovyanGFX's ----
    void setFont(const IFont* f) { font_ = f; }
    const IFont* getFont() const { return font_; }
    void setTextSize(float s) { sx_ = sy_ = s; }
    void setTextSize(float sx, float sy) { sx_ = sx; sy_ = sy; }
    float getTextSizeX() const { return sx_; }
    float getTextSizeY() const { return sy_; }
    template <typename T> void setTextColor(const T& fg) { fg_ = q888(fg); bg_ = -1; }
    template <typename T, typename U> void setTextColor(const T& fg, const U& bg) { fg_ = q888(fg); uint32_t b = q888(bg); bg_ = (b == fg_) ? -1 : (int32_t)b; }
    void setTextDatum(uint8_t d) { datum_ = d; }
    void setTextDatum(textdatum_t d) { datum_ = (uint8_t)d; }
    uint8_t getTextDatum() const { return datum_; }
    void setTextWrap(bool, bool = false) {}
    void setTextPadding(uint32_t) {}
    void setCursor(int32_t x, int32_t y) { cx_ = x; cy_ = y; }
    int32_t getCursorX() const { return cx_; }
    int32_t getCursorY() const { return cy_; }

    // LGFXBase::text_width: per glyph (x_advance * (int32_t)(65536 * size_x)) >> 16, summed;
    // fontHeight/fontWidth: (int32_t)(metric * size), a float product truncated (LGFXBase.hpp).
    int32_t glyphAdvance() const { return (int32_t)((6 * (int32_t)(65536 * sx_)) >> 16); }
    int32_t fontHeight() const { return (int32_t)(8 * sy_); }
    int32_t fontWidth() const { return (int32_t)(6 * sx_); }
    int32_t textWidth(const char* s) const { return s ? (int32_t)std::strlen(s) * glyphAdvance() : 0; }
    int32_t textWidth(const String& s) const { return textWidth(s.c_str()); }
    int32_t textWidth(const std::string& s) const { return textWidth(s.c_str()); }

    int32_t drawString(const char* s, int32_t x, int32_t y) { return drawText(s ? s : "", x, y, true); }
    int32_t drawString(const String& s, int32_t x, int32_t y) { return drawString(s.c_str(), x, y); }
    int32_t drawString(const std::string& s, int32_t x, int32_t y) { return drawString(s.c_str(), x, y); }
    int32_t drawCentreString(const char* s, int32_t x, int32_t y) { uint8_t d = datum_; datum_ = top_center; int32_t w = drawString(s, x, y); datum_ = d; return w; }
    int32_t drawCenterString(const char* s, int32_t x, int32_t y) { return drawCentreString(s, x, y); }
    int32_t drawRightString(const char* s, int32_t x, int32_t y) { uint8_t d = datum_; datum_ = top_right; int32_t w = drawString(s, x, y); datum_ = d; return w; }
    int32_t drawNumber(long v, int32_t x, int32_t y) { return drawString(String(v), x, y); }
    size_t print(const char* s) { int32_t w = drawText(s ? s : "", cx_, cy_, false); cx_ += w; return s ? std::strlen(s) : 0; }
    size_t print(const String& s) { return print(s.c_str()); }
    size_t println(const char* s = "") { print(s); cx_ = 0; cy_ += fontHeight(); return 0; }
    size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    void startWrite() {}
    void endWrite() {}
    void setClipRect(int32_t, int32_t, int32_t, int32_t) {}
    void clearClipRect() {}
    void setRotation(uint8_t) {}
    void setSwapBytes(bool) {}
    void setColorDepth(int d) { depth_ = d; }
    void setColorDepth(color_depth_t d) { depth_ = (int)d; }
    int getColorDepth() const { return depth_; }

    // Raster content: counted, and drawn as a placeholder the size of the image.
    template <typename T> void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const T*) { dl_.imageCalls++; dl_.op({OP_IMAGE, 0x202020, (float)x, (float)y, (float)w, (float)h}); }
    bool drawJpg(const uint8_t*, uint32_t, int32_t = 0, int32_t = 0, int32_t = 0, int32_t = 0, int32_t = 0, int32_t = 0, float = 1.0f, float = 0.0f, int = 0) { return false; }
    bool drawPng(const uint8_t*, uint32_t, int32_t = 0, int32_t = 0) { return false; }

  protected:
    int32_t w_ = 0, h_ = 0;
    int depth_ = 16;
    DisplayList dl_;
    const IFont* font_ = nullptr;
    float sx_ = 1, sy_ = 1;
    uint32_t fg_ = 0xFFFFFF;
    int32_t bg_ = -1;
    uint8_t datum_ = top_left;
    int32_t cx_ = 0, cy_ = 0;

    // The colour as the panel shows it: by type to 888, then through the sprite's depth.
    template <typename T> uint32_t q888(const T& c) const
    {
        uint32_t v = to888(c);
        if (depth_ == 16) return expand565(color565(v >> 16, (v >> 8) & 0xFF, v & 0xFF));
        if (depth_ == 8) return expand332(color332(v >> 16, (v >> 8) & 0xFF, v & 0xFF));
        return v;
    }
    template <typename T> float q(const T& c) const { return (float)q888(c); }

    int32_t drawText(const char* s, int32_t x, int32_t y, bool useDatum)
    {
        const int32_t w = textWidth(s), h = fontHeight();
        if (useDatum) {
            if (datum_ & 1) x -= w / 2;
            else if (datum_ & 2) x -= w;
            if (datum_ & 16) y -= (int32_t)((7 * (int32_t)(65536 * sy_)) >> 16);   // Font0 baseline 7
            else if (datum_ & 4) y -= h / 2;
            else if (datum_ & 8) y -= h;
        }
        const size_t n = std::strlen(s);
        if (n) {
            const size_t off = dl_.text.size();
            dl_.text.append(s, n);
            dl_.op({OP_TEXT, (float)fg_, (float)bg_, (float)x, (float)y, sx_, sy_, (float)off, (float)n});
        }
        return w;
    }
};

// ---- the device, its panel parts, and the sprite --------------------------------------------
struct Bus_SPI {
    struct config_t { int spi_host = 0, freq_write = 0, freq_read = 0, pin_miso = -1, pin_mosi = -1, pin_sclk = -1, pin_dc = -1,
                      pin_io0 = -1, pin_io1 = -1, pin_io2 = -1, pin_io3 = -1, spi_mode = 0, dma_channel = 0;
                      bool spi_3wire = false, use_lock = false; };
    config_t c;
    config_t config() const { return c; }
    void config(const config_t& x) { c = x; }
};
struct Light_PWM {
    struct config_t { int pin_bl = -1, freq = 0, pwm_channel = 0; bool invert = false; };
    config_t c;
    config_t config() const { return c; }
    void config(const config_t& x) { c = x; }
};
struct Touch_CST816S : public ITouch {
    struct config_t { int x_min = 0, x_max = 0, y_min = 0, y_max = 0, pin_int = -1, pin_rst = -1, offset_rotation = 0,
                      i2c_port = 0, pin_sda = -1, pin_scl = -1, i2c_addr = 0, freq = 0; bool bus_shared = false; };
    config_t c;
    config_t config() const { return c; }
    void config(const config_t& x) { c = x; }
};
struct Panel_GC9A01 {
    struct config_t { int pin_cs = -1, pin_rst = -1, pin_busy = -1, panel_width = 0, panel_height = 0, offset_x = 0,
                      offset_y = 0, offset_rotation = 0, dummy_read_pixel = 0, dummy_read_bits = 0, memory_width = 0,
                      memory_height = 0; bool readable = false, invert = false, rgb_order = false, dlen_16bit = false,
                      bus_shared = false; };
    config_t c;
    config_t config() const { return c; }
    void config(const config_t& x) { c = x; }
    void setBus(Bus_SPI*) {}
    void setLight(Light_PWM*) {}
    void setTouch(ITouch*) {}
};

class LGFX_Device : public LGFXBase
{
  public:
    void setPanel(Panel_GC9A01*) {}
    bool init();                                  // web/web_gfx.cpp: the size comes from variant::SCREEN_SIZE
    bool begin() { return init(); }
    void setBrightness(uint8_t b);                // remembered; the page dims the disc by it
    uint8_t getBrightness() const { return brightness_; }
    bool getTouch(int32_t* x, int32_t* y);        // web/web_gfx.cpp: the page's pointer, in panel px
    template <typename T> bool getTouch(T* x, T* y) { int32_t a = 0, b = 0; bool t = getTouch(&a, &b); if (t) { *x = (T)a; *y = (T)b; } return t; }
    ITouch* touch() { return &touch_; }
    void sleep() {}
    void wakeup() {}
    void display() {}
    void waitDisplay() {}

  protected:
    uint8_t brightness_ = 255;
    Touch_CST816S touch_;
};

class LGFX_Sprite : public LGFXBase
{
  public:
    LGFX_Sprite() {}
    explicit LGFX_Sprite(LGFXBase* parent) : parent_(parent) {}
    void setPsram(bool) {}
    void* createSprite(int32_t w, int32_t h) { w_ = w; h_ = h; created_ = true; return this; }
    void deleteSprite() { created_ = false; w_ = h_ = 0; dl_.clear(); }
    void* getBuffer() const { return created_ ? (void*)this : nullptr; }
    bool isCreated() const { return created_; }
    // To the parent device: this frame is done (web/web_gfx.cpp hands the list to the page).
    void pushSprite(int32_t x, int32_t y);
    // Onto another sprite: its commands composited at (x, y), as LovyanGFX blits the pixels.
    void pushSprite(LGFX_Sprite* dst, int32_t x, int32_t y);

  private:
    LGFXBase* parent_ = nullptr;
    bool created_ = false;
};

// The touch chip on its I2C bus, as TouchWatchdog probes it: always present and awake, answering the
// CST816T's chip id (0xB6) at 0xA7. So the watchdog reads a healthy chip and never "recovers" it.
namespace i2c {
    template <typename T> struct result_t {
        bool ok; T v;
        bool has_value() const { return ok; }
        T value() const { return v; }
        explicit operator bool() const { return ok; }
    };
    inline result_t<uint8_t> readRegister8(int, int, uint8_t reg, uint32_t = 400000) { return {true, (uint8_t)(reg == 0xA7 ? 0xB6 : 0)}; }
    inline result_t<bool> writeRegister8(int, int, uint8_t, uint8_t, uint8_t = 0, uint32_t = 400000) { return {true, true}; }
    inline result_t<bool> readRegister(int, int, uint8_t, uint8_t* data, size_t len, uint32_t = 400000) { if (data) std::memset(data, 0, len); return {true, true}; }
    inline result_t<bool> writeRegister(int, int, uint8_t, const uint8_t*, size_t, uint32_t = 400000) { return {true, true}; }
    inline result_t<bool> transactionWrite(int, int, const uint8_t*, uint32_t, uint32_t = 400000) { return {true, true}; }
    inline result_t<bool> transactionRead(int, int, uint8_t* data, uint32_t len, uint32_t = 400000) { if (data) std::memset(data, 0, len); return {true, true}; }
    inline result_t<bool> init(int, int, int) { return {true, true}; }
    inline result_t<bool> release(int) { return {true, true}; }
}

} // namespace lgfx

using lgfx::LGFX_Sprite;
using lgfx::textdatum_t;
namespace fonts = lgfx::fonts;
