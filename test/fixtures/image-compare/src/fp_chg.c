/* fillchg (1f): fp.c with one real change (y << 4 -> y << 5), assembled as fill/b is */
extern int ext(int);
int pick(int x, int y) {
  int r;
  switch (x) {
    case 0: r = ext(y) + 1; break;
    case 1: r = ext(y * 3) - 7; break;
    case 2: r = y << 5; break;
    case 3: r = ext(y ^ 5); break;
    default: r = -1; break;
  }
  while (r > 100) r = ext(r) - 3;
  return r;
}
