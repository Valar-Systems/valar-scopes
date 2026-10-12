// The heap ledger behind hostheap.h. Compiled WITHOUT the force-include: this is
// the one file that must call the real malloc.
//
// Every block carries a 16-byte prefix holding its requested size, so free and
// realloc can retire exactly what was added. 16, not sizeof(size_t), so the user
// pointer keeps malloc's alignment on 32- and 64-bit hosts alike. Under
// AddressSanitizer the prefix sits inside the sanitizer's own allocation, so an
// overrun past the END of a block is still caught at the first byte.
#include "hostheap.h"   // the declarations; its rename macros are undone just below

#undef malloc
#undef calloc
#undef realloc
#undef free

namespace {

constexpr size_t kPrefix = 16;
constexpr uint32_t kMagic = 0xE6A110C5u;

struct Prefix {
    size_t size;
    uint32_t magic;
};

hostheap::Totals g_now;
hostheap::Totals g_peak;
long g_allocs = 0;

void Account(size_t n, int sign)
{
    g_now.reqBytes += sign * (long)n;
    g_now.blocks += sign;
    const long dev = (long)hostheap::DeviceBytes(n);
    if (n <= hostheap::kAlwaysInternal) g_now.devInternal += sign * dev;
    else g_now.devPsram += sign * dev;
    if (sign > 0 && (long)n > g_now.biggest) g_now.biggest = (long)n;
    if (g_now.reqBytes > g_peak.reqBytes) g_peak.reqBytes = g_now.reqBytes;
    if (g_now.blocks > g_peak.blocks) g_peak.blocks = g_now.blocks;
    if (g_now.devInternal > g_peak.devInternal) g_peak.devInternal = g_now.devInternal;
    if (g_now.devPsram > g_peak.devPsram) g_peak.devPsram = g_now.devPsram;
    if (g_now.biggest > g_peak.biggest) g_peak.biggest = g_now.biggest;
}

Prefix* PrefixOf(void* user) { return reinterpret_cast<Prefix*>(static_cast<char*>(user) - kPrefix); }

}  // namespace

extern "C" void* hostheap_malloc(size_t n)
{
    char* base = static_cast<char*>(std::malloc(kPrefix + n));
    if (!base) return nullptr;
    Prefix* p = reinterpret_cast<Prefix*>(base);
    p->size = n;
    p->magic = kMagic;
    g_allocs++;
    Account(n, +1);
    return base + kPrefix;
}

extern "C" void hostheap_free(void* user)
{
    if (!user) return;
    Prefix* p = PrefixOf(user);
    if (p->magic != kMagic) std::abort();   // not ours: a ledger that guesses is not a ledger
    Account(p->size, -1);
    p->magic = 0;
    std::free(p);
}

extern "C" void* hostheap_calloc(size_t n, size_t size)
{
    void* u = hostheap_malloc(n * size);
    if (u) std::memset(u, 0, n * size);
    return u;
}

extern "C" void* hostheap_realloc(void* user, size_t n)
{
    if (!user) return hostheap_malloc(n);
    if (n == 0) { hostheap_free(user); return nullptr; }
    Prefix* p = PrefixOf(user);
    if (p->magic != kMagic) std::abort();
    const size_t old = p->size;
    char* base = static_cast<char*>(std::realloc(p, kPrefix + n));
    if (!base) return nullptr;              // the old block is still live and still counted
    Account(old, -1);
    p = reinterpret_cast<Prefix*>(base);
    p->size = n;
    g_allocs++;
    Account(n, +1);
    return base + kPrefix;
}

// THE GLOBAL operator new, so std::vector<eam::Msg> and every other container in
// the code under test lands in the same ledger. On the device operator new is
// newlib's, which is malloc.
void* operator new(size_t n)
{
    void* u = hostheap_malloc(n ? n : 1);
    if (!u) throw std::bad_alloc();
    return u;
}
void* operator new[](size_t n) { return operator new(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return hostheap_malloc(n ? n : 1); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return hostheap_malloc(n ? n : 1); }
void operator delete(void* u) noexcept { hostheap_free(u); }
void operator delete[](void* u) noexcept { hostheap_free(u); }
#if defined(__cpp_sized_deallocation)   // C++14's sized forms; run.sh builds C++11
void operator delete(void* u, size_t) noexcept { hostheap_free(u); }
void operator delete[](void* u, size_t) noexcept { hostheap_free(u); }
#endif

namespace hostheap {
Totals Now() { return g_now; }
Totals Peak() { return g_peak; }
void ResetPeak() { g_peak = g_now; }
long Allocations() { return g_allocs; }
}  // namespace hostheap
