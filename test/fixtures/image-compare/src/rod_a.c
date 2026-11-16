/* rodata, A: two functions whose struct constants share the object's unsplit .rodata */
struct cfg { int a, b, c, d, e, f, g, h; };
extern void use_cfg(const struct cfg *);
void f1(void) { struct cfg c = {10000, 2, 3, 4, 5, 6, 7, 8}; use_cfg(&c); }
void f2(void) { struct cfg c = {9, 10, 11, 12, 13, 14, 15, 16}; use_cfg(&c); }
