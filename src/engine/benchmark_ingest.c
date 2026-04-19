#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include "ingestor.h"
#include "storage_engine.h"

int main() {
    const char* filename = "dummy_web.log";
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        printf("Failed to open %s\n", filename);
        return 1;
    }

    LomoColumnType types[] = { LOMO_TYPE_TIMESTAMP, LOMO_TYPE_STRING, LOMO_TYPE_INT64 };
    LomoMemTable* mt = lomo_init_memtable(3, types);
    mt->max_rows = 25000000; // Increase for 1GB test

    char line[512];
    uint64_t count = 0;
    uint64_t total_bytes = 0;

    printf("[LoMo Benchmark] Starting ingestion of %s...\n", filename);
    
    LARGE_INTEGER frequency;
    LARGE_INTEGER start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        total_bytes += len;
        
        // Very basic parsing for benchmark
        // Format: [2026-04-13 12:00:00] 192.168.1.10 GET /index.html 200
        uint64_t ts = (uint64_t)time(NULL); // Simplified
        int64_t status = 200; // Simplified
        
        const void* data[] = { &ts, line, &status };
        size_t sizes[] = { 8, len, 8 };
        
        lomo_ingest_row(mt, data, sizes);
        count++;

        if (count % 1000000 == 0) {
            printf("Ingested %llu rows...\n", count);
        }
    }

    QueryPerformanceCounter(&end);
    double elapsed_ingest = (double)(end.QuadPart - start.QuadPart) / frequency.QuadPart;

    printf("[LoMo Benchmark] Ingested %llu rows in %.4f sec (Ingestion speed: %.2f MB/s)\n", 
           count, elapsed_ingest, (total_bytes / 1024.0 / 1024.0) / elapsed_ingest);

    printf("[LoMo Benchmark] Flushing to disk...\n");
    QueryPerformanceCounter(&start);
    lomo_flush_memtable(mt, "lomo_dummy_part");
    QueryPerformanceCounter(&end);
    double elapsed_flush = (double)(end.QuadPart - start.QuadPart) / frequency.QuadPart;

    printf("[LoMo Benchmark] Flush completed in %.4f sec.\n", elapsed_flush);
    printf("[LoMo Benchmark] TOTAL TIME: %.4f sec (Overall speed: %.2f MB/s)\n", 
           elapsed_ingest + elapsed_flush, (total_bytes / 1024.0 / 1024.0) / (elapsed_ingest + elapsed_flush));

    lomo_free_memtable(mt);
    fclose(fp);
    return 0;
}
