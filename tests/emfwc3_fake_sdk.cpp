// An isolated C-ABI SDK double. It is built into its own test directory only.
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>

#ifdef _WIN32
#define API extern "C" __declspec(dllexport)
#else
#define API extern "C"
#endif
namespace
{
std::mutex mutex;
std::array<double, 3> positions{};
std::array<int, 3> stops{};
std::atomic<int> readFailure{0}, commandFailure{0}, motionCount{0}, saves{0}, closes{0}, deinits{0};
std::atomic<bool> nonfinite{false};
int speed = 100;
bool opened = false;
}
API void testReset()
{
    std::lock_guard lock(mutex);
    positions = {0, 45, 90}; stops = {}; speed = 100; opened = false;
    readFailure = 0; commandFailure = 0; motionCount = 0; saves = 0; closes = 0; deinits = 0; nonfinite = false;
}
API void testFailRead(int code) { readFailure = code; }
API void testFailCommand(int code) { commandFailure = code; }
API void testNonfinite(int value) { nonfinite = value != 0; }
API int testMotionCount() { return motionCount; }
API int testSaves() { return saves; }
API int testCloses() { return closes; }
API int testDeinits() { return deinits; }
API int testStops(int channel) { std::lock_guard lock(mutex); return stops.at(channel); }
API int libraryInit() { return 0; }
API void libraryDeInit() { ++deinits; }
API int COMMON_openSerialDevice(const char*, const char*) { std::lock_guard lock(mutex); opened = true; return 0; }
API void COMMON_closeDevice(const char*) { std::lock_guard lock(mutex); opened = false; ++closes; }
API int EMFWC3_SetMotorType(const char*, int count, const char**) { return count == 3 ? 0 : -1000; }
API int EMFWC3_GetPosition(const char*, double *pos, char channel)
{
    std::lock_guard lock(mutex);
    if (!opened || readFailure) return readFailure ? readFailure.load() : -1;
    *pos = nonfinite ? std::numeric_limits<double>::quiet_NaN() : positions.at(channel); return 0;
}
API int EMFWC3_GetSpeed(const char*, int *value) { std::lock_guard lock(mutex); *value = speed; return 0; }
API int EMFWC3_SetSpeed(const char*, int value) { std::lock_guard lock(mutex); if (commandFailure) return commandFailure; speed = value; return 0; }
API int EMFWC3_Run(const char*, double angle, char channel)
{
    std::lock_guard lock(mutex); if (commandFailure) return commandFailure;
    positions.at(channel) += angle; ++motionCount; return 0;
}
API int EMFWC3_RunPosition(const char*, double position, char channel)
{
    std::lock_guard lock(mutex); if (commandFailure) return commandFailure;
    positions.at(channel) = position; ++motionCount; return 0;
}
API int EMFWC3_Reset(const char*, char channel)
{
    std::lock_guard lock(mutex); if (commandFailure) return commandFailure;
    positions.at(channel) = 0; ++motionCount; return 0;
}
API int EMFWC3_RunContinue(const char*, int direction, char channel)
{
    std::lock_guard lock(mutex);
    if (direction == 0) ++stops.at(channel); else if (!commandFailure) ++motionCount;
    return commandFailure;
}
API int EMFWC3_SavePosition(const char*) { if (commandFailure) return commandFailure; ++saves; return 0; }
