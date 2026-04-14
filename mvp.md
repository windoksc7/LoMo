# MVP Scope: LoMo Phase 1 (High-Speed Log Analytics)

## 1. Objective
The Phase 1 MVP aims to demonstrate a 10x performance-per-dollar improvement over legacy log analyzers by achieving **1GB/s ingestion on a single commodity node** while maintaining sub-second analytical query performance on multi-terabyte datasets.

---

## 2. Core Functional Features ("Must-Haves")

### 2.1. High-Flow Ingestion Layer
*   **Protocols:** Support for `Syslog (UDP/TCP)` and `JSON over HTTP/gRPC`.
*   **Zero-Copy Buffer:** Implementation of a ring-buffer in memory to decouple ingestion from disk I/O.
*   **Schema Discovery:** Automatic inference of basic JSON structures to enable immediate columnar mapping.

### 2.2. Storage Engine (C++ Core)
*   **MergeTree Architecture:** Data is written in sorted parts and merged in the background.
*   **Columnar Format:** Storage optimized for "scans" rather than "point lookups" (e.g., LZ4/ZSTD compression).
*   **Index Granularity:** Sparse primary indexing to allow fast skips of irrelevant data blocks.

### 2.3. Query & Analytics
*   **SQL-Lite Interface:** Basic `SELECT`, `WHERE`, `GROUP BY`, and `ORDER BY` support.
*   **Vectorized Execution:** Engine must process data in blocks (e.g., 65,536 rows at a time) using SIMD instructions.
*   **Live Tail:** Capability to query data that is still in the memory buffer (not yet flushed to disk).

### 2.4. Minimal UI/Dashboard
*   **Health Metrics:** Real-time visualization of Ingestion Rate (GB/s), CPU Load, and Disk I/O.
*   **Query Console:** A simple CLI or Web interface to execute SQL and view results in tabular/line-chart format.

---

## 3. Technical MVP Stack
*   **Language:** C++20 for the core engine (performance & memory safety).
*   **SIMD Library:** `SIMDJSON` for ultra-fast log parsing.
*   **Networking:** `Asio` or `io_uring` for high-concurrency network I/O.
*   **Compression:** `LZ4` for high-speed ingestion; `ZSTD` for cold storage.

---

## 4. Out of Scope (Phase 2/3)
*   **Advanced Joins:** Multi-table joins (reserved for Semiconductor data alignment).
*   **ML Integration:** Native predictive modules.
*   **Cloud Multi-tenancy:** Advanced RBAC and billing (MVP is single-instance/cluster).

---

## 5. Success Criteria & Benchmarks
To move to Phase 2, the MVP must pass the following "Performance Gate":
1.  **Sustained Ingestion:** 1.0 GB/s raw log ingestion on a single 16-core, 32GB RAM node with NVMe.
2.  **Analytical Speed:** `SELECT count(*) FROM logs WHERE level='ERROR'` on 1TB of data in < 500ms.
3.  **Compression Ratio:** Achieve at least 5:1 compression on standard server logs.

---

## 6. MVP Milestone Roadmap
1.  **Week 1-4:** Core Ingestion & Ring-Buffer (C++).
2.  **Week 5-8:** Columnar MergeTree & Background Merging.
3.  **Week 9-10:** SQL Parser & Vectorized Execution Engine.
4.  **Week 11-12:** Benchmarking & Performance Optimization.
