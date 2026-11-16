/* offset, B: a member inserted ahead of b moves its offset; get's source is unchanged */
struct S { int a; int x; int b; };
int get(const struct S *p) { return p->b; }
