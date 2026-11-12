extern int ext(int);
extern int h(int);
int f(int x) { return ext(x) * 3 + 10000 + h(x); }
