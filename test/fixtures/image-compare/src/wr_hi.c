/* wrap (amendment 1j): the real hw, and its wrapper in .text.hi_wrap -- far from lo_w (relaxed in A, kept in B). The
   wrapper's own call names __real_hw, which the link resolves to hw and the comparator does not model (a named limit) */
int hw(int x) { return x * 7; }
extern int __real_hw(int);
__attribute__((section(".text.hi_wrap"))) int __wrap_hw(int x) { return __real_hw(x) + 2; }
