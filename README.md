# LoMo (Unified Industrial Data Platform) - Phase 1 MVP

LoMo is a high-speed, unified industrial data platform designed for extreme ingestion and analytical performance. Phase 1 (MVP) focuses on a foundational **High-Speed Log Analytics Engine** that leverages modern hardware primitives (SIMD, Columnar Storage, Native Compression) to process massive telemetry streams.

## 🔍 Why LoMo? (LoMo vs. Traditional RDBMS)

LoMo is not a replacement for traditional RDBMS (like MySQL or PostgreSQL). It is a specialized engine designed to solve the **"Industrial Data Bottleneck"** where data volume and velocity exceed the limits of row-oriented databases.

| Feature | Traditional RDBMS (e.g., MySQL) | **LoMo (Unified Platform)** |
| :--- | :--- | :--- |
| **Storage Model** | Row-oriented (Horizontal) | **Column-oriented (Vertical)** |
| **Write Strategy** | B-Tree (Real-time index updates) | **MergeTree (Buffered & Sorted Flush)** |
| **Execution Model** | Row-at-a-time processing | **Vectorized (SIMD) processing** |
| **Compression** | Low (2:1 ~ 3:1) | **Extreme (5:1 to 1400:1+)** |
| **Data Throughput** | Low to Medium (Index-bound) | **Ultra-High (1GB/s+ Target)** |
| **Primary Use Case** | Transactions (OLTP) | **Big Data & AI Analytics (OLAP)** |

### Key Architectural Shifts:
1. **Columnar Storage:** Unlike RDBMS which reads entire rows, LoMo reads only the columns required for a query. This reduces I/O by orders of magnitude during large-scale analytical scans.
2. **MergeTree Engine:** Traditional databases struggle with high-velocity writes due to index overhead. LoMo buffers data in memory and flushes it in sorted "parts," which are merged in the background, enabling sustained 1GB/s+ ingestion.
3. **Vectorized SIMD Execution:** LoMo utilizes **SIMD (Single Instruction, Multiple Data)** to process blocks of data simultaneously, allowing it to scan billions of rows in milliseconds.
4. **AI-Ready Architecture:** While RDBMS requires complex ETL to move data to AI platforms, LoMo integrates a **Feature Store** directly into the storage layer (Phase 3), ensuring zero-latency data supply for ML models.

---

## 🛠 Platform Dependencies & Architecture
LoMo Phase 1 is currently optimized for the following environment:

- **OS:** Windows (Win32 API)
- **APIs:** 
  - `compressapi.h` (Windows Native XPRESS Compression)
  - `windows.h` (Memory Mapping, Performance Counters)
- **Hardware:**
  - x86 (AVX2 support required for SIMD filtering)
  - ARM64 (NEON support for basic vectorized matches)
- **Compiler:** MSVC (Microsoft Visual C++ Compiler)

---

## ⚠️ Technical Disclaimer (Phase 1 Limits)
As an MVP (Phase 1), the following limitations exist and are planned for optimization in Phase 2:

1. **32-bit (x86) Architecture:** Currently compiled as 32-bit. This limits the engine to <2GB memory mapping per process. Datasets exceeding this will fail to map.
2. **Single-Threaded Ingestion:** Parsing and flushing are currently single-threaded. Ingestion speed is CPU-bound (approx. 120MB/s), while the target for Phase 2 is 1GB/s+.
3. **Limited SQL Support:** Only basic aggregations (`SUM`, `COUNT`) and single-column filters (`>`, `CONTAINS`) are supported.
4. **Platform Binding:** Strong dependency on Windows-specific compression and I/O APIs. Linux/POSIX support is not yet implemented.

---

## 📁 Project Structure
- `src/`: Core UI and shared logic.
- `src/engine/`: The high-performance storage and query engine.
  - `storage_engine.c`: Columnar part management and header I/O.
  - `ingestor.c`: MemTable management and sort-on-flush logic.
  - `simd_filter.c`: Hardware-accelerated scan and filter kernels.
  - `sql_parser.c`: SQL parsing and execution plan logic.

---

## 🔨 Build & Run Instructions

### Prerequisites
- Windows OS
- Visual Studio with C++ Build Tools (MSVC)

### Building the Engine
1. Open a **Developer Command Prompt for VS**.
2. Navigate to `src/engine/`.
3. Run the build script:
   ```cmd
   build_engine.bat
   ```

### Running the CLI
Execute `lomo_cli.exe` to interact with the engine:
```cmd
lomo_cli.exe
```
Inside the CLI, you can generate test data and query it:
```sql
lomo> gen 1000000
lomo> status
lomo> query SELECT COUNT(*) FROM 'test_part_dir' WHERE 1 > 500
```

---

## 🛤 Roadmap & Strategy
LoMo is evolving rapidly beyond the initial MVP. For detailed technical and strategic plans, refer to:

- **[Phase 2 MVP Scope](mvp_phase2.md):** Parallel ingestion, 64-bit migration, and Industrial OT/IT integration.
- **[Cross-Platform Roadmap](cross_platform.md):** Porting strategy for Linux (io_uring) and macOS (Apple Silicon).
- **[Phase 1 MVP Scope](mvp.md):** Objectives and performance gates for the current version.

### Key Upcoming Milestones:
1. **Parallel Ingestion:** Multi-threaded parsing and flushing to reach 1GB/s.
2. **64-bit (x64) Migration:** Support for large-scale datasets (>100GB).
3. **Full Part Compression:** Extending XPRESS/ZSTD compression to all columns.
4. **Industrial AI:** Integrating a Feature Store with "Time Travel" queries for AI training.

---

## 📄 Strategic Documentation
For a deeper understanding of the market positioning and product requirements:
- **[MRD (Market Requirements Document)](mrd.md):** Market opportunity, segments, and competitive landscape.
- **[PRD (Product Requirements Document)](prd.md):** Detailed functional roadmap and success metrics.

---

## License
This project is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0)**. 

LoMo is committed to the open-source community. The AGPL-3.0 ensures that all improvements to the core engine remain open and accessible, especially when the software is used over a network (closing the "SaaS loophole").

This software is licensed under AGPLv3. If you require commercial usage or need to keep your source code private, please contact us for separate licensing terms.

For more details, please see the [LICENSE](LICENSE) file.
