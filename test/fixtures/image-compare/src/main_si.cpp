// the inittry case's root, and stubs for what -fexceptions code links against
extern int g_val;
int may_throw_i(int x) { return x; }
extern "C" {
  volatile int g_out;
  void entry(void) { g_out = g_val; }
  void __gxx_personality_v0(void) {}
  void _Unwind_Resume(void) {}
  void *__cxa_begin_catch(void *) { return 0; }
  void __cxa_end_catch(void) {}
}
