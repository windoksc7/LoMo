#ifndef LOMO_INGESTOR_H
#define LOMO_INGESTOR_H

#include "storage_engine.h"
#include "lomo_os.h"
#include <mutex>
#include <vector>
#include <atomic>
#include <thread>

#define LOMO_MEMTABLE_MAX_ROWS 1048576 // 1M rows for MVP
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
    // We'll wrap the mutex in a struct or handle for C compatibility if needed, 
    // but since we are migrating core to C++, we can use std::mutex directly if we rename ingestor.h or use opaque pointers.
    // For now, let's use a void* for the lock to keep the header semi-C-compatible for other tools.
    void* lock_handle;            
} LomoMemTable;

LomoMemTable* lomo_init_memtable(uint32_t column_count, const LomoColumnType* types);
int lomo_ingest_row(LomoMemTable* mt, const void** column_data, const size_t* column_sizes);
int lomo_flush_memtable(LomoMemTable* mt, const char* directory_path);
void lomo_free_memtable(LomoMemTable* mt);

// Phase 6: Snapshot System
int lomo_save_memtable_snapshot(const LomoMemTable* mt, const char* file_path);
LomoMemTable* lomo_load_memtable_snapshot(const char* file_path);

#ifdef __cplusplus
}
#endif

#endif // LOMO_INGESTOR_H
