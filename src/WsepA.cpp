// THE FRAME BENCH (REG-W-sep Corrections 3 and 4; bench/fp-port-wsep only, never merged):
// copy Wa of W's per-contact path, in flash where the linker puts it.
// Empty in every image but the separation image (FRAME_BENCH with the profile's list 12). Body: src/WsepCopy.inc.
#if defined(FRAME_BENCH) && defined(FRAME_PROFILE_GAINS) && FRAME_PROFILE_GAINS == 12
#define WSEP_NS wsepA
#include "WsepCopy.inc"
#endif
