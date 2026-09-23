#include "UsbOpen.h"

#if defined(FEATURE_USB_OPEN)

#if ARDUINO_USB_MODE
#error "FEATURE_USB_OPEN needs ARDUINO_USB_MODE=0 (TinyUSB): the USB-Serial/JTAG peripheral cannot present a HID keyboard"
#endif

#include "USB.h"
#include "USBHIDKeyboard.h"

namespace {
// A GLOBAL, on purpose: its constructor registers the HID interface with TinyUSB
// before main() starts USB for the CDC serial (ARDUINO_USB_CDC_ON_BOOT), so the
// device enumerates once, as CDC + keyboard. Constructed any later, the keyboard
// would be missing from the descriptors until a re-enumeration.
USBHIDKeyboard keyboard;

enum class Step : uint8_t { Idle, Chord, Wait, Type, Enter };
Step step = Step::Idle;
usbopen::Os os = usbopen::Os::Windows;
String url;
unsigned typed = 0;
unsigned long stepAt = 0;
const unsigned CHARS_PER_PUMP = 8;   // ~8 x 2 HID reports per loop pass
}

namespace usbopen {

void Begin()
{
    keyboard.begin();
    USB.begin();   // a no-op when CDC-on-boot already started it; harmless otherwise
    Serial.println("[usb-open] HID keyboard ready (composite CDC + HID)");
}

bool Busy() { return step != Step::Idle; }

bool Request(Os o, const String& u)
{
    if (Busy() || u.length() == 0 || u.length() > MAX_TYPED) return false;
    os = o;
    url = u;
    typed = 0;
    step = Step::Chord;
    Serial.printf("[usb-open] typing %u chars for os=%u\n", (unsigned)url.length(), (unsigned)os);
    return true;
}

void Pump()
{
    const unsigned long now = millis();
    switch (step) {
        case Step::Idle:
            return;
        case Step::Chord: {
            const Chord c = ChordFor(os);
            keyboard.press(c.mod == Mod::Gui ? KEY_LEFT_GUI : KEY_LEFT_ALT);
            if (c.fKey) keyboard.press(KEY_F1 + (c.fNum - 1));
            else keyboard.press(c.key);
            delay(30);
            keyboard.releaseAll();
            stepAt = now;
            step = Step::Wait;
            return;
        }
        case Step::Wait:
            if (now - stepAt >= WaitFor(os)) step = Step::Type;
            return;
        case Step::Type: {
            for (unsigned i = 0; i < CHARS_PER_PUMP && typed < url.length() && typed < MAX_TYPED; ++i)
                keyboard.write((uint8_t)url[typed++]);
            if (typed >= url.length() || typed >= MAX_TYPED) { stepAt = now; step = Step::Enter; }
            return;
        }
        case Step::Enter:
            if (now - stepAt < 50) return;
            keyboard.write(KEY_RETURN);
            keyboard.releaseAll();
            Serial.println("[usb-open] done");
            step = Step::Idle;
            url = String();
            return;
    }
}

} // namespace usbopen

#endif // FEATURE_USB_OPEN
