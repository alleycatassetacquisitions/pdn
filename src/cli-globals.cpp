#ifdef NATIVE_BUILD

#include <chrono>

// Debug cycling state
bool g_panelCycling = false;
int g_panelCycleInterval = 2000;
std::chrono::steady_clock::time_point g_panelCycleLastSwitch;

bool g_stateCycling = false;
int g_stateCycleDevice = -1;
int g_stateCycleInterval = 3000;
int g_stateCycleStep = 0;
std::chrono::steady_clock::time_point g_stateCycleLastSwitch;

#endif // NATIVE_BUILD
