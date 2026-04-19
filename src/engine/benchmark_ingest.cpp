#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include "ingestor.h"
#include "storage_engine.h"
#include "merger.h"
#include "lomo_os.h"

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

struct ParsingWorkerArgs {
    const uint8_t* base_ptr;
    size_t mf_size;
    uint64_t base_ts;
    uint64_t start_row_offset;
    uint32_t target_rows;
    LomoIngestPipeline* lp;
    LomoColumnType* types;
    std::atomic<uint64_t>* global_bytes;
    std::atomic<uint64_t>* global_rows;
};

void parsing_worker_task(ParsingWorkerArgs args) {
    LomoMemTable* mt = lomo_init_memtable(3, args.types);
    uint64_t rows_done = 0;
    uint64_t bytes_done = 0;

    const uint8_t* p = args.base_ptr;
    const uint8_t* end = args.base_ptr + args.mf_size;

    while (rows_done < args.target_rows) {
        if (p >= end) p = args.base_ptr;

        const uint8_t* line_start = p;
        const uint8_t* nl = (const uint8_t*)memchr(p, '\n', end - p);
        size_t len = 0;
        
        if (nl) {
            len = nl - line_start;
            p = nl + 1; // Move past the newline
        } else {
            len = end - line_start;
            p = end;
        }

        if (len == 0) continue;

        uint64_t ts = args.base_ts + args.start_row_offset + rows_done;
        int64_t status = 200;

        const void* data_ptrs[] = { &ts, line_start, &status };
        size_t sizes[] = { 8, len, 8 };

        lomo_ingest_row_fast(mt, data_ptrs, sizes);
        rows_done++;
        bytes_done += len;

        if (mt->row_count >= 2000000) {
            lomo_pipeline_submit(args.lp, mt);
            mt = lomo_init_memtable(3, args.types);
        }
    }

    if (mt->row_count > 0) {
        lomo_pipeline_submit(args.lp, mt);
    } else {
        lomo_free_memtable(mt);
    }

    args.global_bytes->fetch_add(bytes_done);
    args.global_rows->fetch_add(rows_done);
}

int main() {
    const char* filename = "dummy_web.log";
    uint64_t base_ts = (uint64_t)time(NULL);

    LomoMappedFile mf;
    if (!lomo_mmap_open(filename, &mf)) {
        printf("Failed to mmap %s\n", filename);
        return -1;
    }

    const char* pipeline_dir = "lomo_pipeline_parts";
    const char* merged_dir = "merged_parts_benchmark";
    LomoColumnType types[] = { LOMO_TYPE_TIMESTAMP, LOMO_TYPE_STRING, LOMO_TYPE_INT64 };
    
    LomoMergerDaemon* daemon = lomo_start_merger_daemon(pipeline_dir, merged_dir);
    // 2 Flushers (down from 4 to give more CPU to parsers)
    LomoIngestPipeline* lp = lomo_init_pipeline(2, pipeline_dir, 3, types);

    // 4 Parsers
    uint32_t total_target_rows = 20000000;
    int num_parsers = 4; 
    uint32_t rows_per_parser = total_target_rows / num_parsers;
    
    std::atomic<uint64_t> total_bytes_processed{0};
    std::atomic<uint64_t> total_rows_processed{0};
    std::vector<std::jthread> parsing_workers;

    printf("[LoMo Benchmark] 4-2-4 Split Strategy: %d Parsers, 2 Flushers, 4 Mergers, Target: %u Rows\n", 
           num_parsers, total_target_rows);

    double start_time = get_time();

    for (int i = 0; i < num_parsers; i++) {
        ParsingWorkerArgs args = {
            (const uint8_t*)mf.data, mf.size, base_ts, 
            (uint64_t)i * rows_per_parser, 
            rows_per_parser,
            lp, types, &total_bytes_processed, &total_rows_processed
        };
        parsing_workers.emplace_back(parsing_worker_task, args);
    }

    parsing_workers.clear(); 
    double parsing_end_time = get_time();

    printf("[LoMo Benchmark] Front-end finished. Waiting for back-end pipeline to drain...\n");
    lomo_shutdown_pipeline(lp);
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    lomo_stop_merger_daemon(daemon);

    double total_end_time = get_time();
    double parsing_elapsed = parsing_end_time - start_time;
    double total_elapsed = total_end_time - start_time;

    printf("\n--- High-Speed Scale Results ---\n");
    printf("Total Rows: %llu\n", (unsigned long long)total_rows_processed.load());
    printf("Total Data: %.2f MB\n", total_bytes_processed.load() / 1024.0 / 1024.0);
    printf("Parsing Speed: %.2f MB/s\n", (total_bytes_processed.load() / 1024.0 / 1024.0) / parsing_elapsed);
    printf("End-to-End Throughput: %.2f MB/s\n", (total_bytes_processed.load() / 1024.0 / 1024.0) / total_elapsed);

    lomo_mmap_close(&mf);
    return 0;
}
