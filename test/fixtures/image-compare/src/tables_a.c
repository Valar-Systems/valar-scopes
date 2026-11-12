/* strpool, A: a file-scope table; its strings sit in the object's shared .rodata.str1.1 */
const char *const t1[] = {"alpha", "beta"};
int use(int i) { return t1[i][0]; }
