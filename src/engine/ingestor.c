#include "ingestor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h> // Required for _aligned_malloc on Windows

// Global for qsort comparison (simplified for MVP)
static const uint64_t* g_sort_timestamps = NULL;

static int compare_indices(const void* a, const void* b) {
    uint32_t idx_a = *(const uint32_t*)a;
    uint32_t idx_b = *(const uint32_t*)b;
    if (g_sort_timestamps[idx_a] < g_sort_timestamps[idx_b]) return -1;
    if (g_sort_timestamps[idx_a] > g_sort_timestamps[idx_b]) return 1;
    return 0;
}

LomoMemTable* lomo_init_memtable(uint32_t column_count, const LomoColumnType* types) {
    LomoMemTable* mt = (LomoMemTable*)malloc(sizeof(LomoMemTable));
    if (!mt) return NULL;

    mt->column_count = column_count;
    mt->row_count = 0;
    mt->max_rows = LOMO_MEMTABLE_MAX_ROWS;

    mt->types = (LomoColumnType*)malloc(column_count * sizeof(LomoColumnType));
    memcpy(mt->types, types, column_count * sizeof(LomoColumnType));

    mt->column_buffers = (void**)malloc(column_count * sizeof(void*));
    mt->column_sizes = (size_t*)calloc(column_count, sizeof(size_t));
    mt->column_capacities = (size_t*)calloc(column_count, sizeof(size_t));

    for (uint32_t i = 0; i < column_count; i++) {
        // Initial allocation: 1MB per column (will expand as needed)
        // 32-byte alignment for AVX2 optimization
        mt->column_capacities[i] = 1024 * 1024;
        mt->column_buffers[i] = _aligned_malloc(mt->column_capacities[i], 32);
    }

    return mt;
}

int lomo_ingest_row(LomoMemTable* mt, const void** column_data, const size_t* column_sizes) {
    if (!mt || mt->row_count >= mt->max_rows) return -1;

    size_t total_usage = 0;
    for (uint32_t i = 0; i < mt->column_count; i++) {
        size_t size = column_sizes[i];
        
        // For STRING type, we need to store the 8-byte offset + the actual string data.
        // In this MVP, for simplicity in MemTable, we store [uint64_t offset][string_data] sequentially
        // but we will separate them during flush.
        size_t storage_size = (mt->types[i] == LOMO_TYPE_STRING) ? (size + sizeof(uint64_t)) : size;

        // Expand buffer if needed
        if (mt->column_sizes[i] + storage_size > mt->column_capacities[i]) {
            size_t new_cap = mt->column_capacities[i] * 2;
            while (mt->column_sizes[i] + storage_size > new_cap) new_cap *= 2;
            mt->column_buffers[i] = _aligned_realloc(mt->column_buffers[i], new_cap, 32);
            mt->column_capacities[i] = new_cap;
        }

        uint8_t* dst = (uint8_t*)mt->column_buffers[i] + mt->column_sizes[i];
        if (mt->types[i] == LOMO_TYPE_STRING) {
            uint64_t offset = (uint64_t)size; // Store length/offset info
            memcpy(dst, &offset, sizeof(uint64_t));
            memcpy(dst + sizeof(uint64_t), column_data[i], size);
        } else {
            memcpy(dst, column_data[i], size);
        }

        mt->column_sizes[i] += storage_size;
        total_usage += mt->column_sizes[i];
    }

    mt->row_count++;
    if (total_usage > (size_t)LOMO_FLUSH_THRESHOLD_MB * 1024 * 1024) return 1;
    return 0;
}

int lomo_flush_memtable(LomoMemTable* mt, const char* directory_path) {
    if (!mt || mt->row_count == 0) return 0;

    printf("[LoMo Ingestor] Sorting %u rows (including var-length strings) before flush...\n", mt->row_count);

    uint32_t* indices = (uint32_t*)malloc(mt->row_count * sizeof(uint32_t));
    for (uint32_t i = 0; i < mt->row_count; i++) indices[i] = i;

    g_sort_timestamps = (const uint64_t*)mt->column_buffers[0]; 
    qsort(indices, mt->row_count, sizeof(uint32_t), compare_indices);

    // Physical Reordering with Var-length Support
    for (uint32_t i = 0; i < mt->column_count; i++) {
        void* sorted_buffer = _aligned_malloc(mt->column_capacities[i], 32);
        size_t current_offset = 0;

        if (mt->types[i] == LOMO_TYPE_STRING) {
            // For strings, we need to find the N-th string's position in the unsorted buffer
            // This requires a pre-pass or storing internal pointers.
            // Simplified for MVP: Scan to find original offsets
            size_t* orig_offsets = (size_t*)malloc(mt->row_count * sizeof(size_t));
            size_t* orig_lengths = (size_t*)malloc(mt->row_count * sizeof(size_t));
            size_t scan_off = 0;
            for(uint32_t r=0; r<mt->row_count; r++) {
                orig_offsets[r] = scan_off;
                orig_lengths[r] = *(uint64_t*)((uint8_t*)mt->column_buffers[i] + scan_off);
                scan_off += (sizeof(uint64_t) + orig_lengths[r]);
            }

            for (uint32_t j = 0; j < mt->row_count; j++) {
                uint32_t original_idx = indices[j];
                size_t len = orig_lengths[original_idx];
                size_t off = orig_offsets[original_idx];
                
                memcpy((uint8_t*)sorted_buffer + current_offset, (uint8_t*)mt->column_buffers[i] + off, sizeof(uint64_t) + len);
                current_offset += (sizeof(uint64_t) + len);
            }
            free(orig_offsets);
            free(orig_lengths);
        } else {
            size_t row_size = mt->column_sizes[i] / mt->row_count;
            for (uint32_t j = 0; j < mt->row_count; j++) {
                uint32_t original_idx = indices[j];
                memcpy((uint8_t*)sorted_buffer + (j * row_size),
                       (uint8_t*)mt->column_buffers[i] + (original_idx * row_size),
                       row_size);
            }
        }

        _aligned_free(mt->column_buffers[i]);
        mt->column_buffers[i] = sorted_buffer;
    }

    LomoPartHeader* part = lomo_init_part(mt->column_count);
    if (!part) { free(indices); return -1; }
    part->total_rows = mt->row_count;

    int result = lomo_flush_part(part, directory_path, mt->column_buffers, mt->column_sizes);
    
    lomo_free_part(part);
    free(indices);
    for (uint32_t i = 0; i < mt->column_count; i++) mt->column_sizes[i] = 0;
    mt->row_count = 0;
    return result;
}

void lomo_free_memtable(LomoMemTable* mt) {
    if (!mt) return;
    for (uint32_t i = 0; i < mt->column_count; i++) {
        _aligned_free(mt->column_buffers[i]);
    }
    free(mt->column_buffers);
    free(mt->column_sizes);
    free(mt->column_capacities);
    free(mt->types);
    free(mt);
}
