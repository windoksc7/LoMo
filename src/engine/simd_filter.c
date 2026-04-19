#include "simd_filter.h"
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64)
    #include <immintrin.h> // Intel/AMD AVX2
#elif defined(__aarch64__) || defined(_M_ARM64)
    #include <arm_neon.h>  // ARM NEON
#endif

int lomo_simd_match_2byte(const char* ptr, char c1, char c2) {
#if defined(__x86_64__) || defined(_M_X64)
    __m256i chunk1 = _mm256_loadu_si256((const __m256i*)ptr);
    __m256i chunk2 = _mm256_loadu_si256((const __m256i*)(ptr + 1));
    return _mm256_movemask_epi8(_mm256_and_si256(
        _mm256_cmpeq_epi8(chunk1, _mm256_set1_epi8(c1)),
        _mm256_cmpeq_epi8(chunk2, _mm256_set1_epi8(c2))
    ));
#elif defined(__aarch64__) || defined(_M_ARM64)
    uint8x16_t chunk1 = vld1q_u8((const uint8_t*)ptr);
    uint8x16_t chunk2 = vld1q_u8((const uint8_t*)(ptr + 1));
    uint8x16_t cmp = vandq_u8(vceqq_u8(chunk1, vdupq_n_u8(c1)), 
                              vceqq_u8(chunk2, vdupq_n_u8(c2)));
    return (vaddvq_u8(cmp) > 0); 
#else
    return (*ptr == c1 && *(ptr+1) == c2);
#endif
}

uint64_t lomo_simd_count_matches(const char* data, size_t size, const char* pattern, size_t pattern_len) {
    if (!data || size < pattern_len || pattern_len < 2) return 0;

    uint64_t count = 0;
    const char* ptr = (const char*)data;
    const char* end = (const char*)data + size - 33; // Safety margin for SIMD loads
    char c1 = pattern[0];
    char c2 = pattern[1];

    while (ptr <= end) {
        if (lomo_simd_match_2byte(ptr, c1, c2)) {
            if (memcmp(ptr, pattern, pattern_len) == 0) {
                count++;
                ptr += pattern_len - 1;
            }
        }
        ptr++;
    }

    // Residual scan for the last 33 bytes
    ptr = (ptr > (const char*)data + size - 33) ? ptr : (const char*)data + size - 33;
    end = (const char*)data + size - pattern_len;
    while (ptr <= end) {
        if (memcmp(ptr, pattern, pattern_len) == 0) {
            count++;
            ptr += pattern_len - 1;
        }
        ptr++;
    }

    return count;
}

uint64_t lomo_simd_filter_int64_gt(const int64_t* data, size_t count, int64_t threshold) {
    if (!data || count == 0) return 0;

    uint64_t total_matches = 0;
    size_t i = 0;

#if defined(__x86_64__) || defined(_M_X64)
    // AVX2 processes 4 int64_t values at a time
    __m256i v_threshold = _mm256_set1_epi64x(threshold);
    for (; i + 3 < count; i += 4) {
        __m256i v_data = _mm256_loadu_si256((const __m256i*)&data[i]);
        __m256i v_mask = _mm256_cmpgt_epi64(v_data, v_threshold);
        int mask = _mm256_movemask_pd(_mm256_castsi256_pd(v_mask));
#ifdef _MSC_VER
        total_matches += __popcnt(mask);
#else
        total_matches += __builtin_popcount(mask);
#endif
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    // NEON processes 2 int64_t values at a time
    int64x2_t v_threshold = vdupq_n_s64(threshold);
    for (; i + 1 < count; i += 2) {
        int64x2_t v_data = vld1q_s64(&data[i]);
        uint64x2_t v_mask = vcgtq_s64(v_data, v_threshold);
        if (vgetq_lane_u64(v_mask, 0)) total_matches++;
        if (vgetq_lane_u64(v_mask, 1)) total_matches++;
    }
#endif

    // Process remaining elements
    for (; i < count; i++) {
        if (data[i] > threshold) total_matches++;
    }

    return total_matches;
}

uint64_t lomo_simd_filter_int64_gt_mask(const int64_t* data, size_t count, int64_t threshold, uint8_t* out_mask) {
    if (!data || count == 0 || !out_mask) return 0;

    memset(out_mask, 0, (count + 7) / 8);
    uint64_t total_matches = 0;
    size_t i = 0;

#if defined(__x86_64__) || defined(_M_X64)
    __m256i v_threshold = _mm256_set1_epi64x(threshold);
    for (; i + 3 < count; i += 4) {
        __m256i v_data = _mm256_loadu_si256((const __m256i*)&data[i]);
        __m256i v_mask = _mm256_cmpgt_epi64(v_data, v_threshold);
        int mask = _mm256_movemask_pd(_mm256_castsi256_pd(v_mask));
        for(int bit = 0; bit < 4; bit++) {
            if (mask & (1 << bit)) {
                size_t idx = i + bit;
                out_mask[idx / 8] |= (1 << (idx % 8));
                total_matches++;
            }
        }
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    int64x2_t v_threshold = vdupq_n_s64(threshold);
    for (; i + 1 < count; i += 2) {
        int64x2_t v_data = vld1q_s64(&data[i]);
        uint64x2_t v_mask = vcgtq_s64(v_data, v_threshold);
        if (vgetq_lane_u64(v_mask, 0)) {
            out_mask[i / 8] |= (1 << (i % 8));
            total_matches++;
        }
        if (vgetq_lane_u64(v_mask, 1)) {
            out_mask[(i+1) / 8] |= (1 << ((i+1) % 8));
            total_matches++;
        }
    }
#endif

    for (; i < count; i++) {
        if (data[i] > threshold) {
            out_mask[i / 8] |= (1 << (i % 8));
            total_matches++;
        }
    }

    return total_matches;
}

uint64_t lomo_simd_count_range_uint64(const uint64_t* data, size_t count, uint64_t min_val, uint64_t max_val) {
    if (!data || count == 0 || min_val > max_val) return 0;

    uint64_t total_matches = 0;
    size_t i = 0;

#if defined(__x86_64__) || defined(_M_X64)
    __m256i v_min = _mm256_set1_epi64x((int64_t)min_val);
    __m256i v_max = _mm256_set1_epi64x((int64_t)max_val);
    __m256i sign_flip = _mm256_set1_epi64x((int64_t)0x8000000000000000ULL);
    __m256i v_min_f = _mm256_xor_si256(v_min, sign_flip);
    __m256i v_max_f = _mm256_xor_si256(v_max, sign_flip);

    for (; i + 3 < count; i += 4) {
        __m256i v_data = _mm256_loadu_si256((const __m256i*)&data[i]);
        __m256i v_data_f = _mm256_xor_si256(v_data, sign_flip);
        __m256i ge_min = _mm256_xor_si256(_mm256_cmpgt_epi64(v_min_f, v_data_f), _mm256_set1_epi64x(-1LL));
        __m256i le_max = _mm256_xor_si256(_mm256_cmpgt_epi64(v_data_f, v_max_f), _mm256_set1_epi64x(-1LL));
        __m256i in_range = _mm256_and_si256(ge_min, le_max);
        int mask = _mm256_movemask_pd(_mm256_castsi256_pd(in_range));
#ifdef _MSC_VER
        total_matches += __popcnt(mask);
#else
        total_matches += __builtin_popcount(mask);
#endif
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    uint64x2_t v_min = vdupq_n_u64(min_val);
    uint64x2_t v_max = vdupq_n_u64(max_val);
    for (; i + 1 < count; i += 2) {
        uint64x2_t v_data = vld1q_u64(&data[i]);
        uint64x2_t ge_min = vcgeq_u64(v_data, v_min);
        uint64x2_t le_max = vcleq_u64(v_data, v_max);
        uint64x2_t in_range = vandq_u64(ge_min, le_max);
        if (vgetq_lane_u64(in_range, 0)) total_matches++;
        if (vgetq_lane_u64(in_range, 1)) total_matches++;
    }
#endif

    // Residual
    for (; i < count; i++) {
        if (data[i] >= min_val && data[i] <= max_val) total_matches++;
    }

    return total_matches;
}

uint64_t lomo_simd_count_matches_masked(const char* data, size_t count, size_t row_size, const uint8_t* mask, const char* pattern, size_t pattern_len) {
    if (!data || count == 0 || !mask || !pattern) return 0;

    uint64_t total_matches = 0;
    char c1 = pattern[0];
    char c2 = pattern[1];

    for (size_t i = 0; i < count; i++) {
        if (!(mask[i / 8] & (1 << (i % 8)))) continue;
        const char* row_ptr = data + (i * row_size);
        if (lomo_simd_match_2byte(row_ptr, c1, c2)) {
            if (memcmp(row_ptr, pattern, pattern_len) == 0) {
                total_matches++;
            }
        }
    }

    return total_matches;
}

uint64_t lomo_simd_filter_string_contains_mask(const char* data, size_t total_rows, const uint8_t* in_mask, uint8_t* out_mask, const char* pattern, size_t pattern_len) {
    if (!data || total_rows == 0 || !out_mask || !pattern) return 0;

    memset(out_mask, 0, (total_rows + 7) / 8);
    uint64_t total_matches = 0;
    const uint8_t* ptr = (const uint8_t*)data;
    char c1 = pattern[0];
    char c2 = (pattern_len > 1) ? pattern[1] : 0;

    for (size_t i = 0; i < total_rows; i++) {
        uint64_t len = *(uint64_t*)ptr;
        ptr += sizeof(uint64_t);

        if (!in_mask || (in_mask[i / 8] & (1 << (i % 8)))) {
            int matched = 0;
            if (pattern_len == 1) {
                for(uint64_t j=0; j<len; j++) if (((char*)ptr)[j] == c1) { matched = 1; break; }
            } else {
                const char* row_ptr = (const char*)ptr;
                for(uint64_t j=0; j + pattern_len <= len; j++) {
                    if (row_ptr[j] == c1 && row_ptr[j+1] == c2) {
                        if (memcmp(row_ptr + j, pattern, pattern_len) == 0) {
                            matched = 1;
                            break;
                        }
                    }
                }
            }
            if (matched) {
                out_mask[i / 8] |= (1 << (i % 8));
                total_matches++;
            }
        }
        ptr += len;
    }

    return total_matches;
}

int64_t lomo_simd_sum_int64(const int64_t* data, size_t count) {
    if (!data || count == 0) return 0;
    int64_t total = 0;
    size_t i = 0;

#if defined(__x86_64__) || defined(_M_X64)
    __m256i v_total = _mm256_setzero_si256();
    for (; i + 3 < count; i += 4) {
        __m256i v_data = _mm256_loadu_si256((const __m256i*)&data[i]);
        v_total = _mm256_add_epi64(v_total, v_data);
    }
    int64_t temp[4];
    _mm256_storeu_si256((__m256i*)temp, v_total);
    total = temp[0] + temp[1] + temp[2] + temp[3];
#elif defined(__aarch64__) || defined(_M_ARM64)
    int64x2_t v_total = vdupq_n_s64(0);
    for (; i + 1 < count; i += 2) {
        int64x2_t v_data = vld1q_s64(&data[i]);
        v_total = vaddq_s64(v_total, v_data);
    }
    total = vaddvq_s64(v_total);
#endif

    for (; i < count; i++) {
        total += data[i];
    }
    return total;
}

int64_t lomo_simd_max_int64(const int64_t* data, size_t count) {
    if (!data || count == 0) return 0;
    int64_t max_val = data[0];
    size_t i = 0;

#if defined(__x86_64__) || defined(_M_X64)
    if (count >= 4) {
        __m256i v_max = _mm256_loadu_si256((const __m256i*)&data[0]);
        for (i = 4; i + 3 < count; i += 4) {
            __m256i v_data = _mm256_loadu_si256((const __m256i*)&data[i]);
            __m256i v_mask = _mm256_cmpgt_epi64(v_data, v_max);
            v_max = _mm256_blendv_epi8(v_max, v_data, v_mask);
        }
        int64_t temp[4];
        _mm256_storeu_si256((__m256i*)temp, v_max);
        for(int j = 0; j < 4; j++) {
            if (temp[j] > max_val) max_val = temp[j];
        }
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    if (count >= 2) {
        int64x2_t v_max = vld1q_s64(&data[0]);
        for (i = 2; i + 1 < count; i += 2) {
            int64x2_t v_data = vld1q_s64(&data[i]);
            v_max = vmaxq_s64(v_max, v_data);
        }
        max_val = vmaxvq_s64(v_max);
    }
#endif

    for (; i < count; i++) {
        if (data[i] > max_val) max_val = data[i];
    }
    return max_val;
}

int64_t lomo_simd_sum_int64_masked(const int64_t* data, size_t count, const uint8_t* mask) {
    if (!data || count == 0 || !mask) return 0;
    int64_t total = 0;
    for (size_t i = 0; i < count; i++) {
        if (mask[i / 8] & (1 << (i % 8))) {
            total += data[i];
        }
    }
    return total;
}
