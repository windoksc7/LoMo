#include "lomo_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include "storage_engine.h"
#include "simd_filter.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static double get_time() {
    LARGE_INTEGER frequency;
    LARGE_INTEGER start;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    return (double)start.QuadPart / frequency.QuadPart;
}
#else
#include <sys/time.h>
static double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}
#endif

// --- Industrial Join Engine (Phase 2: Sweep-Line Interval Join) ---

typedef struct {
    uint64_t start;
    uint64_t end;
    uint32_t recipe_id;
} ITInterval;

bool compare_intervals(const ITInterval& a, const ITInterval& b) {
    return a.start < b.start;
}

/**
 * @brief High-precision Point-in-Time Join between OT and IT streams.
 * OT: [Timestamp, ...] - Chronologically sorted.
 * IT: [RecipeID, ..., StartTS, EndTS] - Intervals.
 * 
 * Target SLA: < 200ms for 1M OT / 10K IT.
 */
void lomo_join_ot_it(const char* ot_path, const char* it_path) {
    LomoPartHeader* ot_part = lomo_load_part(ot_path);
    LomoPartHeader* it_part = lomo_load_part(it_path);
    if (!ot_part || !it_part) {
        if (ot_part) lomo_free_part(ot_part);
        if (it_part) lomo_free_part(it_part);
        return;
    }

    printf("[LoMo Join] Precision Interval Join: OT (%llu rows) vs IT (%llu intervals)...\n", 
           (unsigned long long)ot_part->total_rows, (unsigned long long)it_part->total_rows);

    double start_time = get_time();

    // 1. Build Phase: Load and sort IT intervals
    std::vector<ITInterval> intervals(it_part->total_rows);
    
    uint64_t* it_start = (uint64_t*)lomo_aligned_malloc(it_part->total_rows * 8, 4096);
    uint64_t* it_end = (uint64_t*)lomo_aligned_malloc(it_part->total_rows * 8, 4096);
    lomo_read_column_simd(it_part, 2, it_start, it_part->total_rows * 8);
    lomo_read_column_simd(it_part, 3, it_end, it_part->total_rows * 8);

    for (uint32_t i = 0; i < it_part->total_rows; i++) {
        intervals[i] = { it_start[i], it_end[i], i };
    }
    std::sort(intervals.begin(), intervals.end(), compare_intervals);

    // 2. Probe Phase: Sweep-Line (Linear Scan)
    uint64_t* ot_ts = (uint64_t*)lomo_aligned_malloc(ot_part->total_rows * 8, 4096);
    lomo_read_column_simd(ot_part, 0, ot_ts, ot_part->total_rows * 8);

    uint32_t matched = 0;
    uint32_t it_ptr = 0;
    uint32_t num_intervals = (uint32_t)intervals.size();

    for (uint32_t i = 0; i < ot_part->total_rows; i++) {
        uint64_t current_ot_ts = ot_ts[i];

        // Advance IT pointer if the current interval has ended before this OT timestamp
        while (it_ptr < num_intervals && current_ot_ts > intervals[it_ptr].end) {
            it_ptr++;
        }

        // Check if OT falls within current IT interval
        if (it_ptr < num_intervals && current_ot_ts >= intervals[it_ptr].start) {
            matched++;
            // In a real implementation, we would output the IT context here
        }
    }

    double end_time = get_time();
    printf("[LoMo Join] Precise matches found: %u rows.\n", matched);
    printf("[LoMo Join] Execution Time: %.4f ms (SLA < 200ms)\n", (end_time - start_time) * 1000.0);

    lomo_aligned_free(it_start);
    lomo_aligned_free(it_end);
    lomo_aligned_free(ot_ts);
    lomo_free_part(ot_part);
    lomo_free_part(it_part);
}

void run_vector_query(const char* part_dir) {
    LomoPartHeader* part = lomo_load_part(part_dir);
    if (!part) {
        printf("Failed to load part from %s\n", part_dir);
        return;
    }

    uint64_t part_min_ts = (part->granule_count > 0) ? part->index[0].min_timestamp : 0;
    uint64_t query_min = part_min_ts + 20;
    uint64_t query_max = part_min_ts + 40;

    uint32_t matched_granule_count = 0;
    uint32_t* matched_granules = lomo_filter_granules_by_time(part, query_min, query_max, &matched_granule_count);

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

    printf("[LoMo Query] Range Query Result: Found %llu rows.\n", (unsigned long long)total_matches);
    free(matched_granules);
    lomo_free_part(part);
}

int main(int argc, char** argv) {
    if (argc > 3 && strcmp(argv[1], "join") == 0) {
        lomo_join_ot_it(argv[2], argv[3]);
    } else if (argc > 1) {
        run_vector_query(argv[1]);
    } else {
        printf("Usage: query_lomo join <ot_dir> <it_dir>  OR  query_lomo <part_dir>\n");
    }
    return 0;
}
