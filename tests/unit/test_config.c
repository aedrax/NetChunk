#include "unity.h"
#include "test_utils.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Test data and fixtures
static netchunk_config_t test_config;
static test_file_context_t test_files;

void setUp(void) {
    // Initialize test environment
    test_setup_environment();
    
    // Create temporary directory for test files
    TEST_ASSERT_EQUAL_INT(0, create_temp_test_directory(&test_files));
    
    // Clear config structure
    memset(&test_config, 0, sizeof(test_config));
}

void tearDown(void) {
    // Cleanup config
    netchunk_config_cleanup(&test_config);
    
    // Remove temporary test files
    cleanup_temp_test_directory(&test_files);
    
    // Cleanup test environment
    test_cleanup_environment();
}

// Test config initialization with defaults
void test_config_init_defaults(void) {
    netchunk_error_t result = netchunk_config_init_defaults(&test_config);
    
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    TEST_ASSERT_EQUAL_size_t(NETCHUNK_DEFAULT_CHUNK_SIZE, test_config.chunk_size);
    TEST_ASSERT_EQUAL_INT(NETCHUNK_DEFAULT_REPLICATION_FACTOR, test_config.replication_factor);
    TEST_ASSERT_EQUAL_INT(4, test_config.max_concurrent_operations);
    TEST_ASSERT_EQUAL_INT(30, test_config.ftp_timeout);
    TEST_ASSERT_EQUAL_STRING("~/.netchunk/data", test_config.local_storage_path);
    TEST_ASSERT_EQUAL(NETCHUNK_LOG_INFO, test_config.log_level);
    TEST_ASSERT_EQUAL_STRING("~/.netchunk/netchunk.log", test_config.log_file);
    TEST_ASSERT_TRUE(test_config.health_monitoring_enabled);
    TEST_ASSERT_EQUAL_INT(300, test_config.health_check_interval);
    TEST_ASSERT_EQUAL_INT(0, test_config.server_count);
    TEST_ASSERT_TRUE(test_config.auto_repair_enabled);
    TEST_ASSERT_EQUAL_INT(3, test_config.max_repair_attempts);
    TEST_ASSERT_EQUAL_INT(10, test_config.repair_delay);
    TEST_ASSERT_TRUE(test_config.rebalancing_enabled);
    TEST_ASSERT_EQUAL_INT(85, test_config.storage_alert_threshold);
    TEST_ASSERT_EQUAL_INT(1000, test_config.latency_alert_threshold);
    TEST_ASSERT_FALSE(test_config.performance_logging);
    TEST_ASSERT_EQUAL_STRING("~/.netchunk/monitoring", test_config.monitoring_data_path);
    TEST_ASSERT_TRUE(test_config.verify_ssl_certificates);
    TEST_ASSERT_TRUE(test_config.always_verify_integrity);
    TEST_ASSERT_FALSE(test_config.encrypt_chunks);
}

void test_config_init_defaults_null_pointer(void) {
    netchunk_error_t result = netchunk_config_init_defaults(NULL);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
}

// Test configuration validation
void test_config_validate_valid_config(void) {
    // Initialize with defaults and add one server
    netchunk_config_init_defaults(&test_config);
    
    // Set replication factor to 1 to match server count
    test_config.replication_factor = 1;
    
    // Add a valid server
    test_config.server_count = 1;
    strcpy(test_config.servers[0].host, "ftp.example.com");
    test_config.servers[0].port = 21;
    strcpy(test_config.servers[0].username, "testuser");
    strcpy(test_config.servers[0].password, "testpass");
    strcpy(test_config.servers[0].base_path, "/upload");
    
    netchunk_error_t result = netchunk_config_validate(&test_config);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
}

void test_config_validate_null_pointer(void) {
    netchunk_error_t result = netchunk_config_validate(NULL);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
}

void test_config_validate_invalid_chunk_size(void) {
    netchunk_config_init_defaults(&test_config);
    
    // Set replication factor to 1 to match server count
    test_config.replication_factor = 1;
    
    // Add valid server
    test_config.server_count = 1;
    strcpy(test_config.servers[0].host, "ftp.example.com");
    test_config.servers[0].port = 21;
    strcpy(test_config.servers[0].username, "testuser");
    strcpy(test_config.servers[0].password, "testpass");
    strcpy(test_config.servers[0].base_path, "/upload");
    
    // Test chunk size too small
    test_config.chunk_size = NETCHUNK_MIN_CHUNK_SIZE - 1;
    netchunk_error_t result = netchunk_config_validate(&test_config);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_CONFIG_VALIDATION, result);
    
    // Test chunk size too large
    test_config.chunk_size = NETCHUNK_MAX_CHUNK_SIZE + 1;
    result = netchunk_config_validate(&test_config);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_CONFIG_VALIDATION, result);
}

void test_config_validate_invalid_replication_factor(void) {
    netchunk_config_init_defaults(&test_config);
    
    // Add valid servers
    test_config.server_count = 3;
    for (int i = 0; i < 3; i++) {
        snprintf(test_config.servers[i].host, sizeof(test_config.servers[i].host), "ftp%d.example.com", i+1);
        test_config.servers[i].port = 21;
        strcpy(test_config.servers[i].username, "testuser");
        strcpy(test_config.servers[i].password, "testpass");
        strcpy(test_config.servers[i].base_path, "/upload");
    }
    
    // Test replication factor too small
    test_config.replication_factor = NETCHUNK_MIN_REPLICATION_FACTOR - 1;
    netchunk_error_t result = netchunk_config_validate(&test_config);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_CONFIG_VALIDATION, result);
    
    // Test replication factor too large
    test_config.replication_factor = NETCHUNK_MAX_REPLICATION_FACTOR + 1;
    result = netchunk_config_validate(&test_config);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_CONFIG_VALIDATION, result);
}

void test_config_validate_no_servers(void) {
    netchunk_config_init_defaults(&test_config);
    test_config.server_count = 0;
    
    netchunk_error_t result = netchunk_config_validate(&test_config);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_CONFIG_VALIDATION, result);
}

void test_config_validate_insufficient_servers(void) {
    netchunk_config_init_defaults(&test_config);
    
    // Set replication factor higher than server count
    test_config.replication_factor = 3;
    test_config.server_count = 2;
    
    // Add servers
    for (int i = 0; i < 2; i++) {
        snprintf(test_config.servers[i].host, sizeof(test_config.servers[i].host), "ftp%d.example.com", i+1);
        test_config.servers[i].port = 21;
        strcpy(test_config.servers[i].username, "testuser");
        strcpy(test_config.servers[i].password, "testpass");
        strcpy(test_config.servers[i].base_path, "/upload");
    }
    
    netchunk_error_t result = netchunk_config_validate(&test_config);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INSUFFICIENT_SERVERS, result);
}

void test_config_validate_invalid_server_config(void) {
    netchunk_config_init_defaults(&test_config);
    test_config.server_count = 1;
    test_config.replication_factor = 1; // Set to match server count
    
    // Test empty host
    test_config.servers[0].host[0] = '\0';
    test_config.servers[0].port = 21;
    strcpy(test_config.servers[0].username, "testuser");
    strcpy(test_config.servers[0].base_path, "/upload");
    
    netchunk_error_t result = netchunk_config_validate(&test_config);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_CONFIG_VALIDATION, result);
    
    // Test invalid port
    strcpy(test_config.servers[0].host, "ftp.example.com");
    test_config.servers[0].port = 0;
    result = netchunk_config_validate(&test_config);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_CONFIG_VALIDATION, result);
    
    test_config.servers[0].port = 65536;
    result = netchunk_config_validate(&test_config);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_CONFIG_VALIDATION, result);
    
    // Test empty username
    test_config.servers[0].port = 21;
    test_config.servers[0].username[0] = '\0';
    result = netchunk_config_validate(&test_config);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_CONFIG_VALIDATION, result);
    
    // Test empty base_path
    strcpy(test_config.servers[0].username, "testuser");
    test_config.servers[0].base_path[0] = '\0';
    result = netchunk_config_validate(&test_config);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_CONFIG_VALIDATION, result);
}

// Test error string function
void test_error_string_mapping(void) {
    TEST_ASSERT_EQUAL_STRING("Success", netchunk_error_string(NETCHUNK_SUCCESS));
    TEST_ASSERT_EQUAL_STRING("Invalid argument", netchunk_error_string(NETCHUNK_ERROR_INVALID_ARGUMENT));
    TEST_ASSERT_EQUAL_STRING("Out of memory", netchunk_error_string(NETCHUNK_ERROR_OUT_OF_MEMORY));
    TEST_ASSERT_EQUAL_STRING("File not found", netchunk_error_string(NETCHUNK_ERROR_FILE_NOT_FOUND));
    TEST_ASSERT_EQUAL_STRING("Configuration validation error", netchunk_error_string(NETCHUNK_ERROR_CONFIG_VALIDATION));
    TEST_ASSERT_EQUAL_STRING("Insufficient servers", netchunk_error_string(NETCHUNK_ERROR_INSUFFICIENT_SERVERS));
    TEST_ASSERT_EQUAL_STRING("Unknown error", netchunk_error_string((netchunk_error_t)-999));
}

// Test log level parsing functions
void test_log_level_from_string(void) {
    TEST_ASSERT_EQUAL(NETCHUNK_LOG_ERROR, netchunk_log_level_from_string("ERROR"));
    TEST_ASSERT_EQUAL(NETCHUNK_LOG_ERROR, netchunk_log_level_from_string("error"));
    TEST_ASSERT_EQUAL(NETCHUNK_LOG_WARN, netchunk_log_level_from_string("WARN"));
    TEST_ASSERT_EQUAL(NETCHUNK_LOG_WARN, netchunk_log_level_from_string("WARNING"));
    TEST_ASSERT_EQUAL(NETCHUNK_LOG_INFO, netchunk_log_level_from_string("INFO"));
    TEST_ASSERT_EQUAL(NETCHUNK_LOG_DEBUG, netchunk_log_level_from_string("DEBUG"));
    TEST_ASSERT_EQUAL(NETCHUNK_LOG_INFO, netchunk_log_level_from_string("INVALID"));
    TEST_ASSERT_EQUAL(NETCHUNK_LOG_INFO, netchunk_log_level_from_string(NULL));
}

void test_log_level_to_string(void) {
    TEST_ASSERT_EQUAL_STRING("ERROR", netchunk_log_level_to_string(NETCHUNK_LOG_ERROR));
    TEST_ASSERT_EQUAL_STRING("WARN", netchunk_log_level_to_string(NETCHUNK_LOG_WARN));
    TEST_ASSERT_EQUAL_STRING("INFO", netchunk_log_level_to_string(NETCHUNK_LOG_INFO));
    TEST_ASSERT_EQUAL_STRING("DEBUG", netchunk_log_level_to_string(NETCHUNK_LOG_DEBUG));
    TEST_ASSERT_EQUAL_STRING("INFO", netchunk_log_level_to_string((netchunk_log_level_t)999));
}

// Test path expansion
void test_config_expand_path(void) {
    char expanded[NETCHUNK_MAX_PATH_LEN];
    
    // Test non-tilde path (no expansion)
    netchunk_error_t result = netchunk_config_expand_path("/etc/config", expanded, sizeof(expanded));
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    TEST_ASSERT_EQUAL_STRING("/etc/config", expanded);
    
    // Test tilde expansion (this will use actual HOME env var)
    result = netchunk_config_expand_path("~/test", expanded, sizeof(expanded));
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    TEST_ASSERT_TRUE(strstr(expanded, "/test") != NULL); // Should end with /test
    TEST_ASSERT_TRUE(expanded[0] == '/'); // Should start with /
    
    // Test just tilde
    result = netchunk_config_expand_path("~", expanded, sizeof(expanded));
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    TEST_ASSERT_TRUE(expanded[0] == '/'); // Should start with /
}

void test_config_expand_path_invalid_args(void) {
    char expanded[NETCHUNK_MAX_PATH_LEN];
    
    // Test null path
    netchunk_error_t result = netchunk_config_expand_path(NULL, expanded, sizeof(expanded));
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    // Test null output buffer
    result = netchunk_config_expand_path("~/test", NULL, sizeof(expanded));
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    // Test zero length buffer
    result = netchunk_config_expand_path("~/test", expanded, 0);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    // Test buffer too small for long path
    char small_buffer[5];
    result = netchunk_config_expand_path("/very/long/path/that/exceeds/buffer", small_buffer, sizeof(small_buffer));
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
}

// Test config file loading
void test_config_load_file_not_found(void) {
    netchunk_error_t result = netchunk_config_load_file(&test_config, "/nonexistent/config/file.conf");
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_FILE_NOT_FOUND, result);
}

void test_config_load_file_invalid_args(void) {
    // Test null config
    netchunk_error_t result = netchunk_config_load_file(NULL, "test.conf");
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    // Test null path
    result = netchunk_config_load_file(&test_config, NULL);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
}

void test_config_load_valid_file(void) {
    // Create a test config file
    char config_file[TEST_MAX_PATH_LEN];
    snprintf(config_file, sizeof(config_file), "%s/test.conf", test_files.temp_dir);
    
    FILE* fp = fopen(config_file, "w");
    TEST_ASSERT_NOT_NULL(fp);
    
    fprintf(fp, "[general]\n");
    fprintf(fp, "chunk_size=8MB\n");
    fprintf(fp, "replication_factor=2\n");
    fprintf(fp, "log_level=DEBUG\n");
    fprintf(fp, "\n");
    fprintf(fp, "[server_1]\n");
    fprintf(fp, "host=ftp1.example.com\n");
    fprintf(fp, "port=21\n");
    fprintf(fp, "username=user1\n");
    fprintf(fp, "password=pass1\n");
    fprintf(fp, "base_path=/upload\n");
    fprintf(fp, "use_ssl=true\n");
    fprintf(fp, "passive_mode=false\n");
    fprintf(fp, "\n");
    fprintf(fp, "[server_2]\n");
    fprintf(fp, "host=ftp2.example.com\n");
    fprintf(fp, "port=2121\n");
    fprintf(fp, "username=user2\n");
    fprintf(fp, "password=pass2\n");
    fprintf(fp, "base_path=/data\n");
    fprintf(fp, "priority=5\n");
    
    fclose(fp);
    
    // Load the config file
    netchunk_error_t result = netchunk_config_load_file(&test_config, config_file);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Verify parsed values
    TEST_ASSERT_EQUAL_size_t(8 * 1024 * 1024, test_config.chunk_size);
    TEST_ASSERT_EQUAL_INT(2, test_config.replication_factor);
    TEST_ASSERT_EQUAL(NETCHUNK_LOG_DEBUG, test_config.log_level);
    TEST_ASSERT_EQUAL_INT(2, test_config.server_count);
    
    // Verify server 1
    TEST_ASSERT_EQUAL_STRING("ftp1.example.com", test_config.servers[0].host);
    TEST_ASSERT_EQUAL_UINT16(21, test_config.servers[0].port);
    TEST_ASSERT_EQUAL_STRING("user1", test_config.servers[0].username);
    TEST_ASSERT_EQUAL_STRING("pass1", test_config.servers[0].password);
    TEST_ASSERT_EQUAL_STRING("/upload", test_config.servers[0].base_path);
    TEST_ASSERT_TRUE(test_config.servers[0].use_ssl);
    TEST_ASSERT_FALSE(test_config.servers[0].passive_mode);
    
    // Verify server 2
    TEST_ASSERT_EQUAL_STRING("ftp2.example.com", test_config.servers[1].host);
    TEST_ASSERT_EQUAL_UINT16(2121, test_config.servers[1].port);
    TEST_ASSERT_EQUAL_STRING("user2", test_config.servers[1].username);
    TEST_ASSERT_EQUAL_STRING("pass2", test_config.servers[1].password);
    TEST_ASSERT_EQUAL_STRING("/data", test_config.servers[1].base_path);
    TEST_ASSERT_EQUAL_INT(5, test_config.servers[1].priority);
}

// Test config file finding
void test_config_find_file_invalid_args(void) {
    // Test null config path
    netchunk_error_t result = netchunk_config_find_file(NULL, 512);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    // Test zero length
    char path[512];
    result = netchunk_config_find_file(path, 0);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
}

void test_config_find_file_not_found(void) {
    char path[512];
    // This should fail since we don't have any config files in standard locations in our test environment
    netchunk_error_t result = netchunk_config_find_file(path, sizeof(path));
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_FILE_NOT_FOUND, result);
}

// Test memory and cleanup
void test_config_cleanup(void) {
    netchunk_config_init_defaults(&test_config);
    
    // This should not crash - currently it's a no-op but exists for future expansion
    netchunk_config_cleanup(&test_config);
    TEST_PASS();
}

// Unity test runner
int main(void) {
    UNITY_BEGIN();
    
    // Initialization tests
    RUN_TEST(test_config_init_defaults);
    RUN_TEST(test_config_init_defaults_null_pointer);
    
    // Validation tests
    RUN_TEST(test_config_validate_valid_config);
    RUN_TEST(test_config_validate_null_pointer);
    RUN_TEST(test_config_validate_invalid_chunk_size);
    RUN_TEST(test_config_validate_invalid_replication_factor);
    RUN_TEST(test_config_validate_no_servers);
    RUN_TEST(test_config_validate_insufficient_servers);
    RUN_TEST(test_config_validate_invalid_server_config);
    
    // Error string tests
    RUN_TEST(test_error_string_mapping);
    
    // Log level tests
    RUN_TEST(test_log_level_from_string);
    RUN_TEST(test_log_level_to_string);
    
    // Path expansion tests
    RUN_TEST(test_config_expand_path);
    RUN_TEST(test_config_expand_path_invalid_args);
    
    // File loading tests
    RUN_TEST(test_config_load_file_not_found);
    RUN_TEST(test_config_load_file_invalid_args);
    RUN_TEST(test_config_load_valid_file);
    
    // File finding tests
    RUN_TEST(test_config_find_file_invalid_args);
    RUN_TEST(test_config_find_file_not_found);
    
    // Cleanup tests
    RUN_TEST(test_config_cleanup);
    
    return UNITY_END();
}
