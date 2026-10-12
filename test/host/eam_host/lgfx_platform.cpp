// The six functions LovyanGFX's framebuffer platform declares
// (lgfx/v1/platforms/framebuffer/common.hpp), for the E6 ticker-draw test.
//
// WHY NOT THE LIBRARY'S OWN common.cpp: it opens /dev/fb0 (linux/fb.h) and does
// not exist for Windows. A sprite never touches a panel, so nothing on the drawing
// path reaches these -- they only have to link. run.sh builds LovyanGFX with
// -DLGFX_LINUX_FB (and, on MinGW, -D__linux__) so the library selects that
// platform header; that is the whole extent of the pretence.
#include <lgfx/v1/platforms/common.hpp>

#include <chrono>

namespace lgfx {
inline namespace v1 {

static const auto kStart = std::chrono::steady_clock::now();

unsigned long millis(void)
{
    return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - kStart).count();
}
unsigned long micros(void)
{
    return (unsigned long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - kStart).count();
}
void delay(unsigned long) {}
void delayMicroseconds(unsigned int) {}
void gpio_hi(uint32_t) {}
void gpio_lo(uint32_t) {}
bool gpio_in(uint32_t) { return false; }
void pinMode(int_fast16_t, pin_mode_t) {}
void lgfxPinMode(int_fast16_t, pin_mode_t) {}

}  // namespace v1
}  // namespace lgfx
