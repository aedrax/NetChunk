#ifndef NETCHUNK_TEST_UTILS_H
#define NETCHUNK_TEST_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

// Maximum path length for test files
#define TEST_MAX_PATH_LEN 512
#define TEST_MAX_SERVERS 10
#define TEST_TEMP_DIR_PREFIX "netchunk_test_"

/**
 * Test file context for managing temporary files and directories
 */
typedef struct test_file_context {
    char temp_dir[TEST_MAX_PATH_LEN];
    char test_files[10][TEST_MAX_PATH_LEN];
    size_t file_count;
    bool cleanup_required;
} test_file_context_t;

/**
 * Performance test metrics for benchmarking
 */
typedef struct test_metrics {
    uint64_t start_time_ns;
    uint64_t end_time_ns;
    size_t peak_memory_usage;
    size_t bytes_processed;
    uint32_t operations_completed;
} test_metrics_t;

/**
 * Test configuration for various test scenarios
 */
typedef struct test_config {
    size_t chunk_size;
    uint32_t replication_factor;
    uint32_t server_count;
    bool simulate_failures;
    double failure_rate;
} test_config_t;

// Test environment setup and cleanup
int test_setup_environment(void);
void test_cleanup_environment(void);

// Temporary file and directory management
int create_temp_test_directory(test_file_context_t* ctx);
int cleanup_temp_test_directory(test_file_context_t* ctx);
int generate_test_file(const char* filepath, size_t size, uint8_t pattern);
int generate_random_test_file(const char* filepath, size_t size);
int compare_files(const char* file1, const char* file2);
int verify_file_integrity(const char* filepath, const char* expected_hash);

// Test data generation utilities
int create_test_config_file(const char* filepath, const test_config_t* config);
int create_test_manifest_file(const char* filepath, const char* filename, size_t file_size);
const char* get_test_data_pattern(size_t index);
int write_test_pattern_to_buffer(uint8_t* buffer, size_t buffer_size, uint8_t pattern);

// File system utilities
bool file_exists(const char* filepath);
size_t get_file_size(const char* filepath);
int copy_file(const char* src, const char* dst);
int create_directory_if_not_exists(const char* dirpath);
int remove_directory_recursive(const char* dirpath);

// Performance measurement utilities
void test_metrics_start(test_metrics_t* metrics);
void test_metrics_end(test_metrics_t* metrics);
void test_metrics_add_bytes(test_metrics_t* metrics, size_t bytes);
void test_metrics_add_operation(test_metrics_t* metrics);
double test_metrics_get_duration_ms(const test_metrics_t* metrics);
double test_metrics_get_throughput_mbps(const test_metrics_t* metrics);
void test_metrics_print_summary(const test_metrics_t* metrics, const char* test_name);

// Memory management utilities
void* test_malloc_tracked(size_t size);
void test_free_tracked(void* ptr);
size_t test_get_allocated_memory(void);
void test_reset_memory_tracking(void);
bool test_check_memory_leaks(void);

// String utilities for testing
char* test_create_temp_filename(const char* prefix, const char* extension);
int test_parse_size_string(const char* size_str, size_t* size);
const char* test_format_bytes(size_t bytes, char* buffer, size_t buffer_size);

// Random number utilities for testing
void test_seed_random(uint32_t seed);
uint32_t test_random_uint32(void);
uint64_t test_random_uint64(void);
double test_random_double(void);
int test_random_int_range(int min, int max);

// Test assertion helpers (complement Unity assertions)
#define TEST_ASSERT_FILE_EXISTS(filepath) \
    do { \
        if (!file_exists(filepath)) { \
            TEST_FAIL_MESSAGE("File does not exist: " filepath); \
        } \
    } while(0)

#define TEST_ASSERT_FILE_NOT_EXISTS(filepath) \
    do { \
        if (file_exists(filepath)) { \
            TEST_FAIL_MESSAGE("File should not exist: " filepath); \
        } \
    } while(0)

#define TEST_ASSERT_FILE_SIZE(filepath, expected_size) \
    do { \
        size_t actual_size = get_file_size(filepath); \
        if (actual_size != (expected_size)) { \
            char msg[256]; \
            snprintf(msg, sizeof(msg), "File size mismatch for %s: expected %zu, got %zu", \
                     filepath, (size_t)(expected_size), actual_size); \
            TEST_FAIL_MESSAGE(msg); \
        } \
    } while(0)

#define TEST_ASSERT_FILES_EQUAL(file1, file2) \
    do { \
        int result = compare_files(file1, file2); \
        if (result != 0) { \
            char msg[512]; \
            snprintf(msg, sizeof(msg), "Files are not equal: %s vs %s (result: %d)", \
                     file1, file2, result); \
            TEST_FAIL_MESSAGE(msg); \
        } \
    } while(0)

#define TEST_ASSERT_NO_MEMORY_LEAKS() \
    do { \
        if (!test_check_memory_leaks()) { \
            TEST_FAIL_MESSAGE("Memory leaks detected"); \
        } \
    } while(0)

// Test timeout utilities
#define TEST_TIMEOUT_SECONDS 30

// Common test patterns
#define TEST_PATTERN_ZEROS 0x00
#define TEST_PATTERN_ONES 0xFF
#define TEST_PATTERN_ALTERNATING 0xAA
#define TEST_PATTERN_INCREMENTAL 0x01

#endif // NETCHUNK_TEST_UTILS_H
