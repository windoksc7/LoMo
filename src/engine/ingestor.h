#ifndef LOMO_INGESTOR_H
#define LOMO_INGESTOR_H

#include "storage_engine.h"
#include "lomo_os.h"

#define LOMO_MEMTABLE_MAX_ROWS 1048576 // 1M rows for MVP
#define LOMO_FLUSH_THRESHOLD_MB 256

typedef struct {
    uint32_t column_count;
    LomoColumnType* types;
    void** column_buffers;     // Array of buffers (one per column)
    size_t* column_sizes;      // Current used size (in bytes) of each column buffer
    size_t* column_capacities; // Allocated capacity (in bytes) of each column buffer
    uint32_t row_count;
    uint32_t max_rows;
    LomoMutex lock;            // Phase 6: Mutex-guarded MemTable
} LomoMemTable;

// Initialize a new MemTable
LomoMemTable* lomo_init_memtable(uint32_t column_count, const LomoColumnType* types);

// Add a single row of data to the MemTable
int lomo_ingest_row(LomoMemTable* mt, const void** column_data, const size_t* column_sizes);

// Sort the MemTable by the primary column (timestamp) and flush it to disk
int lomo_flush_memtable(LomoMemTable* mt, const char* directory_path);

// Free all resources used by the MemTable
void lomo_free_memtable(LomoMemTable* mt);

// Phase 6: Snapshot System
int lomo_save_memtable_snapshot(const LomoMemTable* mt, const char* file_path);
LomoMemTable* lomo_load_memtable_snapshot(const char* file_path);

#endif // LOMO_INGESTOR_H
