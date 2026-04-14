# Cross-Platform Roadmap: Linux & macOS Support

## 1. Objective
Currently, LoMo Phase 1 is built with a strong dependency on the Windows Win32 API and native compression. The objective of this roadmap is to decouple the core storage and query engine from the OS, enabling high-performance operation on **Linux (Ubuntu/CentOS)** and **macOS (Apple Silicon/Intel)**.

---

## 2. Key Porting Challenges

### 2.1. Native Compression (XPRESS)
*   **Current State:** Uses Windows `compressapi.h` (XPRESS).
*   **Target:** Migrate to **ZSTD** (high ratio) and **LZ4** (high speed) for all platforms.
*   **Action:** Implement a compression abstraction layer that supports multiple backends.

### 2.2. Memory Mapping & I/O
*   **Current State:** Uses `CreateFileMapping`, `MapViewOfFile`, and `windows.h`.
*   **Target:** Standardize on **POSIX `mmap`** and `fcntl`.
*   **Optimization:** Implement **`io_uring`** for Linux to achieve asynchronous I/O performance parity (or superiority) with Windows IOCP.

### 2.3. SIMD Vectorization
*   **Current State:** Optimized for AVX2 (x86). Preliminary NEON support in `simd_filter.c`.
*   **Target:** Full optimization for **ARM NEON** (Apple Silicon M1/M2/M3) and **AVX-512** for modern Linux servers.
*   **Action:** Use a header-only SIMD abstraction library or intrinsics-based wrappers for cross-architecture compatibility.

---

## 3. Implementation Phases

### Phase A: Core Abstraction Layer (HAL)
*   **File System:** Abstract file handles and directory operations into a platform-agnostic `LomoFile` API.
*   **Memory:** Wrap memory mapping and aligned allocation (`_aligned_malloc` vs `posix_memalign`).
*   **Threading:** Standardize on `std::jthread` and C++20 synchronization primitives.

### Phase B: Linux High-Performance Core
*   **Stroage:** Implement the storage engine using `O_DIRECT` and `io_uring` for zero-copy ingestion.
*   **Deployment:** Dockerize the engine for seamless deployment in Kubernetes (K8s) environments.
*   **Benchmarking:** Target 1.5GB/s+ ingestion on Linux NVMe arrays.

### Phase C: macOS Development & Edge Support
*   **Architecture:** Optimize for ARM64 (Apple Silicon) using NEON intrinsics for all filter kernels.
*   **UI Porting:** Move from Win32 GDI/User32 to a cross-platform GUI framework (e.g., Qt or Dear ImGui) for the LoMo UI.
*   **Local Dev:** Enable developers to run the full LoMo stack on MacBooks for local data analysis.

---

## 4. Proposed Technical Stack (Cross-Platform)
- **I/O:** `io_uring` (Linux), `mmap` (macOS/POSIX).
- **Compression:** `libzstd`, `liblz4`.
- **SIMD:** `sleef` or custom intrinsics wrappers for AVX2/AVX-512/NEON.
- **Build System:** Migrate from `build_engine.bat` to **CMake** to support multi-platform builds.

---

## 5. Success Criteria
1.  **Uniform Performance:** Achievement of < 10% performance variance between Windows and Linux on identical hardware.
2.  **Binary Compatibility:** Single-source codebase that compiles on all three platforms without `#ifdef` pollution in core logic.
3.  **CI/CD Integration:** Automated builds and performance regression tests running on GitHub Actions for Windows, Ubuntu, and macOS.

---

## 6. Timeline
*   **Quarter 1:** Compression & Memory Mapping Abstraction.
*   **Quarter 2:** Linux `io_uring` Integration & Performance Tuning.
*   **Quarter 3:** macOS Apple Silicon (NEON) Optimization.
*   **Quarter 4:** Cross-Platform UI & Unified CLI Release.
