// src/engine/storage_engine.h
#ifndef LOMO_STORAGE_ENGINE_H
#define LOMO_STORAGE_ENGINE_H

#include <stdint.h>
#include <stddef.h>

// 1. Magic Number & Version
#define LOMO_MAGIC 0x4F4D4F4C // "LOMO" (Little-endian)
#define LOMO_VERSION 1

// 2. Column Types
typedef enum {
    LOMO_TYPE_UNKNOWN = 0,
    LOMO_TYPE_INT64 = 1,
    LOMO_TYPE_STRING = 2,
    LOMO_TYPE_ENUM = 3,
    LOMO_TYPE_TIMESTAMP = 4
} LomoColumnType;

typedef enum {
    LOMO_COMPRESS_NONE = 0,
    LOMO_COMPRESS_XPRESS = 1 
} LomoCompressionType;

// 3. Column Metadata
typedef struct {
    uint32_t column_id;
    uint32_t schema_version;  // Phase 6: Schema Evolution support
    LomoColumnType type;
    LomoCompressionType compression;
    uint64_t offset;          
    uint64_t uncompressed_size;
    uint64_t compressed_size; 
    uint8_t has_nulls;        
} LomoColumnMeta;

// 4. Sparse Index for fast skipping
// Layout: Flattened array [Column 0 Granules][Column 1 Granules]...
typedef struct {
    uint64_t min_timestamp;   // Range info (Primary index only, or redundant for others)
    uint64_t max_timestamp;
    uint64_t start_offset;    // Physical byte offset in its specific .col file
    uint32_t compressed_size; // Size on disk (uncompressed_size if no compression)
    uint32_t uncompressed_size;
    uint32_t row_count;       
} LomoSparseIndexGranule;

// 5. Part Header
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t total_rows;
    uint32_t column_count;
    LomoColumnMeta* columns;          // Array of column metadata
     
    uint32_t granule_count;
    LomoSparseIndexGranule* index;    // Sparse index array
    char* directory_path;             // Path to the part directory
} LomoPartHeader;

// --- Abstract API ---

#ifdef __cplusplus
extern "C" {
#endif

// Initialize a new part for writing (MemTable phase)
LomoPartHeader* lomo_init_part(uint32_t column_count);

// Append a block of data to a specific column
// This will be used during the ingestion phase before sorting/flushing.
int lomo_write_column_chunk(LomoPartHeader* part, uint32_t column_id, const void* data_ptr, size_t size);

// Read a block of column data into an AVX2/SIMD aligned buffer
int lomo_read_column_chunk_simd(const LomoPartHeader* part, uint32_t column_id, uint64_t granule_idx, void* aligned_buffer, size_t read_size);

// Read the entire column into an AVX2/SIMD aligned buffer
int lomo_read_column_simd(const LomoPartHeader* part, uint32_t column_id, void* aligned_buffer, size_t buffer_size);

// Flush the MemTable to disk, applying ZSTD/LZ4 compression, creating sparse indexes, and writing the header.
int lomo_flush_part(LomoPartHeader* part, const char* directory_path, void** column_buffers, size_t* column_sizes);
#define LOMO_GRANULE_ROWS 8192 // 8192 for production throughput

// Load a part header from a directory
LomoPartHeader* lomo_load_part(const char* directory_path);

// Filter granules based on a timestamp range
uint32_t* lomo_filter_granules_by_time(const LomoPartHeader* part, uint64_t min_ts, uint64_t max_ts, uint32_t* out_count);

// Free resources associated with the part header
void lomo_free_part(LomoPartHeader* part);

#ifdef __cplusplus
}
#endif
#endif // LOMO_STORAGE_ENGINE_H