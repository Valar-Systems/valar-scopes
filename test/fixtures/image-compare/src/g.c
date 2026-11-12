extern const char *sink(const char *);
int g(int x) { return sink(x ? "hello world" : "other")[0]; }
