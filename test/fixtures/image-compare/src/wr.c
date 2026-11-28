/* wrap (amendment 1j): lo_w's call names hw, an undefined reference, so the link's --wrap=hw sends it to __wrap_hw */
extern int hw(int);
int lo_w(int x) { return hw(x) + 1; }
