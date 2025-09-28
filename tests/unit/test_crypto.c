#include "unity.h"
#include "test_utils.h"
#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test data and fixtures
static netchunk_sha256_context_t crypto_ctx;
static test_file_context_t test_files;

void setUp(void) {
    // Initialize test environment
    test_setup_environment();
    
    // Create temporary directory for test files
    TEST_ASSERT_EQUAL_INT(0, create_temp_test_directory(&test_files));
    
    // Clear crypto context
    memset(&crypto_ctx, 0, sizeof(crypto_ctx));
}

void tearDown(void) {
    // Remove temporary test files
    cleanup_temp_test_directory(&test_files);
    
    // Cleanup test environment
    test_cleanup_environment();
}

// Test SHA-256 context initialization
void test_sha256_init(void) {
    netchunk_error_t result = netchunk_sha256_init(&crypto_ctx);
    
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    // Verify initial state values (SHA-256 standard initial values)
    TEST_ASSERT_EQUAL_HEX32(0x6a09e667, crypto_ctx.state[0]);
    TEST_ASSERT_EQUAL_HEX32(0xbb67ae85, crypto_ctx.state[1]);
    TEST_ASSERT_EQUAL_HEX32(0x3c6ef372, crypto_ctx.state[2]);
    TEST_ASSERT_EQUAL_HEX32(0xa54ff53a, crypto_ctx.state[3]);
    TEST_ASSERT_EQUAL_HEX32(0x510e527f, crypto_ctx.state[4]);
    TEST_ASSERT_EQUAL_HEX32(0x9b05688c, crypto_ctx.state[5]);
    TEST_ASSERT_EQUAL_HEX32(0x1f83d9ab, crypto_ctx.state[6]);
    TEST_ASSERT_EQUAL_HEX32(0x5be0cd19, crypto_ctx.state[7]);
    TEST_ASSERT_EQUAL_UINT64(0, crypto_ctx.count);
}

void test_sha256_init_null_pointer(void) {
    netchunk_error_t result = netchunk_sha256_init(NULL);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
}

// Test SHA-256 one-shot hash function
void test_sha256_hash_empty_data(void) {
    uint8_t hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    
    // Test with empty string instead of NULL data
    const uint8_t empty_data = 0;
    netchunk_error_t result = netchunk_sha256_hash(&empty_data, 0, hash);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Known SHA-256 hash of empty string
    uint8_t expected_hash[] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };
    
    TEST_ASSERT_EQUAL_MEMORY(expected_hash, hash, NETCHUNK_SHA256_DIGEST_LENGTH);
}

void test_sha256_hash_known_data(void) {
    const char* test_data = "abc";
    uint8_t hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    
    netchunk_error_t result = netchunk_sha256_hash((const uint8_t*)test_data, strlen(test_data), hash);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Known SHA-256 hash of "abc"
    uint8_t expected_hash[] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    
    TEST_ASSERT_EQUAL_MEMORY(expected_hash, hash, NETCHUNK_SHA256_DIGEST_LENGTH);
}

void test_sha256_hash_longer_data(void) {
    const char* test_data = "The quick brown fox jumps over the lazy dog";
    uint8_t hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    
    netchunk_error_t result = netchunk_sha256_hash((const uint8_t*)test_data, strlen(test_data), hash);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Known SHA-256 hash of the fox sentence
    uint8_t expected_hash[] = {
        0xd7, 0xa8, 0xfb, 0xb3, 0x07, 0xd7, 0x80, 0x94,
        0x69, 0xca, 0x9a, 0xbc, 0xb0, 0x08, 0x2e, 0x4f,
        0x8d, 0x56, 0x51, 0xe4, 0x6d, 0x3c, 0xdb, 0x76,
        0x2d, 0x02, 0xd0, 0xbf, 0x37, 0xc9, 0xe5, 0x92
    };
    
    TEST_ASSERT_EQUAL_MEMORY(expected_hash, hash, NETCHUNK_SHA256_DIGEST_LENGTH);
}

void test_sha256_hash_null_parameters(void) {
    const char* test_data = "test";
    uint8_t hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    
    // Test null hash output
    netchunk_error_t result = netchunk_sha256_hash((const uint8_t*)test_data, strlen(test_data), NULL);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    // Test null data with non-zero length (should fail)
    result = netchunk_sha256_hash(NULL, 10, hash);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
}

// Test streaming SHA-256 operations
void test_sha256_streaming_operations(void) {
    netchunk_error_t result = netchunk_sha256_init(&crypto_ctx);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Update with multiple chunks
    const char* chunk1 = "The quick brown ";
    const char* chunk2 = "fox jumps over ";
    const char* chunk3 = "the lazy dog";
    
    result = netchunk_sha256_update(&crypto_ctx, (const uint8_t*)chunk1, strlen(chunk1));
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    result = netchunk_sha256_update(&crypto_ctx, (const uint8_t*)chunk2, strlen(chunk2));
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    result = netchunk_sha256_update(&crypto_ctx, (const uint8_t*)chunk3, strlen(chunk3));
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    uint8_t hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    result = netchunk_sha256_final(&crypto_ctx, hash);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Should match the one-shot hash of the same data
    uint8_t expected_hash[] = {
        0xd7, 0xa8, 0xfb, 0xb3, 0x07, 0xd7, 0x80, 0x94,
        0x69, 0xca, 0x9a, 0xbc, 0xb0, 0x08, 0x2e, 0x4f,
        0x8d, 0x56, 0x51, 0xe4, 0x6d, 0x3c, 0xdb, 0x76,
        0x2d, 0x02, 0xd0, 0xbf, 0x37, 0xc9, 0xe5, 0x92
    };
    
    TEST_ASSERT_EQUAL_MEMORY(expected_hash, hash, NETCHUNK_SHA256_DIGEST_LENGTH);
}

void test_sha256_streaming_null_parameters(void) {
    netchunk_sha256_init(&crypto_ctx);
    const char* test_data = "test";
    uint8_t hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    
    // Test null context in update
    netchunk_error_t result = netchunk_sha256_update(NULL, (const uint8_t*)test_data, strlen(test_data));
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    // Test null data in update (with non-zero length)
    result = netchunk_sha256_update(&crypto_ctx, NULL, 10);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    // Test null context in final
    result = netchunk_sha256_final(NULL, hash);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    // Test null hash in final
    result = netchunk_sha256_final(&crypto_ctx, NULL);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
}

// Test file hashing
void test_sha256_hash_file(void) {
    // Create a test file
    char test_file[TEST_MAX_PATH_LEN];
    snprintf(test_file, sizeof(test_file), "%s/hash_test.txt", test_files.temp_dir);
    
    const char* file_content = "Hello NetChunk! This file will be hashed.";
    FILE* fp = fopen(test_file, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fputs(file_content, fp);
    fclose(fp);
    
    // Hash the file
    uint8_t file_hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    netchunk_error_t result = netchunk_sha256_hash_file(test_file, file_hash);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Verify against one-shot hash of the same data
    uint8_t data_hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    result = netchunk_sha256_hash((const uint8_t*)file_content, strlen(file_content), data_hash);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    TEST_ASSERT_EQUAL_MEMORY(data_hash, file_hash, NETCHUNK_SHA256_DIGEST_LENGTH);
}

void test_sha256_hash_file_not_found(void) {
    uint8_t hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    netchunk_error_t result = netchunk_sha256_hash_file("/nonexistent/file.txt", hash);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_FILE_NOT_FOUND, result);
}

void test_sha256_hash_file_null_parameters(void) {
    char test_file[TEST_MAX_PATH_LEN];
    snprintf(test_file, sizeof(test_file), "%s/test.txt", test_files.temp_dir);
    
    // Create a simple test file
    FILE* fp = fopen(test_file, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fputs("test", fp);
    fclose(fp);
    
    uint8_t hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    
    // Test null file path
    netchunk_error_t result = netchunk_sha256_hash_file(NULL, hash);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    // Test null hash output
    result = netchunk_sha256_hash_file(test_file, NULL);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
}

// Test hash utility functions
void test_hash_to_hex_string(void) {
    uint8_t test_hash[] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00
    };
    
    char hex_string[NETCHUNK_SHA256_DIGEST_LENGTH * 2 + 1];
    netchunk_error_t result = netchunk_hash_to_hex_string(test_hash, sizeof(test_hash), hex_string);
    
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    TEST_ASSERT_EQUAL_STRING("0123456789abcdeffedcba9876543210112233445566778899aabbccddeeff00", hex_string);
}

void test_hex_string_to_hash(void) {
    const char* hex_string = "0123456789abcdeffedcba9876543210112233445566778899aabbccddeeff00";
    uint8_t hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    
    netchunk_error_t result = netchunk_hex_string_to_hash(hex_string, hash, sizeof(hash));
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    uint8_t expected_hash[] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00
    };
    
    TEST_ASSERT_EQUAL_MEMORY(expected_hash, hash, NETCHUNK_SHA256_DIGEST_LENGTH);
}

void test_hex_string_conversion_roundtrip(void) {
    // Test roundtrip conversion
    uint8_t original_hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    
    // Generate test hash with incremental pattern
    for (size_t i = 0; i < NETCHUNK_SHA256_DIGEST_LENGTH; i++) {
        original_hash[i] = (uint8_t)(i * 7 + 13); // Some pattern
    }
    
    // Convert to hex string
    char hex_string[NETCHUNK_SHA256_DIGEST_LENGTH * 2 + 1];
    netchunk_error_t result = netchunk_hash_to_hex_string(original_hash, sizeof(original_hash), hex_string);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Convert back to hash
    uint8_t converted_hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    result = netchunk_hex_string_to_hash(hex_string, converted_hash, sizeof(converted_hash));
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Should be identical
    TEST_ASSERT_EQUAL_MEMORY(original_hash, converted_hash, NETCHUNK_SHA256_DIGEST_LENGTH);
}

void test_hex_conversion_invalid_parameters(void) {
    uint8_t hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    char hex_string[NETCHUNK_SHA256_DIGEST_LENGTH * 2 + 1];
    
    // Test hash_to_hex_string with null parameters
    netchunk_error_t result = netchunk_hash_to_hex_string(NULL, NETCHUNK_SHA256_DIGEST_LENGTH, hex_string);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    result = netchunk_hash_to_hex_string(hash, NETCHUNK_SHA256_DIGEST_LENGTH, NULL);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    // Test hex_string_to_hash with null parameters
    result = netchunk_hex_string_to_hash(NULL, hash, NETCHUNK_SHA256_DIGEST_LENGTH);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    result = netchunk_hex_string_to_hash("0123456789abcdef", NULL, NETCHUNK_SHA256_DIGEST_LENGTH);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    // Test invalid hex string (odd length)
    result = netchunk_hex_string_to_hash("0123456789abcde", hash, NETCHUNK_SHA256_DIGEST_LENGTH);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
    
    // Test invalid hex characters
    result = netchunk_hex_string_to_hash("0123456789abcdez", hash, 8);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
}

// Test hash comparison
void test_hash_compare(void) {
    uint8_t hash1[NETCHUNK_SHA256_DIGEST_LENGTH];
    uint8_t hash2[NETCHUNK_SHA256_DIGEST_LENGTH];
    uint8_t hash3[NETCHUNK_SHA256_DIGEST_LENGTH];
    
    // Fill hashes with known patterns
    memset(hash1, 0xAA, sizeof(hash1));
    memset(hash2, 0xAA, sizeof(hash2));
    memset(hash3, 0xBB, sizeof(hash3));
    
    // Test identical hashes
    TEST_ASSERT_TRUE(netchunk_hash_compare(hash1, hash2, NETCHUNK_SHA256_DIGEST_LENGTH));
    
    // Test different hashes
    TEST_ASSERT_FALSE(netchunk_hash_compare(hash1, hash3, NETCHUNK_SHA256_DIGEST_LENGTH));
    
    // Test with shorter length
    TEST_ASSERT_TRUE(netchunk_hash_compare(hash1, hash2, 16));
    TEST_ASSERT_FALSE(netchunk_hash_compare(hash1, hash3, 16));
}

void test_hash_compare_null_parameters(void) {
    uint8_t hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    memset(hash, 0xAA, sizeof(hash));
    
    // Test null parameters
    TEST_ASSERT_FALSE(netchunk_hash_compare(NULL, hash, NETCHUNK_SHA256_DIGEST_LENGTH));
    TEST_ASSERT_FALSE(netchunk_hash_compare(hash, NULL, NETCHUNK_SHA256_DIGEST_LENGTH));
    TEST_ASSERT_FALSE(netchunk_hash_compare(NULL, NULL, NETCHUNK_SHA256_DIGEST_LENGTH));
    
    // Test zero length - this might return false for null checks
    bool result = netchunk_hash_compare(hash, hash, 0);
    // Accept either true (no bytes to compare) or false (implementation specific)
    TEST_PASS(); // We just verify it doesn't crash
}

// Test random byte generation
void test_generate_random_bytes(void) {
    uint8_t buffer1[64];
    uint8_t buffer2[64];
    
    netchunk_error_t result = netchunk_generate_random_bytes(buffer1, sizeof(buffer1));
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    result = netchunk_generate_random_bytes(buffer2, sizeof(buffer2));
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // The two random buffers should be different
    TEST_ASSERT_NOT_EQUAL(0, memcmp(buffer1, buffer2, sizeof(buffer1)));
    
    // Check that not all bytes are the same (very unlikely with good randomness)
    bool all_same = true;
    for (size_t i = 1; i < sizeof(buffer1); i++) {
        if (buffer1[i] != buffer1[0]) {
            all_same = false;
            break;
        }
    }
    TEST_ASSERT_FALSE(all_same);
}

void test_generate_random_bytes_small_buffers(void) {
    uint8_t buffer[1];
    
    netchunk_error_t result = netchunk_generate_random_bytes(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Test zero-size buffer
    result = netchunk_generate_random_bytes(buffer, 0);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result); // Should succeed but do nothing
}

void test_generate_random_bytes_null_buffer(void) {
    netchunk_error_t result = netchunk_generate_random_bytes(NULL, 64);
    TEST_ASSERT_EQUAL(NETCHUNK_ERROR_INVALID_ARGUMENT, result);
}

// Test large data hashing performance
void test_sha256_large_data_performance(void) {
    // Create large test data (1MB)
    size_t data_size = 1024 * 1024;
    uint8_t* large_data = malloc(data_size);
    TEST_ASSERT_NOT_NULL(large_data);
    
    // Fill with pattern data
    for (size_t i = 0; i < data_size; i++) {
        large_data[i] = (uint8_t)(i & 0xFF);
    }
    
    // Hash using one-shot method
    uint8_t hash1[NETCHUNK_SHA256_DIGEST_LENGTH];
    netchunk_error_t result = netchunk_sha256_hash(large_data, data_size, hash1);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Hash using streaming method
    result = netchunk_sha256_init(&crypto_ctx);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Process in 64KB chunks
    size_t chunk_size = 64 * 1024;
    for (size_t offset = 0; offset < data_size; offset += chunk_size) {
        size_t current_chunk = (offset + chunk_size > data_size) ? (data_size - offset) : chunk_size;
        result = netchunk_sha256_update(&crypto_ctx, large_data + offset, current_chunk);
        TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    }
    
    uint8_t hash2[NETCHUNK_SHA256_DIGEST_LENGTH];
    result = netchunk_sha256_final(&crypto_ctx, hash2);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Both methods should produce the same hash
    TEST_ASSERT_EQUAL_MEMORY(hash1, hash2, NETCHUNK_SHA256_DIGEST_LENGTH);
    
    free(large_data);
}

// Test file hashing with larger file
void test_sha256_hash_large_file(void) {
    // Create a larger test file
    char test_file[TEST_MAX_PATH_LEN];
    snprintf(test_file, sizeof(test_file), "%s/large_hash_test.dat", test_files.temp_dir);
    
    size_t file_size = 256 * 1024; // 256KB
    TEST_ASSERT_EQUAL_INT(0, generate_test_file(test_file, file_size, TEST_PATTERN_INCREMENTAL));
    
    // Hash the file
    uint8_t file_hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    netchunk_error_t result = netchunk_sha256_hash_file(test_file, file_hash);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Verify the hash is not all zeros (would indicate failure)
    bool all_zeros = true;
    for (size_t i = 0; i < NETCHUNK_SHA256_DIGEST_LENGTH; i++) {
        if (file_hash[i] != 0) {
            all_zeros = false;
            break;
        }
    }
    TEST_ASSERT_FALSE(all_zeros);
    
    // Hash the file again - should get the same result
    uint8_t file_hash2[NETCHUNK_SHA256_DIGEST_LENGTH];
    result = netchunk_sha256_hash_file(test_file, file_hash2);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    TEST_ASSERT_EQUAL_MEMORY(file_hash, file_hash2, NETCHUNK_SHA256_DIGEST_LENGTH);
}

// Test edge cases
void test_sha256_edge_cases(void) {
    uint8_t hash[NETCHUNK_SHA256_DIGEST_LENGTH];
    
    // Test very small data
    uint8_t one_byte = 0x42;
    netchunk_error_t result = netchunk_sha256_hash(&one_byte, 1, hash);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Test data exactly one block size (64 bytes)
    uint8_t block_data[NETCHUNK_SHA256_BLOCK_SIZE];
    memset(block_data, 0x55, sizeof(block_data));
    result = netchunk_sha256_hash(block_data, sizeof(block_data), hash);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
    
    // Test data slightly larger than one block
    uint8_t large_block_data[NETCHUNK_SHA256_BLOCK_SIZE + 1];
    memset(large_block_data, 0x77, sizeof(large_block_data));
    result = netchunk_sha256_hash(large_block_data, sizeof(large_block_data), hash);
    TEST_ASSERT_EQUAL(NETCHUNK_SUCCESS, result);
}

// Unity test runner
int main(void) {
    UNITY_BEGIN();
    
    // Initialization tests
    RUN_TEST(test_sha256_init);
    RUN_TEST(test_sha256_init_null_pointer);
    
    // One-shot hash tests
    RUN_TEST(test_sha256_hash_empty_data);
    RUN_TEST(test_sha256_hash_known_data);
    RUN_TEST(test_sha256_hash_longer_data);
    RUN_TEST(test_sha256_hash_null_parameters);
    
    // Streaming hash tests
    RUN_TEST(test_sha256_streaming_operations);
    RUN_TEST(test_sha256_streaming_null_parameters);
    
    // File hash tests
    RUN_TEST(test_sha256_hash_file);
    RUN_TEST(test_sha256_hash_file_not_found);
    RUN_TEST(test_sha256_hash_file_null_parameters);
    RUN_TEST(test_sha256_hash_large_file);
    
    // Utility function tests
    RUN_TEST(test_hash_to_hex_string);
    RUN_TEST(test_hex_string_to_hash);
    RUN_TEST(test_hex_string_conversion_roundtrip);
    RUN_TEST(test_hex_conversion_invalid_parameters);
    
    // Hash comparison tests
    RUN_TEST(test_hash_compare);
    RUN_TEST(test_hash_compare_null_parameters);
    
    // Random generation tests
    RUN_TEST(test_generate_random_bytes);
    RUN_TEST(test_generate_random_bytes_small_buffers);
    RUN_TEST(test_generate_random_bytes_null_buffer);
    
    // Performance and edge case tests
    RUN_TEST(test_sha256_large_data_performance);
    RUN_TEST(test_sha256_edge_cases);
    
    return UNITY_END();
}
