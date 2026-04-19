#ifndef LOMO_INGESTOR_H
#define LOMO_INGESTOR_H

#include "storage_engine.h"
#include "lomo_os.h"
#include <mutex>
#include <vector>
#include <atomic>
#include <thread>

#define LOMO_MEMTABLE_MAX_ROWS 2097152 // 2M rows: The optimal Sweet Spot
#define LOMO_FLUSH_THRESHOLD_MB 256

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t column_count;
    LomoColumnType* types;
    void** column_buffers;     // Array of buffers (one per column)
    size_t* column_sizes;      // Current used size (in bytes) of each column buffer
    size_t* column_capacities; // Allocated capacity (in bytes) of each column buffer
    uint32_t row_count;
    uint32_t max_rows;
    void* lock_handle;            
} LomoMemTable;

LomoMemTable* lomo_init_memtable(uint32_t column_count, const LomoColumnType* types);
int lomo_ingest_row(LomoMemTable* mt, const void** column_data, const size_t* column_sizes);
int lomo_ingest_row_fast(LomoMemTable* mt, const void** column_data, const size_t* column_sizes);
int lomo_flush_memtable(LomoMemTable* mt, const char* directory_path);
void lomo_free_memtable(LomoMemTable* mt);

// --- Phase 2: Ingestion Pipeline ---

typedef struct LomoIngestPipeline LomoIngestPipeline;

/**
 * @brief Initializes a multi-threaded ingestion pipeline.
 * @param num_flusher_threads Number of background flusher threads to spawn.
 * @param directory_path Path where flushed parts will be stored.
 * @return Handle to the initialized pipeline.
 */
LomoIngestPipeline* lomo_init_pipeline(int num_flusher_threads, const char* directory_path, uint32_t column_count, const LomoColumnType* types);

/**
 * @brief Submits a full MemTable to the background flusher queue.
 */
void lomo_pipeline_submit(LomoIngestPipeline* lp, LomoMemTable* mt);

/**
 * @brief Shuts down the pipeline, waiting for all background flushes to complete.
 */
void lomo_shutdown_pipeline(LomoIngestPipeline* lp);

// Phase 6: Snapshot System
int lomo_save_memtable_snapshot(const LomoMemTable* mt, const char* file_path);
LomoMemTable* lomo_load_memtable_snapshot(const char* file_path);

#ifdef __cplusplus
}
#endif

#endif // LOMO_INGESTOR_H
