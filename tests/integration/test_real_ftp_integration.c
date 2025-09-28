#include "unity.h"
#include "test_utils.h"
#include "netchunk.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// Test data and fixtures
static netchunk_context_t netchunk_ctx;
static test_file_context_t test_files;
static netchunk_config_t config;
static test_metrics_t metrics;
static bool servers_available = false;

// Progress callback for testing
static void test_progress_callback(void* userdata, const char* operation_name,
                                  uint64_t current, uint64_t total,
                                  uint64_t bytes_processed, uint64_t bytes_total) {
    (void)userdata;
    if (total > 0) {
        double percent = (double)current / total * 100.0;
        printf("  %s: %.1f%% (%llu/%llu, %llu bytes)\n", 
               operation_name, percent, 
               (unsigned long long)current, (unsigned long long)total,
               (unsigned long long)bytes_processed);
    }
}

void setUp(void) {
    // Initialize test environment
    test_setup_environment();
    
    // Create temporary directory for test files
    TEST_ASSERT_EQUAL_INT(0, create_temp_test_directory(&test_files));
    
    // Clear structures
    memset(&netchunk_ctx, 0, sizeof(netchunk_ctx));
    memset(&config, 0, sizeof(config));
    
    // Load configuration for real FTP servers
    char config_path[TEST_MAX_PATH_LEN];
    
    // Try multiple possible locations for the config file
    const char* possible_paths[] = {
        "../tests/integration/ftp-servers-config.conf",  // From build directory
        "tests/integration/ftp-servers-config.conf",     // From project root
        "./tests/integration/ftp-servers-config.conf"    // From current directory
    };
    
    bool config_found = false;
    for (size_t i = 0; i < sizeof(possible_paths) / sizeof(possible_paths[0]); i++) {
        if (access(possible_paths[i], R_OK) == 0) {
            strncpy(config_path, possible_paths[i], sizeof(config_path) - 1);
            config_path[sizeof(config_path) - 1] = '\0';
            config_found = true;
            break;
        }
    }
    
    if (!config_found) {
        printf("Warning: Could not find FTP server config file\n");
        printf("Looked in: %s, %s, %s\n", possible_paths[0], possible_paths[1], possible_paths[2]);
        printf("Make sure Docker FTP servers are configured and running\n");
        servers_available = false;
        return;
    }
    
    netchunk_error_t result = netchunk_config_load_file(&config, config_path);
    if (result != NETCHUNK_SUCCESS) {
        printf("Warning: Could not load FTP server config from %s\n", config_path);
        printf("Make sure Docker FTP servers are configured and running\n");
        servers_available = false;
        return;
    }
    
    // Initialize NetChunk context with the config
    netchunk_ctx.config = &config;
    result = netchunk_init(&netchunk_ctx, config_path);
    if (result != NETCHUNK_SUCCESS) {
        printf("Warning: Could not initialize NetChunk context\n");
        printf("Error: %s\n", netchunk_error_string(result));
        servers_available = false;
        return;
    }
    
    // Set up progress callback
    netchunk_set_progress_callback(&netchunk_ctx, test_progress_callback, NULL);
    
    servers_available = true;
    
    // Start performance metrics
    test_metrics_start(&metrics);
}

void tearDown(void) {
    if (servers_available) {
        netchunk_cleanup(&netchunk_ctx);
    }
    
    netchunk_config_cleanup(&config);
    cleanup_temp_test_directory(&test_files);
    test_cleanup_environment();
    
    // End performance metrics
    test_metrics_end(&metrics);
}

// Test NetChunk initialization with real FTP servers
void test_netchunk_init_with_real_servers(void) {
    if (!servers_available) {
        TEST_IGNORE_MESSAGE("FTP servers not available - run 'tests/docker/manage-ftp-servers.sh start'");
    }
    
    TEST_ASSERT_TRUE(netchunk_ctx.initialized);
    TEST_ASSERT_NOT_NULL(netchunk_ctx.config);
    TEST_ASSERT_EQUAL_INT(7, netchunk_ctx.config->server_count);
    TEST_ASSERT_EQUAL_INT(3, netchunk_ctx.config->replication_factor);
}

// Test health check of real FTP servers
void test_netchunk_health_check_real_servers(void) {
    if (!servers_available) {
        TEST_IGNORE_MESSAGE("FTP servers not available - run 'tests/docker/manage-ftp-servers.sh start'");
    }
    
    uint32_t healthy_servers = 0;
    uint32_t total_servers = 0;
    
    netchunk_error_t result = netchunk_health_check(&netchunk_ctx, &healthy_servers, &total_servers);
    
    printf("Health check result: %d healthy out of %d total servers\n", healthy_servers, total_servers);
    
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    TEST_ASSERT_EQUAL_UINT32(7, total_servers);
    // We should have at least some servers healthy (allow for some to be down)
    TEST_ASSERT_GREATER_THAN_UINT32(0, healthy_servers);
}

// Test file upload to real FTP servers
void test_netchunk_upload_small_file(void) {
    if (!servers_available) {
        TEST_IGNORE_MESSAGE("FTP servers not available - run 'tests/docker/manage-ftp-servers.sh start'");
    }
    
    // Create a test file
    char test_file[TEST_MAX_PATH_LEN];
    snprintf(test_file, sizeof(test_file), "%s/small_test.txt", test_files.temp_dir);
    
    const char* test_content = "Hello NetChunk! This is a test file for integration testing with real FTP servers.";
    FILE* fp = fopen(test_file, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fputs(test_content, fp);
    fclose(fp);
    
    // Upload the file
    netchunk_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    
    printf("Uploading test file...\n");
    netchunk_error_t result = netchunk_upload(&netchunk_ctx, test_file, "small_test.txt", &stats);
    
    printf("Upload completed with result: %s\n", netchunk_error_string(result));
    printf("Stats: %llu bytes, %u chunks, %u servers, %.2f seconds, %u retries\n",
           (unsigned long long)stats.bytes_processed, stats.chunks_processed, 
           stats.servers_used, stats.elapsed_seconds, stats.retries_performed);
    
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    TEST_ASSERT_GREATER_THAN_UINT64(0, stats.bytes_processed);
    TEST_ASSERT_GREATER_THAN_UINT32(0, stats.chunks_processed);
    TEST_ASSERT_GREATER_THAN_UINT32(0, stats.servers_used);
}

// Test file download from real FTP servers
void test_netchunk_download_small_file(void) {
    if (!servers_available) {
        TEST_IGNORE_MESSAGE("FTP servers not available - run 'tests/docker/manage-ftp-servers.sh start'");
    }
    
    // First upload a file (assuming previous test ran)
    char test_file[TEST_MAX_PATH_LEN];
    snprintf(test_file, sizeof(test_file), "%s/upload_test.txt", test_files.temp_dir);
    
    const char* test_content = "Download test content for NetChunk integration testing.";
    FILE* fp = fopen(test_file, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fputs(test_content, fp);
    fclose(fp);
    
    // Upload first
    printf("Uploading file for download test...\n");
    netchunk_error_t result = netchunk_upload(&netchunk_ctx, test_file, "download_test.txt", NULL);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Now download to a different file
    char download_file[TEST_MAX_PATH_LEN];
    snprintf(download_file, sizeof(download_file), "%s/downloaded_test.txt", test_files.temp_dir);
    
    netchunk_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    
    printf("Downloading test file...\n");
    result = netchunk_download(&netchunk_ctx, "download_test.txt", download_file, &stats);
    
    printf("Download completed with result: %s\n", netchunk_error_string(result));
    printf("Stats: %llu bytes, %u chunks, %u servers, %.2f seconds, %u retries\n",
           (unsigned long long)stats.bytes_processed, stats.chunks_processed, 
           stats.servers_used, stats.elapsed_seconds, stats.retries_performed);
    
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Verify the downloaded file matches the original
    TEST_ASSERT_EQUAL_INT(0, compare_files(test_file, download_file));
    
    // Verify file contents
    fp = fopen(download_file, "r");
    TEST_ASSERT_NOT_NULL(fp);
    
    char buffer[256];
    fgets(buffer, sizeof(buffer), fp);
    fclose(fp);
    
    TEST_ASSERT_EQUAL_STRING(test_content, buffer);
}

// Test file listing with real FTP servers
void test_netchunk_list_files(void) {
    if (!servers_available) {
        TEST_IGNORE_MESSAGE("FTP servers not available - run 'tests/docker/manage-ftp-servers.sh start'");
    }
    
    // Upload a couple of test files first
    char test_file1[TEST_MAX_PATH_LEN];
    char test_file2[TEST_MAX_PATH_LEN];
    snprintf(test_file1, sizeof(test_file1), "%s/list_test1.txt", test_files.temp_dir);
    snprintf(test_file2, sizeof(test_file2), "%s/list_test2.txt", test_files.temp_dir);
    
    // Create test files
    FILE* fp = fopen(test_file1, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fputs("List test file 1", fp);
    fclose(fp);
    
    fp = fopen(test_file2, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fputs("List test file 2", fp);
    fclose(fp);
    
    // Upload them
    printf("Uploading files for list test...\n");
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, 
                     netchunk_upload(&netchunk_ctx, test_file1, "list_test1.txt", NULL));
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, 
                     netchunk_upload(&netchunk_ctx, test_file2, "list_test2.txt", NULL));
    
    // List files
    netchunk_file_manifest_t* files = NULL;
    size_t count = 0;
    
    printf("Listing files...\n");
    netchunk_error_t result = netchunk_list_files(&netchunk_ctx, &files, &count);
    
    printf("List completed with result: %s\n", netchunk_error_string(result));
    printf("Found %zu files\n", count);
    
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    TEST_ASSERT_GREATER_THAN_size_t(0, count);
    TEST_ASSERT_NOT_NULL(files);
    
    // Print file information
    for (size_t i = 0; i < count; i++) {
        printf("  File %zu: %s (%llu bytes, %u chunks)\n", 
               i + 1, files[i].original_filename, 
               (unsigned long long)files[i].total_size, 
               files[i].chunk_count);
    }
    
    // Cleanup
    netchunk_free_file_list(files, count);
}

// Test file verification with real FTP servers
void test_netchunk_verify_file(void) {
    if (!servers_available) {
        TEST_IGNORE_MESSAGE("FTP servers not available - run 'tests/docker/manage-ftp-servers.sh start'");
    }
    
    // Upload a test file first
    char test_file[TEST_MAX_PATH_LEN];
    snprintf(test_file, sizeof(test_file), "%s/verify_test.txt", test_files.temp_dir);
    
    const char* test_content = "This file will be verified for integrity testing.";
    FILE* fp = fopen(test_file, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fputs(test_content, fp);
    fclose(fp);
    
    printf("Uploading file for verification test...\n");
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, 
                     netchunk_upload(&netchunk_ctx, test_file, "verify_test.txt", NULL));
    
    // Verify the file
    uint32_t chunks_verified = 0;
    uint32_t chunks_repaired = 0;
    
    printf("Verifying file integrity...\n");
    netchunk_error_t result = netchunk_verify(&netchunk_ctx, "verify_test.txt", true, 
                                             &chunks_verified, &chunks_repaired);
    
    printf("Verification completed with result: %s\n", netchunk_error_string(result));
    printf("Chunks verified: %u, Chunks repaired: %u\n", chunks_verified, chunks_repaired);
    
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    TEST_ASSERT_GREATER_THAN_UINT32(0, chunks_verified);
    // Repairs should be 0 for a freshly uploaded file
    TEST_ASSERT_EQUAL_UINT32(0, chunks_repaired);
}

// Test file deletion with real FTP servers
void test_netchunk_delete_file(void) {
    if (!servers_available) {
        TEST_IGNORE_MESSAGE("FTP servers not available - run 'tests/docker/manage-ftp-servers.sh start'");
    }
    
    // Upload a test file first
    char test_file[TEST_MAX_PATH_LEN];
    snprintf(test_file, sizeof(test_file), "%s/delete_test.txt", test_files.temp_dir);
    
    FILE* fp = fopen(test_file, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fputs("This file will be deleted", fp);
    fclose(fp);
    
    printf("Uploading file for deletion test...\n");
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, 
                     netchunk_upload(&netchunk_ctx, test_file, "delete_test.txt", NULL));
    
    // Delete the file
    printf("Deleting file...\n");
    netchunk_error_t result = netchunk_delete(&netchunk_ctx, "delete_test.txt");
    
    printf("Deletion completed with result: %s\n", netchunk_error_string(result));
    
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Verify file is gone by trying to download it
    char download_file[TEST_MAX_PATH_LEN];
    snprintf(download_file, sizeof(download_file), "%s/should_not_exist.txt", test_files.temp_dir);
    
    result = netchunk_download(&netchunk_ctx, "delete_test.txt", download_file, NULL);
    TEST_ASSERT_NOT_EQUAL(NETCHUNK_SUCCESS, result);
    TEST_ASSERT_FALSE(file_exists(download_file));
}

// Test larger file handling
void test_netchunk_large_file_upload_download(void) {
    if (!servers_available) {
        TEST_IGNORE_MESSAGE("FTP servers not available - run 'tests/docker/manage-ftp-servers.sh start'");
    }
    
    // Create a larger test file (multiple chunks)
    char test_file[TEST_MAX_PATH_LEN];
    snprintf(test_file, sizeof(test_file), "%s/large_test.dat", test_files.temp_dir);
    
    size_t file_size = 10 * 1024 * 1024; // 10MB - should create multiple 4MB chunks
    printf("Creating %zu byte test file...\n", file_size);
    
    TEST_ASSERT_EQUAL_INT(0, generate_test_file(test_file, file_size, TEST_PATTERN_INCREMENTAL));
    
    // Upload the large file
    netchunk_stats_t upload_stats;
    memset(&upload_stats, 0, sizeof(upload_stats));
    
    printf("Uploading large file...\n");
    netchunk_error_t result = netchunk_upload(&netchunk_ctx, test_file, "large_test.dat", &upload_stats);
    
    printf("Large file upload completed with result: %s\n", netchunk_error_string(result));
    printf("Upload stats: %llu bytes, %u chunks, %u servers, %.2f seconds\n",
           (unsigned long long)upload_stats.bytes_processed, upload_stats.chunks_processed, 
           upload_stats.servers_used, upload_stats.elapsed_seconds);
    
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    TEST_ASSERT_EQUAL_UINT64(file_size, upload_stats.bytes_processed);
    TEST_ASSERT_GREATER_THAN_UINT32(1, upload_stats.chunks_processed); // Should be multiple chunks
    
    // Download the large file
    char download_file[TEST_MAX_PATH_LEN];
    snprintf(download_file, sizeof(download_file), "%s/large_downloaded.dat", test_files.temp_dir);
    
    netchunk_stats_t download_stats;
    memset(&download_stats, 0, sizeof(download_stats));
    
    printf("Downloading large file...\n");
    result = netchunk_download(&netchunk_ctx, "large_test.dat", download_file, &download_stats);
    
    printf("Large file download completed with result: %s\n", netchunk_error_string(result));
    printf("Download stats: %llu bytes, %u chunks, %u servers, %.2f seconds\n",
           (unsigned long long)download_stats.bytes_processed, download_stats.chunks_processed, 
           download_stats.servers_used, download_stats.elapsed_seconds);
    
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    TEST_ASSERT_EQUAL_UINT64(file_size, download_stats.bytes_processed);
    
    // Verify the files are identical
    printf("Verifying file integrity...\n");
    TEST_ASSERT_EQUAL_INT(0, compare_files(test_file, download_file));
    
    test_metrics_add_bytes(&metrics, file_size * 2); // Upload + download
    test_metrics_add_operation(&metrics);
}

// Test version information
void test_netchunk_version_info(void) {
    int major, minor, patch;
    const char* version_string;
    
    netchunk_get_version(&major, &minor, &patch, &version_string);
    
    TEST_ASSERT_EQUAL_INT(NETCHUNK_VERSION_MAJOR, major);
    TEST_ASSERT_EQUAL_INT(NETCHUNK_VERSION_MINOR, minor);
    TEST_ASSERT_EQUAL_INT(NETCHUNK_VERSION_PATCH, patch);
    TEST_ASSERT_EQUAL_STRING(NETCHUNK_VERSION_STRING, version_string);
    
    printf("NetChunk version: %d.%d.%d (%s)\n", major, minor, patch, version_string);
}

// Unity test runner
int main(void) {
    UNITY_BEGIN();
    
    printf("\n=== NetChunk Real FTP Server Integration Tests ===\n");
    printf("These tests require Docker FTP servers to be running.\n");
    printf("Run: tests/docker/manage-ftp-servers.sh start\n\n");
    
    // Basic tests
    RUN_TEST(test_netchunk_version_info);
    RUN_TEST(test_netchunk_init_with_real_servers);
    RUN_TEST(test_netchunk_health_check_real_servers);
    
    // File operation tests
    RUN_TEST(test_netchunk_upload_small_file);
    RUN_TEST(test_netchunk_download_small_file);
    RUN_TEST(test_netchunk_list_files);
    RUN_TEST(test_netchunk_verify_file);
    RUN_TEST(test_netchunk_delete_file);
    
    // Performance tests
    RUN_TEST(test_netchunk_large_file_upload_download);
    
    printf("\n=== Integration Test Performance Summary ===\n");
    test_metrics_print_summary(&metrics, "Integration Tests");
    
    return UNITY_END();
}
