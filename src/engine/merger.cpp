#include "merger.h"
#include "ingestor.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "lomo_os.h"

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

// Simplified Merger for Phase 1: Loads all source data into memory and re-flushes.
// This ensures consistency with storage_engine's compression and indexing.
int lomo_merge_parts(const char** part_paths, uint32_t part_count, const char* output_path) {
    if (part_count == 0) return 0;
    mkdir(output_path, 0777);

    printf("[LoMo Merger] Merging %u parts into %s...\n", part_count, output_path);

    uint32_t column_count = 0;
    LomoColumnType* types = NULL;
    uint64_t total_rows = 0;

    // 1. Load all parts and calculate total capacity
    LomoPartHeader** headers = (LomoPartHeader**)malloc(part_count * sizeof(LomoPartHeader*));
    for (uint32_t p = 0; p < part_count; p++) {
        headers[p] = lomo_load_part(part_paths[p]);
        if (!headers[p]) return -1;
        if (p == 0) {
            column_count = headers[p]->column_count;
            types = (LomoColumnType*)malloc(column_count * sizeof(LomoColumnType));
            for(uint32_t c=0; c<column_count; c++) types[c] = headers[p]->columns[c].type;
        }
        total_rows += headers[p]->total_rows;
    }

    // 2. Initialize a MemTable to hold all merged data
    LomoMemTable* mt = lomo_init_memtable(column_count, types);
    mt->max_rows = (uint32_t)total_rows;

    // 3. For each part, read all data and ingest into MemTable
    for (uint32_t p = 0; p < part_count; p++) {
        LomoPartHeader* h = headers[p];
        
        // Temporary buffers to hold entire columns of the current part
        void** p_bufs = (void**)malloc(column_count * sizeof(void*));
        size_t* p_sizes = (size_t*)malloc(column_count * sizeof(size_t));

        for (uint32_t c = 0; c < column_count; c++) {
            p_sizes[c] = (size_t)h->columns[c].uncompressed_size;
            p_bufs[c] = lomo_aligned_malloc(p_sizes[c], 4096);
            
            // Read entire column (handles decompression for primary column)
            lomo_read_column_simd(h, c, p_bufs[c], p_sizes[c]);
        }

        // Ingest row by row from this part into the global MemTable
        uint32_t* col_offsets = (uint32_t*)calloc(column_count, sizeof(uint32_t));
        for (uint32_t r = 0; r < h->total_rows; r++) {
            const void* row_data[32]; // Max 32 cols for MVP
            size_t row_sizes[32];

            for (uint32_t c = 0; c < column_count; c++) {
                if (types[c] == LOMO_TYPE_STRING) {
                    uint64_t len = *(uint64_t*)((uint8_t*)p_bufs[c] + col_offsets[c]);
                    row_data[c] = (uint8_t*)p_bufs[c] + col_offsets[c] + sizeof(uint64_t);
                    row_sizes[c] = (size_t)len;
                    col_offsets[c] += (uint32_t)(sizeof(uint64_t) + len);
                } else {
                    size_t sz = p_sizes[c] / h->total_rows;
                    row_data[c] = (uint8_t*)p_bufs[c] + (r * sz);
                    row_sizes[c] = sz;
                }
            }
            lomo_ingest_row(mt, row_data, row_sizes);
        }

        // Clean up part buffers
        for (uint32_t c = 0; c < column_count; c++) lomo_aligned_free(p_bufs[c]);
        free(p_bufs);
        free(p_sizes);
        free(col_offsets);
        lomo_free_part(h);
    }

    // 4. Flush the combined MemTable (this will sort and compress automatically)
    int result = lomo_flush_memtable(mt, output_path);

    // Final cleanup
    lomo_free_memtable(mt);
    free(headers);
    free(types);

    return result;
}
