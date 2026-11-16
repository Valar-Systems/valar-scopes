/* placed in .text_hi by fixture.ld; the relax case moves it across the call8 limit */
int hi_callee(int x) { return x * 9 + 4; }
