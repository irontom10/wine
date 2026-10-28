# NEXIQ RP1210 / USB-Link 3 serial timeout diagnostics

This branch adds **opt-in logging only** to Wine's serial read/timeout paths. It changes no serial I/O behavior.

## Build and test

Build this branch using your existing Wine build setup. Keep the custom Wine build separate from the system package and run the *matching* Wine and wineserver from that build. Kill existing Wine processes in the Calterm prefix before switching binaries so that `wineserver` inherits the tracing environment.

Fish shell:

```fish
set -gx WINEPREFIX "$HOME/programs/calterm-364"
# Shut down only this prefix's processes first.
wineserver -k
set -gx WINE_NEXIQ_TRACE 1
env WINEDEBUG=-all /path/to/patched/wine \
  "$WINEPREFIX/drive_c/Program Files (x86)/NEXIQ/Device Tester/DeviceTester.exe" \
  &> ~/nexiq-timeout-trace.log

grep NEXIQ_SERIAL ~/nexiq-timeout-trace.log > ~/nexiq-timeouts-filtered.log
```

Make sure your Bluetooth adapter is **bound** to `/dev/rfcomm0` using `rfcomm bind`, without another process holding an `rfcomm connect` session. Retry the **J1708** connection and reproduce `ERR_HARDWARE_NOT_RESPONDING`.

## Instrumented events

- `server_set`: five raw timeout fields received by Wine's server.
- `client_timeout`: timeout fields read by the client and Wine's effective read interval/total timeouts for a request.
- `NtReadFile` / `avail` / `register`: initial serial request, availability mode and asynchronous read registration.
- `server_queue`: queued operation, byte count and calculated total timeout (ms).
- `server_interval_armed` / `server_interval_expired`: read interval timer lifecycle.
- `wake`, `unix_read`, `complete`: asynchronous read wakes, actual Unix read counts/error codes and completion.

All entries have the `NEXIQ_SERIAL` prefix on stderr. Callback tracing includes all async reads whose original buffer size is at least 255 bytes, including non-serial reads; correlate by handle with `NtReadFile`.

**Do not infer a bug solely from a 65,535-byte read:** validate the configured `COMMTIMEOUTS`, partial-read handling, and actual completion status first.

The trace logs counts and status but does not dump ECM, adapter or serial payload data.
