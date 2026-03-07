# Vitality

Vitality is a text editor written in C++20 and Qt 6. The current milestone is a read-only file viewer with a custom text surface, cursor navigation, and a status bar.

## Prerequisites

- CMake 3.24 or newer
- A C++20 compiler
- Qt 6

On this machine, Homebrew Qt 6 is installed at `/opt/homebrew/opt/qt`.

## Configure

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
```

If Qt is already discoverable in your environment, the `-DCMAKE_PREFIX_PATH` argument may not be necessary.

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
