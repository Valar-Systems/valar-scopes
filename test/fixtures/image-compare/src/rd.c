/* rodread: rd and rk read another object's read-only data (tbl changes in B, ktbl never does) */
extern const int tbl[4];
extern const int ktbl[4];
int rd(int i) { return tbl[i & 3]; }
int rk(int i) { return ktbl[i & 3]; }
