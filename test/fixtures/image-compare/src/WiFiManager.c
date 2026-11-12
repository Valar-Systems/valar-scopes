/* T0's fixture: the two stamped strings, as GCC writes __DATE__ " " __TIME__ under SOURCE_DATE_EPOCH. */
const char *about(int k) { return k ? "Jan  1 2026 00:00:00" : "built Jan  1 2026 00:00:00"; }
