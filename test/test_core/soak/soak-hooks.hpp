#pragma once

// Link-time hooks for the soak harness. Include from exactly one translation unit
// per test binary: it defines extern "C" symbols.

// TEMPORARY: delete this hook once the PDN/FDN destructor fix lands (#541). PDN::~PDN
// and FDN::~FDN leak the managers their constructors allocate (src/pdn/device/pdn.cpp:10,
// src/fdn/device/fdn.cpp:10), so each deleted device reports them; scoped to those
// two constructors only.
extern "C" const char* __lsan_default_suppressions() { return "leak:PDN::PDN\nleak:FDN::FDN\n"; }
