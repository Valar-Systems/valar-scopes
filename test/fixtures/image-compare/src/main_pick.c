/* the PICK case's root: two objects each carry a COMDAT copy of inl(). */
extern int fa(int);
extern int fb(int);
volatile int g_out;
void entry(void) { g_out = fa(1) + fb(2); }
