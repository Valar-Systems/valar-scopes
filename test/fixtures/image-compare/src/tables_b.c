/* strpool, B: a new table whose string GCC emits ahead of t1's in the shared pool; t1 itself is unchanged */
const char *const t1[] = {"alpha", "beta"};
const char *const t0[] = {"zero"};
int use(int i) { return t1[i][0] + t0[0][0]; }
