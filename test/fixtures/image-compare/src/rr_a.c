/* rodread (amendment 1i), A: tbl and ktbl global, ltbl static and read by rl. B changes tbl's and ltbl's last word */
const int tbl[4] = {1, 2, 3, 4};
const int ktbl[4] = {9, 8, 7, 6};
static const int ltbl[4] = {1, 1, 2, 3};
int rl(int i) { return ltbl[i & 3]; }
