/* the rodata case's root, use_cfg, and the memcpy GCC calls to copy the struct (the fixtures link -nostdlib) */
extern void f1(void);
extern void f2(void);
volatile const void *g_p;
void use_cfg(const void *p) { g_p = p; }
void *memcpy(void *d, const void *s, __SIZE_TYPE__ n) {
  volatile char *dd = d; const volatile char *ss = s;
  while (n--) *dd++ = *ss++;
  return d;
}
void entry(void) { f1(); f2(); }
