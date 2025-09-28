#include "test_utils.h"
#include "unity.h"
#include <sys/types.h>
#include <sys/time.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>


// Memory tracking globals
static size_t g_allocated_memory = 0;
static bool g_memory_tracking_enabled = false;

// Random number state
static uint64_t g_random_state = 1;

/**
 * Test environment setup
 */
int test_setup_environment(void) {
    // Initialize random number generator with current time
    struct timeval tv;
    gettimeofday(&tv, NULL);
    test_seed_random((uint32_t)(tv.tv_sec ^ tv.tv_usec));
    
    // Enable memory tracking
    g_memory_tracking_enabled = true;
    test_reset_memory_tracking();
    
    return 0;
}

/**
 * Test environment cleanup
 */
void test_cleanup_environment(void) {
    g_memory_tracking_enabled = false;
    g_allocated_memory = 0;
}

/**
 * Create temporary test directory
 */
int create_temp_test_directory(test_file_context_t* ctx) {
    if (!ctx) return -1;
    
    // Generate unique temporary directory name
    struct timeval tv;
    gettimeofday(&tv, NULL);
    snprintf(ctx->temp_dir, sizeof(ctx->temp_dir), "/tmp/%s%ld_%ld",
             TEST_TEMP_DIR_PREFIX, tv.tv_sec, tv.tv_usec);
    
    // Create directory
    if (mkdir(ctx->temp_dir, 0755) != 0) {
        return -1;
    }
    
    ctx->file_count = 0;
    ctx->cleanup_required = true;
    return 0;
}

/**
 * Cleanup temporary test directory
 */
int cleanup_temp_test_directory(test_file_context_t* ctx) {
    if (!ctx || !ctx->cleanup_required) return 0;
    
    // Remove all tracked files
    for (size_t i = 0; i < ctx->file_count; i++) {
        unlink(ctx->test_files[i]);
    }
    
    // Remove directory
    int result = remove_directory_recursive(ctx->temp_dir);
    ctx->cleanup_required = false;
    return result;
}

/**
 * Generate test file with specific pattern
 */
int generate_test_file(const char* filepath, size_t size, uint8_t pattern) {
    FILE* fp = fopen(filepath, "wb");
    if (!fp) return -1;
    
    const size_t buffer_size = 4096;
    uint8_t buffer[buffer_size];
    
    // Fill buffer with pattern
    write_test_pattern_to_buffer(buffer, buffer_size, pattern);
    
    // Write file in chunks
    size_t remaining = size;
    while (remaining > 0) {
        size_t chunk_size = (remaining > buffer_size) ? buffer_size : remaining;
        if (fwrite(buffer, 1, chunk_size, fp) != chunk_size) {
            fclose(fp);
            return -1;
        }
        remaining -= chunk_size;
    }
    
    fclose(fp);
    return 0;
}

/**
 * Generate random test file
 */
int generate_random_test_file(const char* filepath, size_t size) {
    FILE* fp = fopen(filepath, "wb");
    if (!fp) return -1;
    
    const size_t buffer_size = 4096;
    uint8_t buffer[buffer_size];
    
    size_t remaining = size;
    while (remaining > 0) {
        size_t chunk_size = (remaining > buffer_size) ? buffer_size : remaining;
        
        // Fill buffer with random data
        for (size_t i = 0; i < chunk_size; i++) {
            buffer[i] = (uint8_t)test_random_uint32();
        }
        
        if (fwrite(buffer, 1, chunk_size, fp) != chunk_size) {
            fclose(fp);
            return -1;
        }
        remaining -= chunk_size;
    }
    
    fclose(fp);
    return 0;
}

/**
 * Compare two files byte by byte
 */
int compare_files(const char* file1, const char* file2) {
    FILE* fp1 = fopen(file1, "rb");
    FILE* fp2 = fopen(file2, "rb");
    
    if (!fp1 && !fp2) return 0; // Both don't exist - equal
    if (!fp1 || !fp2) {
        if (fp1) fclose(fp1);
        if (fp2) fclose(fp2);
        return -1; // One exists, one doesn't
    }
    
    const size_t buffer_size = 4096;
    uint8_t buffer1[buffer_size];
    uint8_t buffer2[buffer_size];
    
    int result = 0;
    while (true) {
        size_t read1 = fread(buffer1, 1, buffer_size, fp1);
        size_t read2 = fread(buffer2, 1, buffer_size, fp2);
        
        if (read1 != read2) {
            result = -1;
            break;
        }
        
        if (read1 == 0) break; // EOF reached
        
        if (memcmp(buffer1, buffer2, read1) != 0) {
            result = -1;
            break;
        }
    }
    
    fclose(fp1);
    fclose(fp2);
    return result;
}

/**
 * Write test pattern to buffer
 */
int write_test_pattern_to_buffer(uint8_t* buffer, size_t buffer_size, uint8_t pattern) {
    if (!buffer) return -1;
    
    switch (pattern) {
        case TEST_PATTERN_ZEROS:
            memset(buffer, 0x00, buffer_size);
            break;
        case TEST_PATTERN_ONES:
            memset(buffer, 0xFF, buffer_size);
            break;
        case TEST_PATTERN_ALTERNATING:
            for (size_t i = 0; i < buffer_size; i++) {
                buffer[i] = (i % 2) ? 0xAA : 0x55;
            }
            break;
        case TEST_PATTERN_INCREMENTAL:
            for (size_t i = 0; i < buffer_size; i++) {
                buffer[i] = (uint8_t)(i & 0xFF);
            }
            break;
        default:
            memset(buffer, pattern, buffer_size);
            break;
    }
    
    return 0;
}

/**
 * Check if file exists
 */
bool file_exists(const char* filepath) {
    return access(filepath, F_OK) == 0;
}

/**
 * Get file size
 */
size_t get_file_size(const char* filepath) {
    struct stat st;
    if (stat(filepath, &st) != 0) return 0;
    return (size_t)st.st_size;
}

/**
 * Create directory if it doesn't exist
 */
int create_directory_if_not_exists(const char* dirpath) {
    struct stat st;
    if (stat(dirpath, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return mkdir(dirpath, 0755);
}

/**
 * Remove directory recursively
 */
int remove_directory_recursive(const char* dirpath) {
    DIR* dir = opendir(dirpath);
    if (!dir) return -1;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char filepath[TEST_MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);
        
        struct stat st;
        if (stat(filepath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                remove_directory_recursive(filepath);
            } else {
                unlink(filepath);
            }
        }
    }
    
    closedir(dir);
    return rmdir(dirpath);
}

/**
 * Performance measurement utilities
 */
void test_metrics_start(test_metrics_t* metrics) {
    if (!metrics) return;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    metrics->start_time_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    metrics->end_time_ns = 0;
    metrics->peak_memory_usage = 0;
    metrics->bytes_processed = 0;
    metrics->operations_completed = 0;
}

void test_metrics_end(test_metrics_t* metrics) {
    if (!metrics) return;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    metrics->end_time_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    
    // Record peak memory usage
    size_t current_memory = test_get_allocated_memory();
    if (current_memory > metrics->peak_memory_usage) {
        metrics->peak_memory_usage = current_memory;
    }
}

void test_metrics_add_bytes(test_metrics_t* metrics, size_t bytes) {
    if (metrics) metrics->bytes_processed += bytes;
}

void test_metrics_add_operation(test_metrics_t* metrics) {
    if (metrics) metrics->operations_completed++;
}

double test_metrics_get_duration_ms(const test_metrics_t* metrics) {
    if (!metrics || metrics->end_time_ns == 0) return 0.0;
    return (double)(metrics->end_time_ns - metrics->start_time_ns) / 1000000.0;
}

double test_metrics_get_throughput_mbps(const test_metrics_t* metrics) {
    double duration_ms = test_metrics_get_duration_ms(metrics);
    if (duration_ms <= 0.0 || !metrics) return 0.0;
    
    double bytes_per_second = (double)metrics->bytes_processed / (duration_ms / 1000.0);
    return bytes_per_second / (1024.0 * 1024.0); // Convert to MB/s
}

void test_metrics_print_summary(const test_metrics_t* metrics, const char* test_name) {
    if (!metrics || !test_name) return;
    
    double duration_ms = test_metrics_get_duration_ms(metrics);
    double throughput_mbps = test_metrics_get_throughput_mbps(metrics);
    
    char bytes_buffer[64];
    test_format_bytes(metrics->bytes_processed, bytes_buffer, sizeof(bytes_buffer));
    
    char memory_buffer[64];
    test_format_bytes(metrics->peak_memory_usage, memory_buffer, sizeof(memory_buffer));
    
    printf("%s Performance Summary:\n", test_name);
    printf("  Duration: %.2f ms\n", duration_ms);
    printf("  Bytes Processed: %s\n", bytes_buffer);
    printf("  Operations: %u\n", metrics->operations_completed);
    printf("  Peak Memory: %s\n", memory_buffer);
    printf("  Throughput: %.2f MB/s\n", throughput_mbps);
    printf("\n");
}

/**
 * Memory tracking utilities
 */
void* test_malloc_tracked(size_t size) {
    void* ptr = malloc(size);
    if (ptr && g_memory_tracking_enabled) {
        g_allocated_memory += size;
    }
    return ptr;
}

void test_free_tracked(void* ptr) {
    if (ptr) {
        free(ptr);
        // Note: We can't easily track the exact size being freed
        // In a real implementation, we'd maintain a hash table
    }
}

size_t test_get_allocated_memory(void) {
    return g_allocated_memory;
}

void test_reset_memory_tracking(void) {
    g_allocated_memory = 0;
}

bool test_check_memory_leaks(void) {
    return g_allocated_memory == 0;
}

/**
 * Random number utilities
 */
void test_seed_random(uint32_t seed) {
    g_random_state = seed;
}

uint32_t test_random_uint32(void) {
    // Simple LCG (Linear Congruential Generator)
    g_random_state = g_random_state * 1664525ULL + 1013904223ULL;
    return (uint32_t)(g_random_state >> 16);
}

uint64_t test_random_uint64(void) {
    return ((uint64_t)test_random_uint32() << 32) | test_random_uint32();
}

double test_random_double(void) {
    return (double)test_random_uint32() / (double)UINT32_MAX;
}

int test_random_int_range(int min, int max) {
    if (min >= max) return min;
    return min + (test_random_uint32() % (uint32_t)(max - min));
}

/**
 * Create temporary filename
 */
char* test_create_temp_filename(const char* prefix, const char* extension) {
    static char filename[TEST_MAX_PATH_LEN];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    snprintf(filename, sizeof(filename), "/tmp/%s_%ld_%ld%s",
             prefix ? prefix : "test", tv.tv_sec, tv.tv_usec,
             extension ? extension : ".tmp");
    
    return filename;
}

/**
 * Format bytes for human-readable output
 */
const char* test_format_bytes(size_t bytes, char* buffer, size_t buffer_size) {
    if (!buffer) return "";
    
    if (bytes < 1024) {
        snprintf(buffer, buffer_size, "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f KB", (double)bytes / 1024.0);
    } else if (bytes < 1024 * 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buffer, buffer_size, "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    }
    
    return buffer;
}
