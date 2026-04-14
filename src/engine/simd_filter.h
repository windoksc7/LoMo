#ifndef LOMO_SIMD_FILTER_H
#define LOMO_SIMD_FILTER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Checks for a 2-byte pattern match within a 32-byte chunk (AVX2) or 16-byte chunk (NEON).
 * @param ptr Pointer to the data to scan.
 * @param c1 First character of the pattern.
 * @param c2 Second character of the pattern.
 * @return A mask or boolean indicating match positions.
 */
int lomo_simd_match_2byte(const char* ptr, char c1, char c2);

/**
 * @brief Performs a fast SIMD scan for a specific string pattern.
 * @param data Data buffer to scan.
 * @param size Size of the data buffer.
 * @param pattern The pattern to search for.
 * @param pattern_len Length of the pattern.
 * @return Total match count.
 */
uint64_t lomo_simd_count_matches(const char* data, size_t size, const char* pattern, size_t pattern_len);

/**
 * @brief Performs a vectorized 'greater than' filter on a buffer of int64_t values.
 * @param data Buffer of int64_t values.
 * @param count Number of int64_t elements in the buffer.
 * @param threshold The value to compare against.
 * @return Total number of elements greater than the threshold.
 */
uint64_t lomo_simd_filter_int64_gt(const int64_t* data, size_t count, int64_t threshold);

/**
 * @brief Generates a selection bitmask for int64_t values greater than a threshold.
 * @param data Buffer of int64_t values.
 * @param count Number of elements.
 * @param threshold Comparison value.
 * @param out_mask Pointer to a bitmask buffer (must be at least count/8 bytes).
 * @return Total matches found.
 */
uint64_t lomo_simd_filter_int64_gt_mask(const int64_t* data, size_t count, int64_t threshold, uint8_t* out_mask);

/**
 * @brief Counts elements in a uint64_t array that fall within [min_val, max_val] using AVX2.
 * @param data Buffer of uint64_t values.
 * @param count Number of elements.
 * @param min_val Inclusive minimum value.
 * @param max_val Inclusive maximum value.
 * @return Total number of elements in range.
 */
uint64_t lomo_simd_count_range_uint64(const uint64_t* data, size_t count, uint64_t min_val, uint64_t max_val);

/**
 * @brief Performs a masked SIMD scan for a string pattern.
 * @param data Data buffer to scan.
 * @param count Number of rows/elements.
 * @param row_size Size of each row in bytes.
 * @param mask The selection mask from a previous filter.
 * @param pattern The pattern to search for.
 * @param pattern_len Length of the pattern.
 * @return Total match count in selected rows.
 */
uint64_t lomo_simd_count_matches_masked(const char* data, size_t count, size_t row_size, const uint8_t* mask, const char* pattern, size_t pattern_len);

/**
 * @brief Performs a high-speed "CONTAINS" filter on variable-length strings.
 * @param data The string pool buffer (length-prefixed).
 * @param total_rows Total number of rows in the buffer.
 * @param in_mask Optional selection mask from a previous filter (NULL to scan all).
 * @param out_mask Output bitmask for matching rows.
 * @param pattern Substring to search for.
 * @param pattern_len Length of the pattern.
 * @return Total rows matching the substring.
 */
uint64_t lomo_simd_filter_string_contains_mask(const char* data, size_t total_rows, const uint8_t* in_mask, uint8_t* out_mask, const char* pattern, size_t pattern_len);

/**
 * @brief Vectorized SUM operation for int64_t column.
 * @param data Buffer of int64_t values.
 * @param count Number of elements.
 * @return The sum of all elements.
 */
int64_t lomo_simd_sum_int64(const int64_t* data, size_t count);

/**
 * @brief Vectorized MAX operation for int64_t column.
 * @param data Buffer of int64_t values.
 * @param count Number of elements.
 * @return The maximum value in the buffer.
 */
int64_t lomo_simd_max_int64(const int64_t* data, size_t count);

/**
 * @brief Masked vectorized SUM operation for int64_t column.
 * @param data Buffer of int64_t values.
 * @param count Number of elements.
 * @param mask Selection bitmask from a previous filter.
 * @return The sum of selected elements.
 */
int64_t lomo_simd_sum_int64_masked(const int64_t* data, size_t count, const uint8_t* mask);

#ifdef __cplusplus
}
#endif

#endif // LOMO_SIMD_FILTER_H
