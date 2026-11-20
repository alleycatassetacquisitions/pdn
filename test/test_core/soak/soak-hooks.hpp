#pragma once

// Link-time hooks for the soak harness. Include from exactly one translation unit
// per test binary: it defines extern "C" symbols.

#include <cstdint>
#include <random>

namespace soak {

// While set, every std::random_device draw comes from the seeded stream instead of
// the OS, so match ids (IdGenerator) and ReliableChannel seqIds replay from the seed.
// The WIN/LOSE animations seed static generators once per process, so a seed replays
// exactly in a fresh process; a later run of it in the same process may differ.
inline bool entropySeeded = false;
inline std::mt19937 entropyStream;

/** Routes std::random_device to a stream seeded from @p seed until releaseEntropy(). */
inline void seedEntropy(uint64_t seed) {
    entropyStream.seed(static_cast<uint32_t>(seed ^ (seed >> 32)));
    entropySeeded = true;
}

/** Returns std::random_device to the OS source, so later tests see real entropy. */
inline void releaseEntropy() { entropySeeded = false; }

}  // namespace soak

// Built with -Wl,--wrap=_ZNSt13random_device9_M_getvalEv: libstdc++'s inline
// random_device::operator() calls the out-of-line _M_getval, which the linker sends here.
extern "C" unsigned int __real__ZNSt13random_device9_M_getvalEv(void* self);

extern "C" unsigned int __wrap__ZNSt13random_device9_M_getvalEv(void* self) {
    if (soak::entropySeeded) return static_cast<unsigned int>(soak::entropyStream());
    return __real__ZNSt13random_device9_M_getvalEv(self);
}

// TEMPORARY: delete this hook once the PDN/FDN destructor fix lands (#541). PDN::~PDN
// and FDN::~FDN leak the managers their constructors allocate (src/pdn/device/pdn.cpp:10,
// src/fdn/device/fdn.cpp:10), so each deleted device reports them; scoped to those
// two constructors only.
extern "C" const char* __lsan_default_suppressions() { return "leak:PDN::PDN\nleak:FDN::FDN\n"; }
