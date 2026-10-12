/* itoa/utoa, which WString.cpp calls and newlib (the ESP32's libc) provides.
 * glibc has neither; MinGW's msvcrt has itoa but not utoa. Same contract as
 * newlib's: base 2..36, lower-case digits, a '-' only for a negative base-10 int. */
#include <stddef.h>

static char* to_base(unsigned long v, int neg, char* s, int base)
{
    char tmp[34];
    int i = 0;
    if (base < 2 || base > 36) { s[0] = '\0'; return s; }
    do { int d = (int)(v % (unsigned)base); tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= (unsigned)base; } while (v);
    int k = 0;
    if (neg) s[k++] = '-';
    while (i) s[k++] = tmp[--i];
    s[k] = '\0';
    return s;
}

char* utoa(unsigned int value, char* s, int base) { return to_base(value, 0, s, base); }

#if !defined(_WIN32)
char* itoa(int value, char* s, int base)
{
    if (base == 10 && value < 0) return to_base((unsigned long)(-(long)value), 1, s, base);
    return to_base((unsigned int)value, 0, s, base);
}
#endif
