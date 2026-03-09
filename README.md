## SAST Tools

[PVS-Studio](https://pvs-studio.com/en/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) - static analyzer for C, C++, C#, and Java code.

# Vitality

Vitality is a text editor written in C++20 and Qt 6. The current milestone is a read-only file viewer with a custom text surface, cursor navigation, and a status bar.

This project is just getting started and is not usable. Come back later.

## Prerequisites

- CMake 3.24 or newer
- A C++20 compiler
- Qt 6
- ICU
- HarfBuzz

On this machine, Homebrew Qt 6 is installed at `/opt/homebrew/opt/qt`.

## Configure

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
```

If Qt is already discoverable in your environment, the `-DCMAKE_PREFIX_PATH` argument may not be necessary.

### Stage 3 Unicode dependencies

Stage 3 adds explicit ICU and HarfBuzz dependencies. On this machine, the expected Homebrew setup is:

```bash
brew install qt icu4c harfbuzz
```

Vitality now checks that Qt and HarfBuzz come from the same package source on macOS/Homebrew. A mismatched Qt/HarfBuzz combination will fail CMake configuration with a clear error instead of producing a subtly inconsistent runtime.

### macOS and CLion

If you use CLion on macOS, prefer Apple's default toolchain:

- C compiler: `/usr/bin/clang`
- C++ compiler: `/usr/bin/clang++`

Using Homebrew LLVM in CLion can produce a compiler/runtime mismatch where CMake configures successfully but linking fails later with missing `libc++` symbols such as `std::__1::__hash_memory`.

## Build

```bash
cmake --build build
```

Catch2 and RapidCheck are fetched automatically by CMake during configuration.

## Run

Launch with an empty buffer:

```bash
./build/vitality
```

Launch with a file:

```bash
./build/vitality path/to/file.txt
```

## Run All Tests

```bash
ctest --test-dir build --output-on-failure
```
