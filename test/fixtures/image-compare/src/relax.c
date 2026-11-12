extern int hi_callee(int);
int lo_caller(int x) { return hi_callee(x) + 2; }
