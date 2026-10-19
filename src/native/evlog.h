// evlog.h — small persistent event log written by the hook itself.
//
// Why: logcat-based watchers (pvsurv) die with the TV's shell session and need the
// PC. This file lives in the app's own external storage
// (/sdcard/Android/data/<package>/files/pvhook_events.log), survives reboots, needs
// no permission and is readable over adb. One line per event, a few dozen bytes,
// rotated at 1 MB (pvhook_events.log.1 keeps the previous generation).
//
// It also records native crashes: a signal handler appends one line (signal, fault
// address, pc, lr, load bases) then hands the signal back to whoever was installed
// before us, so Android's own crash handling is unchanged.
#pragma once
#include <cstdint>

namespace pvev {

void init();                                   // idempotent; opens the file, logs "start"
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void install_crash_handler(uintptr_t ignite_base);

}  // namespace pvev
