#include "storage_engine.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mutex>
#include <thread>
#include <algorithm>
#include "lomo_os.h"

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

// Internal helper for indexing
#define LOMO_GET_GRANULE(part, col, g_idx) (&part->index[(col * part->granule_count) + g_idx])

LomoPartHeader* lomo_init_part(uint32_t column_count) {
    LomoPartHeader* part = (LomoPartHeader*)malloc(sizeof(LomoPartHeader));
    if (!part) return NULL;
    part->magic = LOMO_MAGIC; part->version = LOMO_VERSION; part->total_rows = 0;
    part->column_count = column_count; part->granule_count = 0; part->index = NULL; part->directory_path = NULL;
    part->columns = (column_count > 0) ? (LomoColumnMeta*)calloc(column_count, sizeof(LomoColumnMeta)) : NULL;
    if (part->columns) {
        for (uint32_t i = 0; i < column_count; i++) {
            part->columns[i].schema_version = 1; 
        }
    }
    return part;
}

int lomo_write_column_chunk(LomoPartHeader* part, uint32_t column_id, const void* data_ptr, size_t size) { 
    (void)part; (void)column_id; (void)data_ptr; (void)size;
    return 0; 
}

int lomo_read_column_chunk_simd(const LomoPartHeader* part, uint32_t column_id, uint64_t granule_idx, void* aligned_buffer, size_t buffer_size) {
    if (!part || !part->index || column_id >= part->column_count || granule_idx >= part->granule_count) return -1;
    LomoSparseIndexGranule* g = LOMO_GET_GRANULE(part, column_id, granule_idx);
    char path[256]; sprintf(path, "%s/%u.col", part->directory_path ? part->directory_path : ".", column_id); 
    FILE* fp = fopen(path, "rb"); if (!fp) return -1;
    fseek(fp, (long)g->start_offset, SEEK_SET);
    
    LomoCompressionType ct = part->columns[column_id].compression;
    if (ct == LOMO_COMPRESS_XPRESS || ct == LOMO_COMPRESS_LZ4 || ct == LOMO_COMPRESS_DELTA_LZ4) {
        LomoHALCompressionAlg hal_alg = (ct == LOMO_COMPRESS_XPRESS) ? LOMO_HAL_COMPRESS_XPRESS : LOMO_HAL_COMPRESS_LZ4;
        void* decompressor = lomo_decompressor_open(hal_alg);
        if (decompressor) {
            void* comp_buf = malloc(g->compressed_size);
            fread(comp_buf, 1, g->compressed_size, fp);
            size_t decomp_size = 0;
            lomo_decompress(decompressor, comp_buf, g->compressed_size, aligned_buffer, buffer_size, &decomp_size);
            
            if (ct == LOMO_COMPRESS_DELTA_LZ4) {
                // Reverse Delta Encoding (Prefix Sum)
                int64_t* data = (int64_t*)aligned_buffer;
                int64_t current = g->base_value;
                for (uint32_t i = 0; i < g->row_count; i++) {
                    current += data[i];
                    data[i] = current;
                }
            }
            free(comp_buf); lomo_decompressor_close(decompressor);
        }
    } else {
        fread(aligned_buffer, 1, (g->uncompressed_size < buffer_size) ? g->uncompressed_size : buffer_size, fp);
    }
    fclose(fp); return 0;
}

int lomo_read_column_simd(const LomoPartHeader* part, uint32_t column_id, void* aligned_buffer, size_t buffer_size) {
    if (!part || column_id >= part->column_count) return -1;
    
    char path[256]; sprintf(path, "%s/%u.col", part->directory_path ? part->directory_path : ".", column_id); 
    FILE* fp = fopen(path, "rb"); if (!fp) return -1;

    uint8_t* dst = (uint8_t*)aligned_buffer;
    LomoCompressionType ct = part->columns[column_id].compression;
    void* decompressor = NULL;
    if (ct == LOMO_COMPRESS_XPRESS || ct == LOMO_COMPRESS_LZ4 || ct == LOMO_COMPRESS_DELTA_LZ4) {
        LomoHALCompressionAlg hal_alg = (ct == LOMO_COMPRESS_XPRESS) ? LOMO_HAL_COMPRESS_XPRESS : LOMO_HAL_COMPRESS_LZ4;
        decompressor = lomo_decompressor_open(hal_alg);
    }

    for (uint32_t g = 0; g < part->granule_count; g++) {
        LomoSparseIndexGranule* gr = LOMO_GET_GRANULE(part, column_id, g);
        fseek(fp, (long)gr->start_offset, SEEK_SET);
        if (decompressor) {
            void* comp_buf = malloc(gr->compressed_size);
            fread(comp_buf, 1, gr->compressed_size, fp);
            size_t decomp_size = 0;
            lomo_decompress(decompressor, comp_buf, gr->compressed_size, dst, gr->uncompressed_size, &decomp_size);
            
            if (ct == LOMO_COMPRESS_DELTA_LZ4) {
                int64_t* data = (int64_t*)dst;
                int64_t current = gr->base_value;
                for (uint32_t i = 0; i < gr->row_count; i++) {
                    current += data[i];
                    data[i] = current;
                }
            }
            dst += gr->uncompressed_size; free(comp_buf);
        } else {
            fread(dst, 1, gr->uncompressed_size, fp);
            dst += gr->uncompressed_size;
        }
    }
    if (decompressor) lomo_decompressor_close(decompressor);
    fclose(fp); return 0;
}

int lomo_flush_part(LomoPartHeader* part, const char* directory_path, void** column_buffers, size_t* column_sizes) {
    if (!part || !directory_path) return -1;
    mkdir(directory_path, 0777);
    if (part->directory_path) free(part->directory_path);
    part->directory_path = _strdup(directory_path);

    uint32_t num_granules = (uint32_t)((part->total_rows + LOMO_GRANULE_ROWS - 1) / LOMO_GRANULE_ROWS);
    if (num_granules == 0 && part->total_rows > 0) num_granules = 1;
    printf("[Storage] Flushing part with %llu rows, %u granules...\n", part->total_rows, num_granules);
    
    part->granule_count = num_granules;
    part->index = (LomoSparseIndexGranule*)calloc(part->column_count * num_granules, sizeof(LomoSparseIndexGranule));

    void* compressor = lomo_compressor_open(LOMO_HAL_COMPRESS_LZ4);

    for (uint32_t c = 0; c < part->column_count; c++) {
        char path[256]; sprintf(path, "%s/%u.col", directory_path, c);
        LomoFile* lf = lomo_file_open_write(path);
        if (!lf) {
            printf("[Storage] ERROR: Could not open file %s for writing.\n", path);
            continue;
        }

        LomoColumnType type = part->columns[c].type;
        // Apply Delta + LZ4 for Integer and Timestamp columns
        if (type == LOMO_TYPE_INT64 || type == LOMO_TYPE_TIMESTAMP) {
            part->columns[c].compression = LOMO_COMPRESS_DELTA_LZ4;
        } else {
            part->columns[c].compression = LOMO_COMPRESS_LZ4;
        }

        uint64_t current_file_offset = 0;
        uint8_t* col_base = (uint8_t*)column_buffers[c];
        printf("  Flushing column %u (%u granules)...\n", c, num_granules);

        for (uint32_t g = 0; g < num_granules; g++) {
            uint32_t start_row = g * LOMO_GRANULE_ROWS;
            uint32_t rows_in_g = (uint32_t)((start_row + LOMO_GRANULE_ROWS > part->total_rows) ? (part->total_rows - start_row) : LOMO_GRANULE_ROWS);
            size_t uncomp_size = 0, src_offset = 0;

            if (type == LOMO_TYPE_STRING) {
                uint8_t* p = col_base; for(uint32_t r=0; r<start_row; r++) {
                    p += (8 + *(uint64_t*)p);
                }
                src_offset = p - col_base; uint8_t* start_p = p;
                for(uint32_t r=0; r<rows_in_g; r++) {
                    p += (8 + *(uint64_t*)p);
                }
                uncomp_size = p - start_p;
            } else {
                size_t row_sz = 8;
                src_offset = (size_t)start_row * row_sz; uncomp_size = (size_t)rows_in_g * row_sz;
            }

            LomoSparseIndexGranule* g_meta = LOMO_GET_GRANULE(part, c, g);
            g_meta->row_count = rows_in_g;
            g_meta->uncompressed_size = (uint32_t)uncomp_size;

            void* final_src = col_base + src_offset;
            int64_t* delta_buf = NULL;

            if (uncomp_size > 0 && part->columns[c].compression == LOMO_COMPRESS_DELTA_LZ4) {
                delta_buf = (int64_t*)malloc(uncomp_size);
                int64_t* src_data = (int64_t*)(col_base + src_offset);
                g_meta->base_value = src_data[0];
                g_meta->min_val = src_data[0];
                g_meta->max_val = src_data[0];
                
                int64_t last = src_data[0];
                delta_buf[0] = 0; // First element delta is 0 relative to base_value
                for (uint32_t i = 1; i < rows_in_g; i++) {
                    delta_buf[i] = src_data[i] - last;
                    last = src_data[i];
                    if (src_data[i] < g_meta->min_val) g_meta->min_val = src_data[i];
                    if (src_data[i] > g_meta->max_val) g_meta->max_val = src_data[i];
                }
                final_src = delta_buf;
            }

            if (uncomp_size > 0) {
                void* comp_buf = lomo_aligned_malloc(uncomp_size + 4096, 4096);
                size_t comp_size = 0;
                int ok = lomo_compress(compressor, final_src, uncomp_size, comp_buf, uncomp_size + 1024, &comp_size);
                
                if (ok && comp_size < uncomp_size) {
                    lomo_file_write_async(lf, current_file_offset, comp_buf, comp_size);
                    g_meta->start_offset = current_file_offset;
                    g_meta->compressed_size = (uint32_t)comp_size; 
                    current_file_offset += comp_size;
                } else {
                    lomo_file_write_async(lf, current_file_offset, final_src, uncomp_size);
                    g_meta->start_offset = current_file_offset;
                    g_meta->compressed_size = (uint32_t)uncomp_size; 
                    current_file_offset += uncomp_size;
                }
                lomo_aligned_free(comp_buf);
            } else {
                g_meta->start_offset = current_file_offset;
                g_meta->compressed_size = 0;
            }

            if (c == 0) { // Primary Index (Timestamp)
                uint64_t* ts = (uint64_t*)column_buffers[0];
                g_meta->min_timestamp = ts[start_row];
                g_meta->max_timestamp = ts[start_row + rows_in_g - 1];
            }
            if (delta_buf) free(delta_buf);
        }
        lomo_file_flush_and_close(lf);
        part->columns[c].column_id = c; part->columns[c].uncompressed_size = column_sizes[c]; part->columns[c].compressed_size = current_file_offset;
    }
    if (compressor) lomo_compressor_close(compressor);

    char idx_path[256]; sprintf(idx_path, "%s/primary.idx", directory_path);
    FILE* ifp = fopen(idx_path, "wb"); if (ifp) { fwrite(part->index, sizeof(LomoSparseIndexGranule), part->column_count * num_granules, ifp); fclose(ifp); }
    char h_path[256]; sprintf(h_path, "%s/header.bin", directory_path);
    FILE* hfp = fopen(h_path, "wb");
    if (hfp) {
        fwrite(&part->magic, 4, 1, hfp); fwrite(&part->version, 4, 1, hfp);
        fwrite(&part->total_rows, 8, 1, hfp); fwrite(&part->column_count, 4, 1, hfp);
        for (uint32_t i = 0; i < part->column_count; i++) fwrite(&part->columns[i], sizeof(LomoColumnMeta), 1, hfp);
        fwrite(&part->granule_count, 4, 1, hfp); fclose(hfp);
    }
    return 0;
}

LomoPartHeader* lomo_load_part(const char* directory_path) {
    char h_path[256]; sprintf(h_path, "%s/header.bin", directory_path);
    FILE* fp = fopen(h_path, "rb"); if (!fp) return NULL;
    LomoPartHeader* part = (LomoPartHeader*)malloc(sizeof(LomoPartHeader));
    part->directory_path = _strdup(directory_path);
    fread(&part->magic, 4, 1, fp); fread(&part->version, 4, 1, fp);
    fread(&part->total_rows, 8, 1, fp); fread(&part->column_count, 4, 1, fp);
    part->columns = (LomoColumnMeta*)calloc(part->column_count, sizeof(LomoColumnMeta));
    for (uint32_t i = 0; i < part->column_count; i++) fread(&part->columns[i], sizeof(LomoColumnMeta), 1, fp);
    if (fread(&part->granule_count, 4, 1, fp) == 1) {
        char i_path[256]; sprintf(i_path, "%s/primary.idx", directory_path);
        FILE* ifp = fopen(i_path, "rb");
        if (ifp) { 
            part->index = (LomoSparseIndexGranule*)calloc(part->column_count * part->granule_count, sizeof(LomoSparseIndexGranule));
            fread(part->index, sizeof(LomoSparseIndexGranule), part->column_count * part->granule_count, ifp); fclose(ifp); 
        }
    }
    fclose(fp); return part;
}

uint32_t* lomo_filter_granules_by_time(const LomoPartHeader* part, uint64_t min_ts, uint64_t max_ts, uint32_t* out_count) {
    if (!part || !part->index || part->granule_count == 0) return NULL;
    uint32_t* matched = (uint32_t*)malloc(part->granule_count * 4); uint32_t count = 0;
    for (uint32_t i = 0; i < part->granule_count; i++) {
        LomoSparseIndexGranule* g = LOMO_GET_GRANULE(part, 0, i);
        if (!(g->max_timestamp < min_ts || g->min_timestamp > max_ts)) matched[count++] = i;
    }
    *out_count = count; return matched;
}

void lomo_free_part(LomoPartHeader* part) { 
    if (!part) return; 
    if (part->columns) free(part->columns); 
    if (part->index) free(part->index); 
    if (part->directory_path) free(part->directory_path); 
    free(part); 
}
