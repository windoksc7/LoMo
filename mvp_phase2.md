# MVP Scope: LoMo Phase 2 (Industrial OT/IT Integration)

## 1. Objective
Phase 2 aims to evolve the LoMo engine from a high-speed log analyzer into a **Real-Time Industrial Yield Predictor**. The primary goal is to achieve 1GB/s+ parallel ingestion while enabling sub-second alignment (Join) between high-frequency sensor data (OT) and manufacturing execution systems (IT).

---

## 2. Core Functional Features ("Must-Haves")

### 2.1. Parallel & Distributed Ingestion
*   **Multi-threaded Ingestion:** Implement a parallel parsing and flushing pipeline to remove single-core CPU bottlenecks.
*   **64-bit (x64) Engine:** Full migration to 64-bit architecture to handle memory mapping for datasets exceeding 2GB.
*   **SEMI Standard Connectors:** Native support for **EDA (Freeze 3 / gRPC)** and **SECS/GEM** protocols for direct equipment communication.

### 2.2. Advanced Storage Engine (Enhanced MergeTree)
*   **Full Column Compression:** Extend Windows Native **XPRESS** (or ZSTD/LZ4) compression to all data columns, not just timestamps.
*   **Per-Column Granule Indexing:** Store physical offsets for every column granule to enable selective reading and skipping.
*   **Background Part Merging:** Implement a multi-threaded merge worker to consolidate small "parts" into large, highly-compressed segments without blocking ingestion.

### 2.3. Real-Time Contextualization (Industrial Joins)
*   **Streaming Join Engine:** Sub-second "Join" operations between high-frequency OT trace data and slow-moving IT (MES) recipe data.
*   **Runtime Filtering:** Use Bloom filters or Min/Max indexes to accelerate complex multi-table joins.
*   **Point-in-Time Queries:** Ensure data consistency across different sensor streams with varying sampling rates.

### 2.4. Predictive Control & ML Integration
*   **Process Drift Detection:** Real-time ML modules to identify micro-anomalies in equipment sensor data (e.g., temperature, vibration).
*   **Prescriptive Setpoints:** Capability to calculate and recommend optimal equipment setpoints based on real-time yield prediction models.

---

## 3. Technical Stack (Phase 2)
*   **Architecture:** x64 (64-bit) for large-scale memory addressing.
*   **Concurrency:** `std::jthread` and lock-free queues for parallel ingestion.
*   **Serialization:** `gRPC` and `Protocol Buffers` for SEMI EDA Freeze 3 integration.
*   **Acceleration:** Enhanced SIMD kernels (AVX-512 support where available) for faster join operations.

---

## 4. Out of Scope (Phase 3)
*   **Native Feature Store:** Full-scale AI feature management and "Time Travel" training.
*   **Global Multi-Region Sync:** Distributed cluster synchronization across different geographic locations.

---

## 5. Success Criteria & Benchmarks
To move to Phase 3, the engine must pass the following "Industrial Performance Gate":
1.  **Parallel Throughput:** Maintain **1.0 GB/s sustained ingestion** across multiple cores/threads.
2.  **Join Latency:** Complete a Join between 1 million OT rows and 10,000 IT rows in **< 200ms**.
3.  **Storage Efficiency:** Achieve **10:1+ average compression ratio** across all columns for semiconductor sensor data.
4.  **Stability:** 99.9% uptime during 24/7 high-flow ingestion testing.

---

## 6. Phase 2 Milestone Roadmap
1.  **Week 1-4:** 64-bit Migration & Multi-threaded Ingestion Pipeline.
2.  **Week 5-8:** EDA/SECS Connectors & Full Column Compression.
3.  **Week 9-12:** Real-Time Join Engine & Industrial Data Alignment.
4.  **Week 13-14:** ML-based Drift Detection & Prescriptive Analytics Integration.
