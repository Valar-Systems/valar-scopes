/* vtab (amendment 1j): string literals first, then a table of function pointers. The four strings total 112 bytes, so
   the merged pool ends 4-aligned and .rodata.vtab starts where the map lists the later .str1.1 sections, with their
   pre-merge sizes -- the firmware's tie (gen.sh asserts it). rv reads vtab; rs reads its own string */
extern int puts_(const char *);
int s1(void) { return puts_("alpha bravo charlies"); }
int s2(void) { return puts_("delta echo foxtrot golf hotel"); }
int s3(void) { return puts_("india juliet kilo lima mike november"); }
int rs(int i) { return puts_("quebec romeo sierra tan") + i; }
int t1(int x) { return x + 1; }
int t2(int x) { return x * 3; }
typedef int (*fn)(int);
extern const fn vtab[2];
int rv(int i) { return vtab[i & 1](i); }
const fn vtab[2] = {t1, t2};
