# NetChunk Test Suite with Unity Framework Integration
cmake_minimum_required(VERSION 3.10)

# Enable testing
enable_testing()

# Unity framework integration
set(UNITY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/unity")
set(UNITY_SOURCES 
    ${UNITY_ROOT}/src/unity.c
)
set(UNITY_HEADERS
    ${UNITY_ROOT}/src/unity.h
    ${UNITY_ROOT}/src/unity_internals.h
)

# Create Unity library
add_library(unity STATIC ${UNITY_SOURCES})
target_include_directories(unity PUBLIC ${UNITY_ROOT}/src)
target_compile_definitions(unity PUBLIC 
    UNITY_INCLUDE_DOUBLE
    UNITY_INCLUDE_FLOAT
)

# Test utilities and common functions
set(TEST_UTILS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/utils")
file(MAKE_DIRECTORY ${TEST_UTILS_DIR})

# Mock infrastructure
set(MOCK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mocks")
file(MAKE_DIRECTORY ${MOCK_DIR})

# Create test_utils library
add_library(test_utils STATIC utils/test_utils.c)
target_include_directories(test_utils PUBLIC 
    utils
    ${UNITY_ROOT}/src
)
target_link_libraries(test_utils PUBLIC unity)

# Function to create test executable with common setup
function(add_netchunk_test test_name test_source)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} 
        unity
        test_utils
        netchunk
        ${CMAKE_THREAD_LIBS_INIT}
    )
    target_include_directories(${test_name} PRIVATE 
        ${CMAKE_SOURCE_DIR}/include
        ${TEST_UTILS_DIR}
        ${MOCK_DIR}
        ${UNITY_ROOT}/src
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
    # Set test timeout to 30 seconds
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
endfunction()

# Unit Tests - Configuration
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_config.c")
    add_netchunk_test(test_config unit/test_config.c)
endif()

# Unit Tests - Chunker
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_chunker.c")
    add_netchunk_test(test_chunker unit/test_chunker.c)
endif()

# Unit Tests - Crypto
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_crypto.c")
    add_netchunk_test(test_crypto unit/test_crypto.c)
endif()

# Unit Tests - FTP Client
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ftp_client.c")
    add_netchunk_test(test_ftp_client unit/test_ftp_client.c)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mocks/mock_ftp.c")
        target_sources(test_ftp_client PRIVATE mocks/mock_ftp.c)
    endif()
endif()

# Unit Tests - Logger
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_logger.c")
    add_netchunk_test(test_logger unit/test_logger.c)
endif()

# Unit Tests - Manifest
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_manifest.c")
    add_netchunk_test(test_manifest unit/test_manifest.c)
endif()

# Unit Tests - Repair
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_repair.c")
    add_netchunk_test(test_repair unit/test_repair.c)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mocks/mock_ftp.c")
        target_sources(test_repair PRIVATE mocks/mock_ftp.c)
    endif()
endif()

# Integration Tests - Upload/Download
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/test_upload_download.c")
    add_netchunk_test(test_upload_download integration/test_upload_download.c)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mocks/mock_ftp.c")
        target_sources(test_upload_download PRIVATE mocks/mock_ftp.c)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utils/test_utils.c")
        target_sources(test_upload_download PRIVATE utils/test_utils.c)
    endif()
endif()

# Integration Tests - Repair Integration
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/test_repair_integration.c")
    add_netchunk_test(test_repair_integration integration/test_repair_integration.c)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mocks/mock_ftp.c")
        target_sources(test_repair_integration PRIVATE mocks/mock_ftp.c)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utils/test_utils.c")
        target_sources(test_repair_integration PRIVATE utils/test_utils.c)
    endif()
endif()

# Integration Tests - End-to-End
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/test_end_to_end.c")
    add_netchunk_test(test_end_to_end integration/test_end_to_end.c)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mocks/mock_ftp.c")
        target_sources(test_end_to_end PRIVATE mocks/mock_ftp.c)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utils/test_utils.c")
        target_sources(test_end_to_end PRIVATE utils/test_utils.c)
    endif()
endif()

# Integration Tests - Real FTP Servers
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/test_real_ftp_integration.c")
    add_netchunk_test(test_real_ftp_integration integration/test_real_ftp_integration.c)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utils/test_utils.c")
        target_sources(test_real_ftp_integration PRIVATE utils/test_utils.c)
    endif()
    # Real FTP integration tests need longer timeout
    set_tests_properties(test_real_ftp_integration PROPERTIES TIMEOUT 180)
endif()

# Debug Test - FTP Connection Debugging
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/test_ftp_connection_debug.c")
    add_netchunk_test(test_ftp_connection_debug integration/test_ftp_connection_debug.c)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utils/test_utils.c")
        target_sources(test_ftp_connection_debug PRIVATE utils/test_utils.c)
    endif()
    # Debug test with timeout
    set_tests_properties(test_ftp_connection_debug PROPERTIES TIMEOUT 60)
endif()

# Stress Tests - Large Files
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/stress/test_large_files.c")
    add_netchunk_test(test_large_files stress/test_large_files.c)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utils/test_utils.c")
        target_sources(test_large_files PRIVATE utils/test_utils.c)
    endif()
    # Large file tests need longer timeout
    set_tests_properties(test_large_files PROPERTIES TIMEOUT 120)
endif()

# Stress Tests - Many Servers
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/stress/test_many_servers.c")
    add_netchunk_test(test_many_servers stress/test_many_servers.c)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mocks/mock_ftp.c")
        target_sources(test_many_servers PRIVATE mocks/mock_ftp.c)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utils/test_utils.c")
        target_sources(test_many_servers PRIVATE utils/test_utils.c)
    endif()
    # Many servers test needs longer timeout
    set_tests_properties(test_many_servers PROPERTIES TIMEOUT 90)
endif()

# Test runner targets
add_custom_target(run_unit_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -L "unit" --verbose
    DEPENDS test_config test_chunker test_crypto test_ftp_client test_logger test_manifest test_repair
    COMMENT "Running unit tests"
)

add_custom_target(run_integration_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -L "integration" --verbose
    DEPENDS test_upload_download test_repair_integration test_end_to_end
    COMMENT "Running integration tests"
)

add_custom_target(run_stress_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -L "stress" --verbose
    DEPENDS test_large_files test_many_servers
    COMMENT "Running stress tests"
)

add_custom_target(run_all_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose
    COMMENT "Running all tests"
)

# Status message
message(STATUS "Unity testing framework integrated successfully")
message(STATUS "Test directories: unit/, integration/, stress/")
message(STATUS "Available test targets: run_unit_tests, run_integration_tests, run_stress_tests, run_all_tests")
