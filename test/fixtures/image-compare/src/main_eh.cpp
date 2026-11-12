// the eh cases' root, and stubs for what -fexceptions code links against (the fixtures link -nostdlib)
extern void handle(int);
void may_throw(int) {}
void note(int) {}
extern "C" {
  void entry(void) { handle(1); }
  void __gxx_personality_v0(void) {}
  void _Unwind_Resume(void) {}
  void *__cxa_begin_catch(void *) { return 0; }
  void __cxa_end_catch(void) {}
}
extern const void *const cti_vtable[4] __asm__("_ZTVN10__cxxabiv117__class_type_infoE");
const void *const cti_vtable[4] = {0, 0, 0, 0};
