# Developer Environment & Build Setup Guide

This guide details how to configure a development environment for `sqlite-ext-core` across **Linux (GCC / Clang)**, **macOS (Apple Clang)**, **Windows MSYS2 (Clang / Make)**, and **Native Windows MSVC (`cl.exe` / `make.bat`)**.

---

## 1. Prerequisites Overview

| Component | Purpose | Requirement |
| :--- | :--- | :--- |
| **Git with Git LFS** | Repository version control & prebuilt binary tracking (`deps/**`) | Required |
| **MSYS2 (Clang64 / UCRT64)** | Windows POSIX environment, `clang`, `clang++`, `make`, and `go` | Recommended for POSIX / ASan testing |
| **Visual Studio 2022 / Build Tools** | Microsoft C/C++ compiler (`cl.exe`) for native Windows builds | Recommended for Windows MSVC |
| **GCC / Clang & SQLite3 dev** | Native toolchains for Linux / macOS | Standard on Linux / macOS |

---

## 2. Setting Up Git & Git LFS

`sqlite-ext-core` uses Git LFS to track pre-compiled SQLite dependencies (`deps/sqlite3/`).

```cmd
:: Install and initialize Git LFS
git lfs install

:: Clone or pull the repository with LFS files populated
git clone https://github.com/sqflyer/sqlite-ext-core.git
cd sqlite-ext-core
git lfs pull
```

---

## 3. Setting Up Windows MSYS2 (Clang, Make, Go)

MSYS2 provides a native Windows POSIX build toolchain. `sqlite-ext-core` defaults to `clang` / `clang++` with AddressSanitizer (`-fsanitize=address`) on Windows and macOS.

### Step 1: Install MSYS2
Download and install MSYS2 from [msys2.org](https://www.msys2.org/) (default path: `C:\msys64`).

### Step 2: Install Required Toolchains
Open the **MSYS2 CLANG64** or **MSYS2 MinGW64** terminal and install the compiler packages:

```bash
# Update package databases
pacman -Syu

# 1. Base Development & Make
pacman -S --needed base-devel make git

# 2. CLANG64 Toolchain & Compiler-RT (Provides full AddressSanitizer runtime)
pacman -S --needed mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-compiler-rt mingw-w64-clang-x86_64-sqlite3

# 3. Go Language Runtime (for ext_state concurrency tests)
pacman -S --needed mingw-w64-clang-x86_64-go
```

> [!TIP]
> All repository Makefiles automatically detect `/clang64/bin` and prepend it to `PATH`, ensuring that `clang++` and its AddressSanitizer dynamic runtime (`libclang_rt.asan_dynamic-x86_64.dll`) are resolved seamlessly without manual environment configuration.

### Step 3: Run Tests in MSYS2
```bash
# Clean and run all 19 test suites with AddressSanitizer active
make clean
make test

# Run individual test suites
make test-oom
make test-multi-tu
make test-time
make test-locks
make test-cpp-buffer
make test-cpp-statement
make test-cpp-extension
```

### Step 4: AddressSanitizer & Leak Checking in MSYS2

> [!NOTE]
> **Why `-fsanitize=leak` is unsupported on Windows**: Standalone LeakSanitizer relies on Linux kernel `ptrace` APIs (`unsupported option '-fsanitize=leak' for target 'x86_64-w64-windows-gnu'`). On Windows and macOS, memory debugging is performed using **AddressSanitizer (`-fsanitize=address`)** combined with SQLite's internal byte-level memory tracking (`sqlite3_memory_used()`). On Linux, both `-fsanitize=address,leak` are active simultaneously.

To compile and run any custom test under AddressSanitizer:
```bash
clang++ -std=c++11 -O1 -g -fsanitize=address -Iinclude tests/oom_safety/test_oom.cpp -lsqlite3 -o bin/test_asan.exe
./bin/test_asan.exe
```

---

## 4. Setting Up Windows MSVC (`cl.exe`)

`sqlite-ext-core` features a zero-dependency batch build runner (`make.bat`) driven by Microsoft Visual C++ with Level 4 warnings (`/W4`).

### Step 1: Install Visual Studio 2022 / Build Tools
Download [Visual Studio 2022 Community](https://visualstudio.microsoft.com/vs/community/) or **Visual Studio Build Tools**.

During installation, select:
- **Desktop development with C++**
- Under *Installation details* $\to$ *Optional*:
  - **MSVC v143 - VS 2022 C++ x64/x86 build tools**
  - **Windows 10/11 SDK**
  - **C++ AddressSanitizer** (optional, for MSVC memory sanitization)

### Step 2: Environment Discovery
`make.bat` automatically searches for `vcvarsall.bat` in standard installation directories:
- `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat`
- `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat`

If you are running in a standard **Developer Command Prompt for VS 2022** or **x64 Native Tools Command Prompt**, `cl.exe` is active in `PATH` automatically.

### Step 3: Run Tests via `make.bat`
From standard Windows `cmd.exe` or PowerShell:

```cmd
:: Clean previous build artifacts
make.bat clean

:: Run entire integration test suite
make.bat test

:: Run individual subsystem test suites
make.bat test-oom
make.bat test-multi-tu
make.bat test-time
make.bat test-locks
make.bat test-cpp-allocator
make.bat test-cpp-smart-ptr
make.bat test-cpp-value
make.bat test-cpp-udf
make.bat test-cpp-aggregate
make.bat test-cpp-statement
make.bat test-cpp-tvf
make.bat test-cpp-transaction
make.bat test-cpp-db
make.bat test-cpp-buffer
make.bat test-cpp-blob-stream
make.bat test-cpp-backup
make.bat test-cpp-vtab
make.bat test-cpp-extension
make.bat test-ext-state

:: Run turnkey extension examples
make.bat example
make.bat example-c
```

---

## 5. Setting Up Linux (Ubuntu, Debian, Fedora, Arch)

On Linux, `sqlite-ext-core` uses standard GNU/Clang toolchains and links against the system SQLite 3 development headers.

### Step 1: Install Dependencies
```bash
# Ubuntu / Debian
sudo apt update
sudo apt install -y build-essential libsqlite3-dev golang git git-lfs valgrind

# Fedora / RHEL
sudo dnf install -y gcc-c++ sqlite-devel golang git git-lfs valgrind

# Arch Linux
sudo pacman -S --needed base-devel sqlite go git git-lfs valgrind
```

### Step 2: Run Tests
```bash
# Clone & pull LFS dependencies
git clone https://github.com/sqflyer/sqlite-ext-core.git
cd sqlite-ext-core && git lfs pull

# Run complete test suite
make test

# Run individual test suites
make test-oom
make test-multi-tu
make test-time
make test-locks
```

### Step 3: Leak & Sanitizer Testing on Linux
```bash
# 1. Using Valgrind
valgrind --leak-check=full --show-leak-kinds=all ./tests/oom_safety/bin/test_oom

# 2. Using AddressSanitizer & LeakSanitizer (ASan/LSan)
g++ -std=c++11 -O1 -g -fsanitize=address,leak -Iinclude tests/oom_safety/test_oom.cpp -lsqlite3 -o bin/test_asan
./bin/test_asan
```

---

## 6. Setting Up macOS (Homebrew & Apple Clang)

On macOS, `sqlite-ext-core` compiles cleanly with Apple Clang, targeting standard POSIX threads, `clock_gettime(CLOCK_MONOTONIC)`, and `.dylib` dynamic extensions.

### Step 1: Install Dependencies via Homebrew
```bash
# Install Homebrew (if not already installed)
# /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install SQLite3, Go, and Git LFS
brew install sqlite3 go git-lfs make

# Initialize Git LFS
git lfs install
```

### Step 2: Configure Environment Paths
macOS ships with an older system SQLite. Ensure Homebrew's modern SQLite is discoverable:

```bash
# Add Homebrew SQLite to compiler include/library flags (e.g. in ~/.zshrc)
export CPATH="$(brew --prefix sqlite)/include:$CPATH"
export LIBRARY_PATH="$(brew --prefix sqlite)/lib:$LIBRARY_PATH"
```

### Step 3: Run Tests on macOS
```bash
# Run all test suites
make test

# Run turnkey extension demos
make example
make example-c
```

### Step 4: Memory Leak Detection on macOS
```bash
# 1. Using AddressSanitizer (ASan)
clang++ -std=c++11 -O1 -g -fsanitize=address -Iinclude tests/oom_safety/test_oom.cpp -lsqlite3 -o bin/test_asan
./bin/test_asan

# 2. Using macOS Native leaks tool
leaks --atExit -- ./tests/oom_safety/bin/test_oom
```

---

## 7. Summary Matrix: Multi-Platform Parity

| Feature | Linux (GCC / Clang) | macOS (Apple Clang) | Windows MSYS2 (`Makefile`) | Windows Native (`make.bat`) |
| :--- | :--- | :--- | :--- | :--- |
| **Compiler** | `g++` / `clang++` | `clang++` | `g++` / `clang++` | `cl.exe` |
| **No-Std Enforcement** | `-nostdlib++ -fno-exceptions -fno-rtti` | `-nostdlib++ -fno-exceptions -fno-rtti` | `-nostdlib++ -fno-exceptions -fno-rtti` | `/GR- /EHs-c- /NODEFAULTLIB:msvcprt.lib` |
| **Shared Lib Suffix** | `.so` | `.dylib` | `.dll` | `.dll` |
| **Clocks** | `clock_gettime(CLOCK_MONOTONIC)` | `clock_gettime` / `mach_time` | `QueryPerformanceCounter` | `QueryPerformanceCounter` |
| **Locks & Mutexes** | `pthread_rwlock_t` | `pthread_rwlock_t` | `SRWLOCK` | `SRWLOCK` |
| **Memory Sanitizers** | Valgrind / ASan / LSan | ASan / `leaks` | `CLANG64` ASan | MSVC ASan (`/fsanitize=address`) |
