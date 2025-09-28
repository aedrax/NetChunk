#include "unity.h"
#include "test_utils.h"
#include "netchunk.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

// Simple debug test to isolate the hanging issue
static netchunk_context_t netchunk_ctx;
static netchunk_config_t config;
static bool servers_available = false;

// Timeout handler
static void timeout_handler(int sig) {
    printf("Test timed out after 30 seconds - this indicates a hang in NetChunk code\n");
    exit(1);
}

void setUp(void) {
    // Set up timeout alarm
    signal(SIGALRM, timeout_handler);
    alarm(30); // 30 second timeout
    
    test_setup_environment();
    
    memset(&netchunk_ctx, 0, sizeof(netchunk_ctx));
    memset(&config, 0, sizeof(config));
    
    // Try to find and load config
    const char* possible_paths[] = {
        "../tests/integration/ftp-servers-config.conf",
        "tests/integration/ftp-servers-config.conf",
        "./tests/integration/ftp-servers-config.conf"
    };
    
    bool config_found = false;
    for (size_t i = 0; i < sizeof(possible_paths) / sizeof(possible_paths[0]); i++) {
        if (access(possible_paths[i], R_OK) == 0) {
            printf("Found config at: %s\n", possible_paths[i]);
            netchunk_error_t result = netchunk_config_load_file(&config, possible_paths[i]);
            if (result == NETCHUNK_SUCCESS) {
                config_found = true;
                printf("Config loaded successfully\n");
                break;
            } else {
                printf("Failed to load config: %s\n", netchunk_error_string(result));
            }
        }
    }
    
    if (!config_found) {
        printf("Could not find or load config file\n");
        servers_available = false;
        return;
    }
    
    printf("Attempting to initialize NetChunk context...\n");
    netchunk_ctx.config = &config;
    
    // Try to initialize - this might be where it hangs
    netchunk_error_t result = netchunk_init(&netchunk_ctx, possible_paths[0]);
    if (result != NETCHUNK_SUCCESS) {
        printf("NetChunk init failed: %s\n", netchunk_error_string(result));
        servers_available = false;
        return;
    }
    
    printf("NetChunk context initialized successfully\n");
    servers_available = true;
}

void tearDown(void) {
    alarm(0); // Cancel alarm
    
    if (servers_available) {
        printf("Cleaning up NetChunk context...\n");
        netchunk_cleanup(&netchunk_ctx);
    }
    
    netchunk_config_cleanup(&config);
    test_cleanup_environment();
}

// Simple test that just checks if we can initialize
void test_simple_init(void) {
    if (!servers_available) {
        TEST_IGNORE_MESSAGE("FTP servers not available");
    }
    
    TEST_ASSERT_TRUE(netchunk_ctx.initialized);
    TEST_ASSERT_NOT_NULL(netchunk_ctx.config);
    printf("NetChunk initialized with %d servers\n", netchunk_ctx.config->server_count);
}

// Test health check with timeout protection
void test_health_check_with_timeout(void) {
    if (!servers_available) {
        TEST_IGNORE_MESSAGE("FTP servers not available");
    }
    
    printf("Starting health check test...\n");
    
    uint32_t healthy_servers = 0;
    uint32_t total_servers = 0;
    
    // This is where it might hang - let's see if we can isolate it
    printf("Calling netchunk_health_check...\n");
    netchunk_error_t result = netchunk_health_check(&netchunk_ctx, &healthy_servers, &total_servers);
    
    printf("Health check completed with result: %s\n", netchunk_error_string(result));
    printf("Healthy servers: %u, Total servers: %u\n", healthy_servers, total_servers);
    
    // Don't fail if some servers are down, just verify the call completes
    TEST_ASSERT_NOT_EQUAL(NETCHUNK_ERROR_TIMEOUT, result);
    TEST_ASSERT_EQUAL_UINT32(7, total_servers);
}

// Unity test runner
int main(void) {
    UNITY_BEGIN();
    
    printf("\n=== NetChunk FTP Connection Debug Tests ===\n");
    printf("These tests will help isolate the hanging issue.\n\n");
    
    RUN_TEST(test_simple_init);
    RUN_TEST(test_health_check_with_timeout);
    
    return UNITY_END();
}
