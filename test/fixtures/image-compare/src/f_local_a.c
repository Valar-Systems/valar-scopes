static __attribute__((noinline)) int helper(int x) { return x * 7 + 300; }
int f(int x) { return helper(x) + 1; }
