# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test Commands

```bash
# Release build
./build.sh
# or: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

# Debug build (with gcov coverage)
./build.sh Debug

# Run server
./run.sh [/path/to/config.conf]          # defaults to src/resources/config.conf

# Run all tests (Debug build + ctest)
./test.sh

# Run a single test binary
./build/test_tcp_server
./build/test_config
./build/test_queue
./build/test_lpf
./build/test_xlating

# Run performance benchmark
./build/perf_xlating

# Generate coverage (after Debug build + test run)
cd build && gcov CMakeFiles/sdr_serverLib.dir/src/*.c.o CMakeFiles/sdr_serverLib.dir/src/sdr/*.c.o
```

System dependencies for MSi SDR support: `sudo apt install libusb-1.0-0-dev`  
All other deps: `sudo apt install librtlsdr-dev libairspy-dev libhackrf-dev libconfig-dev zlib1g-dev`

## Architecture

### Threading model

```
┌──────────────────────────┐
│  TCP server thread       │  accepts connections, parses api.h requests
│  (tcp_server.c)          │  spawns one dsp_worker per client
└────────────┬─────────────┘
             │ bounded queue (queue.c)  ← pre-allocated blocks, overwrite on full
┌────────────▼─────────────┐
│  SDR device thread       │  reads raw IQ from hardware at band_sampling_rate
│  (sdr_device.c →         │  pushes buffers into the queue
│   sdr/*_device.c)        │
└──────────────────────────┘
             │ queue
┌────────────▼─────────────┐
│  DSP worker threads      │  one per connected client (dsp_worker.c)
│                          │  applies Frequency Xlating FIR (xlating.c + lpf.c)
│                          │  decimates to client's requested sample rate
│                          │  writes to file (REQUEST_DESTINATION_FILE=0) or
│                          │  streams back via socket (REQUEST_DESTINATION_SOCKET=1)
└──────────────────────────┘
```

### Device plugin pattern

Each SDR type (0=RTL, 1=Airspy, 2=HackRF, 3=MSi SDR) follows this structure:

- `src/sdr/*_lib.h` — function-pointer struct (`rtlsdr_lib`, `airspy_lib`, etc.)
- `src/sdr/*_lib.c` — compiled into the **final executable** only; populates fn-ptrs via `dlopen` (or static assignment for MSi SDR)
- `test/*_lib_mock.c` — compiled into `sdr_serverTestLib`; replaces hardware with mock; allows unit testing without hardware
- `src/sdr/*_device.c` — compiled into `sdr_serverLib`; calls through the fn-ptr struct; never links to hardware libs directly

`sdr_device.c` routes `server_config->sdr_type` to the correct driver in two switch statements: once in `sdr_device_create` (lib init) and once in `sdr_device_start` (plugin create).

This separation means tests link `sdr_serverLib` + `sdr_serverTestLib` (mocks) while the real binary links `sdr_serverLib` + `*_lib.c` (real hardware libraries).

### Wire protocol (`src/api.h`)

Client sends: `message_header` (2 bytes) + `request` (13 bytes)  
Server replies: `message_header` (2 bytes) + `response` (5 bytes)  
- On success: `response.details` = file index (for file destination)  
- Ping: `TYPE_PING` → `TYPE_RESPONSE` success  
- Shutdown: `TYPE_SHUTDOWN` → disconnect  
All structs are packed, big-endian is not enforced (host byte order).

### MSi SDR specifics (`src/sdr/msisdr_device.c`, `third_party/libmsisdr/`)

libmsisdr is **vendored** in `third_party/libmsisdr/` (fork of libmirisdr). Unlike the other drivers it is statically linked — `msisdr_lib.c` assigns function pointers directly to linked symbols (no `dlopen`). `msisdr_read_async()` is a **blocking call** that runs in a dedicated `rx_thread`; `msisdr_cancel_async()` unblocks it from another thread. Samples arrive as CS16 (int16_t I/Q pairs), identical to Airspy — the DSP worker path is shared.

### Key config fields

- `sdr_type` — selects hardware driver (0–3)
- `band_sampling_rate` — raw USB sample rate; clients must request sub-rates that divide evenly
- `buffer_size` — bytes per queue block (262144 default; Airspy forces this value)
- `device_index` / `device_serial` — hardware selection (serial only works for RTL-SDR and HackRF)
- `gain` in config.c is stored as `int` = `(float_gain * 10)` (tenths of dB for RTL-SDR)

### Adding a new SDR device type

1. Create `src/sdr/newdev_lib.h` + `src/sdr/newdev_lib.c` (fn-ptr struct + loader)
2. Create `src/sdr/newdev_device.h` + `src/sdr/newdev_device.c` (4 functions: create/destroy/start_rx/stop_rx)
3. Create `test/newdev_lib_mock.c` + `.h`
4. Add `SDR_TYPE_NEWDEV` to `src/config.h`
5. Parse config fields in `src/config.c`
6. Add cases to both switch blocks in `src/sdr_device.c`
7. Wire up CMakeLists.txt: device.c → sdr_serverLib, lib.c → sdr_server, mock.c → sdr_serverTestLib
8. Add `test_newdev()` to `test/test_tcp_server.c` following the airspy or msisdr pattern
