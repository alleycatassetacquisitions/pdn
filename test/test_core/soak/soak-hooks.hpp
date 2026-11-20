#pragma once

// Link-time hooks for the soak harness. Include from exactly one translation unit
// per test binary: it defines extern "C" symbols.

// TEMPORARY: delete this hook once the PDN destructor fix lands (#541). PDN::~PDN
// leaks the light, serial, wireless and coordinator managers (src/pdn/device/pdn.cpp:10),
// so each deleted PDN reports them; scoped to that constructor only.
extern "C" const char* __lsan_default_suppressions() { return "leak:PDN::PDN\n"; }
