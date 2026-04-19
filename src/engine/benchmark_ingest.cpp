#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ingestor.h"
#include "storage_engine.h"
#include "merger.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
double get_time() {
    LARGE_INTEGER frequency;
    LARGE_INTEGER start;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    return (double)start.QuadPart / frequency.QuadPart;
}
#else
#include <sys/time.h>
double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}
#endif

void create_dummy_it_part(uint64_t base_ts) {
    printf("[LoMo Benchmark] Creating dummy IT (MES) part with 100 intervals...\n");
    LomoColumnType it_types[] = { LOMO_TYPE_INT64, LOMO_TYPE_INT64, LOMO_TYPE_TIMESTAMP, LOMO_TYPE_TIMESTAMP };
    LomoMemTable* mt = lomo_init_memtable(4, it_types);
    
    for (uint32_t i = 0; i < 100; i++) {
        int64_t recipe_id = i;
        int64_t lot_id = 100 + (i / 10);
        uint64_t start_ts = base_ts + (i * 1000);
        uint64_t end_ts = start_ts + 800;
        
        const void* data[] = { &recipe_id, &lot_id, &start_ts, &end_ts };
        size_t sizes[] = { 8, 8, 8, 8 };
        lomo_ingest_row(mt, data, sizes);
    }
    
    printf("[LoMo Benchmark] Flushing IT memtable...\n");
    lomo_flush_memtable(mt, "it_part_dummy");
    printf("[LoMo Benchmark] IT flush done.\n");
    lomo_free_memtable(mt);
}

int main() {
    const char* filename = "dummy_web.log";
    uint64_t base_ts = (uint64_t)time(NULL);

    // 1. Setup Background Merger Daemon
    const char* pipeline_dir = "lomo_pipeline_parts";
    const char* merged_dir = "merged_parts_benchmark";
    LomoMergerDaemon* daemon = lomo_start_merger_daemon(pipeline_dir, merged_dir);

    // 2. Setup Ingestion Pipeline
    LomoColumnType types[] = { LOMO_TYPE_TIMESTAMP, LOMO_TYPE_STRING, LOMO_TYPE_INT64 };
    LomoIngestPipeline* lp = lomo_init_pipeline(4, pipeline_dir, 3, types);

    uint32_t target_rows = 5000000; 
    uint32_t rows_per_part = 500000; // Trigger more frequent flushes for daemon to pick up

    LomoMemTable* mt = lomo_init_memtable(3, types);

    char line[512];
    uint64_t count = 0;
    uint64_t total_bytes = 0;

    printf("[LoMo Benchmark] Starting Pipeline Ingestion (5M rows, 4 flusher threads)...\n");
    
    double start_total = get_time();

    while (count < target_rows) {
        FILE* fp = fopen(filename, "r");
        if (!fp) {
            printf("Failed to open %s\n", filename);
            break;
        }
        while (fgets(line, sizeof(line), fp) && count < target_rows) {
            size_t len = strlen(line);
            total_bytes += len;
            
            uint64_t ts = base_ts + count; 
            int64_t status = 200; 
            
            const void* data[] = { &ts, line, &status };
            size_t sizes[] = { 8, len, 8 };
            
            lomo_ingest_row(mt, data, sizes);
            count++;

            if (mt->row_count >= rows_per_part) {
                lomo_pipeline_submit(lp, mt);
                mt = lomo_init_memtable(3, types);
                printf("[LoMo Benchmark] Submitted part (%llu rows total)...\n", count);
            }
        }
        fclose(fp);
    }

    if (mt->row_count > 0) lomo_pipeline_submit(lp, mt);
    else lomo_free_memtable(mt);

    // 3. Shutdown Pipeline (wait for flushes)
    lomo_shutdown_pipeline(lp);

    // 4. Wait a bit for daemon to pick up last parts
    printf("[LoMo Benchmark] Waiting for background merger to finish consolidation...\n");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    lomo_stop_merger_daemon(daemon);

    double end_total = get_time();
    double elapsed_total = end_total - start_total;

    printf("[LoMo Benchmark] Ingestion complete. TOTAL TIME: %.4f sec\n", elapsed_total);
    return 0;
}
