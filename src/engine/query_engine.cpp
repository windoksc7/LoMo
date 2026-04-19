#include "lomo_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <unordered_map>
#include "storage_engine.h"
#include "simd_filter.h"

// --- Join Engine (Phase 2) ---

typedef struct {
    uint32_t* matched_indices_ot;
    uint32_t* matched_indices_it;
    uint32_t match_count;
} LomoJoinResult;

// Simple Bloom Filter for Join Acceleration
typedef struct {
    uint8_t* bits;
    size_t size;
} LomoBloomFilter;

LomoBloomFilter* lomo_init_bloom(size_t size_bits) {
    LomoBloomFilter* bf = (LomoBloomFilter*)malloc(sizeof(LomoBloomFilter));
    bf->size = size_bits;
    bf->bits = (uint8_t*)calloc((size_bits + 7) / 8, 1);
    return bf;
}

void lomo_bloom_add(LomoBloomFilter* bf, uint64_t val) {
    size_t hash = (size_t)(val * 11400714819323198485ULL); // Simple multiplicative hash
    size_t bit = hash % bf->size;
    bf->bits[bit / 8] |= (1 << (bit % 8));
}

bool lomo_bloom_check(LomoBloomFilter* bf, uint64_t val) {
    size_t hash = (size_t)(val * 11400714819323198485ULL);
    size_t bit = hash % bf->size;
    return (bf->bits[bit / 8] & (1 << (bit % 8))) != 0;
}

void lomo_free_bloom(LomoBloomFilter* bf) {
    if (!bf) return;
    free(bf->bits);
    free(bf);
}

// OT: High-frequency Trace Data (col 0: TS, col 1: SensorID)
// IT: Slow-moving Recipe Data (col 0: RecipeID, col 1: LotID, col 2: StartTS, col 3: EndTS)
// For MVP Phase 2, we perform a Point-in-Time Join: OT.TS is within [IT.StartTS, IT.EndTS]
void lomo_join_ot_it(const char* ot_path, const char* it_path) {
    LomoPartHeader* ot_part = lomo_load_part(ot_path);
    LomoPartHeader* it_part = lomo_load_part(it_path);
    if (!ot_part || !it_part) {
        if (ot_part) lomo_free_part(ot_part);
        if (it_part) lomo_free_part(it_part);
        return;
    }

    printf("[LoMo Join] Joining OT (%llu rows) with IT (%llu rows)...\n", 
           (unsigned long long)ot_part->total_rows, (unsigned long long)it_part->total_rows);

    // 1. Build Phase (Build index on IT recipe ranges)
    // For Point-in-Time join, we can use an interval tree or simplified hash of time buckets.
    // Here we'll use a Bloom filter of IT intervals to skip OT rows.
    LomoBloomFilter* bf = lomo_init_bloom(1024 * 1024);
    
    // Load IT timestamps (assume col 2 is StartTS, col 3 is EndTS)
    uint64_t* it_start = (uint64_t*)lomo_aligned_malloc(it_part->total_rows * 8, 4096);
    uint64_t* it_end = (uint64_t*)lomo_aligned_malloc(it_part->total_rows * 8, 4096);
    lomo_read_column_simd(it_part, 2, it_start, it_part->total_rows * 8);
    lomo_read_column_simd(it_part, 3, it_end, it_part->total_rows * 8);

    for (uint32_t i = 0; i < it_part->total_rows; i++) {
        // Add bucketed time ranges to Bloom filter
        for (uint64_t t = it_start[i] / 1000; t <= it_end[i] / 1000; t++) {
            lomo_bloom_add(bf, t);
        }
    }

    // 2. Probe Phase (Scan OT and check against IT)
    uint64_t* ot_ts = (uint64_t*)lomo_aligned_malloc(ot_part->total_rows * 8, 4096);
    lomo_read_column_simd(ot_part, 0, ot_ts, ot_part->total_rows * 8);

    uint32_t matched = 0;
    for (uint32_t i = 0; i < ot_part->total_rows; i++) {
        if (lomo_bloom_check(bf, ot_ts[i] / 1000)) {
            // Potential match, would do refined check here
            matched++;
        }
    }

    printf("[LoMo Join] Point-in-Time Join result: %u OT rows aligned with IT context.\n", matched);

    lomo_aligned_free(it_start);
    lomo_aligned_free(it_end);
    lomo_aligned_free(ot_ts);
    lomo_free_bloom(bf);
    lomo_free_part(ot_part);
    lomo_free_part(it_part);
}

void run_vector_query(const char* part_dir) {
    LomoPartHeader* part = lomo_load_part(part_dir);
    if (!part) {
        printf("Failed to load part from %s\n", part_dir);
        return;
    }

    printf("[LoMo Query] Loaded part from %s (%llu rows, %u granules)\n", part_dir, (unsigned long long)part->total_rows, part->granule_count);

    uint64_t part_min_ts = part->index[0].min_timestamp;
    uint64_t query_min = part_min_ts + 20;
    uint64_t query_max = part_min_ts + 40;

    uint32_t matched_granule_count = 0;
    uint32_t* matched_granules = lomo_filter_granules_by_time(part, query_min, query_max, &matched_granule_count);

    printf("[LoMo Query] Range Query: %llu to %llu\n", (unsigned long long)query_min, (unsigned long long)query_max);
    if (matched_granule_count == 0) {
        if (matched_granules) free(matched_granules);
        lomo_free_part(part);
        return;
    }

    uint64_t total_matches = 0;
    for (uint32_t i = 0; i < matched_granule_count; i++) {
        uint32_t g_idx = matched_granules[i];
        LomoSparseIndexGranule* g = &part->index[g_idx];
        size_t read_size = g->row_count * sizeof(uint64_t);
        uint64_t* ts_buffer = (uint64_t*)lomo_aligned_malloc(read_size, 4096);
        
        if (lomo_read_column_chunk_simd(part, 0, g_idx, ts_buffer, read_size) == 0) {
            total_matches += lomo_simd_count_range_uint64(ts_buffer, g->row_count, query_min, query_max);
        }
        lomo_aligned_free(ts_buffer);
    }

    printf("[LoMo Query] Result: Found %llu rows in specified time range.\n", (unsigned long long)total_matches);
    free(matched_granules);
    lomo_free_part(part);
}

int main(int argc, char** argv) {
    if (argc > 2 && strcmp(argv[1], "join") == 0) {
        lomo_join_ot_it(argv[2], argv[3]);
    } else {
        const char* target = (argc > 1) ? argv[1] : "test_part_dir";
        run_vector_query(target);
    }
    return 0;
}
