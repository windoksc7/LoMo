#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <malloc.h>
#include "storage_engine.h"
#include "simd_filter.h"

void run_vector_query(const char* part_dir) {
    LomoPartHeader* part = lomo_load_part(part_dir);
    if (!part) {
        printf("Failed to load part from %s\n", part_dir);
        return;
    }

    printf("[LoMo Query] Loaded part from %s (%llu rows, %u granules)\n", part_dir, (unsigned long long)part->total_rows, part->granule_count);

    // --- Sparse Index Skip Test ---
    // Let's assume we want logs between time X and Y.
    // In our test_ingest, timestamps are time(NULL) + 0...99.
    // We'll try to query only a small middle range to see if granules are skipped.
    uint64_t part_min_ts = part->index[0].min_timestamp;
    uint64_t query_min = part_min_ts + 20;
    uint64_t query_max = part_min_ts + 40;

    uint32_t matched_granule_count = 0;
    uint32_t* matched_granules = lomo_filter_granules_by_time(part, query_min, query_max, &matched_granule_count);

    printf("[LoMo Query] Range Query: %llu to %llu\n", (unsigned long long)query_min, (unsigned long long)query_max);
    printf("[LoMo Query] Sparse Index matched %u/%u granules. (Skipped %u)\n", 
           matched_granule_count, part->granule_count, part->granule_count - matched_granule_count);

    if (matched_granule_count == 0) {
        free(matched_granules);
        lomo_free_part(part);
        return;
    }

    // Now only scan matched granules
    uint64_t total_matches = 0;
    for (uint32_t i = 0; i < matched_granule_count; i++) {
        uint32_t g_idx = matched_granules[i];
        LomoSparseIndexGranule* g = &part->index[g_idx];

        // Column 0 is Timestamp
        size_t read_size = g->row_count * sizeof(uint64_t);
        uint64_t* ts_buffer = (uint64_t*)_aligned_malloc(read_size, 32);
        
        if (lomo_read_column_chunk_simd(part, 0, g_idx, ts_buffer, read_size) == 0) {
            total_matches += lomo_simd_count_range_uint64(ts_buffer, g->row_count, query_min, query_max);
        }
        _aligned_free(ts_buffer);

        // Column 2 is String
        // NOTE: In this MVP, we read the whole column chunk and parse length-prefixed strings.
        size_t str_col_size = (size_t)part->columns[2].uncompressed_size;
        void* str_buffer = _aligned_malloc(str_col_size, 32);
        if (lomo_read_column_simd(part, 2, str_buffer, str_col_size) == 0) {
            uint8_t* ptr = (uint8_t*)str_buffer;
            for(uint32_t r=0; r < part->total_rows; r++) {
                uint64_t len = *(uint64_t*)ptr;
                ptr += sizeof(uint64_t);
                if (r < 3 && i == 0) { // Print first 3 strings of first matched granule for verification
                    printf("[LoMo Query] Row %u String: %.*s (Len: %llu)\n", r, (int)len, (char*)ptr, len);
                }
                ptr += len;
            }
        }
        _aligned_free(str_buffer);
    }

    printf("[LoMo Query] Result: Found %llu rows in specified time range.\n", (unsigned long long)total_matches);

    // --- CONTAINS Mask Test ---
    uint8_t* contains_mask = (uint8_t*)calloc((part->total_rows + 7) / 8, 1);
    size_t str_col_size = (size_t)part->columns[2].uncompressed_size;
    void* str_buffer = _aligned_malloc(str_col_size, 32);
    if (lomo_read_column_simd(part, 2, str_buffer, str_col_size) == 0) {
        uint64_t contains_hits = lomo_simd_filter_string_contains_mask((const char*)str_buffer, part->total_rows, NULL, contains_mask, "Example", 7);
        printf("[LoMo Query] CONTAINS 'Example' filter: %llu rows matched.\n", (unsigned long long)contains_hits);
    }
    _aligned_free(str_buffer);
    free(contains_mask);

    free(matched_granules);
    lomo_free_part(part);
}

int main(int argc, char** argv) {
    const char* target = (argc > 1) ? argv[1] : "test_part_dir";
    run_vector_query(target);
    return 0;
}
