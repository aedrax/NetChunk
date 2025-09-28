# NetChunk Test Suite

This directory contains a comprehensive test suite for the NetChunk distributed file storage system, including unit tests, integration tests with real FTP servers, and performance stress tests.

## Test Infrastructure Overview

### Test Framework
- **Unity**: Lightweight C testing framework
- **Custom Test Utilities**: Helper functions for file operations, performance metrics, and test setup
- **Mock Infrastructure**: Mock FTP servers for isolated unit testing
- **Real FTP Servers**: Docker-based FTP servers for integration testing

### Test Categories

1. **Unit Tests** (`unit/`): Test individual modules in isolation
2. **Integration Tests** (`integration/`): End-to-end testing with real FTP servers
3. **Stress Tests** (`stress/`): Performance and scalability testing
4. **Mock Tests**: Isolated testing with mock FTP infrastructure

## Quick Start

### Prerequisites
- CMake 3.10+
- C compiler (GCC/Clang)
- Docker and Docker Compose (for integration tests)
- libcurl, cjson, OpenSSL development libraries

### Running Unit Tests
```bash
# Build and run config module unit tests
cd build
make test_config
./tests/test_config
```

### Running Integration Tests with Real FTP Servers

1. **Start FTP test servers:**
```bash
# Make the management script executable
chmod +x tests/docker/manage-ftp-servers.sh

# Start all 7 FTP servers
tests/docker/manage-ftp-servers.sh start

# Check server status
tests/docker/manage-ftp-servers.sh status

# Test connections
tests/docker/manage-ftp-servers.sh test
```

2. **Build and run integration tests:**
```bash
cd build
make test_real_ftp_integration
./tests/test_real_ftp_integration
```

3. **Stop FTP servers when done:**
```bash
tests/docker/manage-ftp-servers.sh stop
```

## FTP Test Servers

The Docker setup provides 7 FTP servers for comprehensive integration testing:

| Server | Host      | Port | Username   | Password  | Passive Ports |
|--------|-----------|------|------------|-----------|---------------|
| 1      | localhost | 2121 | netchunk1  | test123   | 21100-21110   |
| 2      | localhost | 2122 | netchunk2  | test456   | 21200-21210   |
| 3      | localhost | 2123 | netchunk3  | test789   | 21300-21310   |
| 4      | localhost | 2124 | netchunk4  | secure456 | 21400-21410   |
| 5      | localhost | 2125 | netchunk5  | secure789 | 21500-21510   |
| 6      | localhost | 2126 | netchunk6  | admin123  | 21600-21610   |
| 7      | localhost | 2127 | netchunk7  | admin456  | 21700-21710   |

### FTP Server Management

The `tests/docker/manage-ftp-servers.sh` script provides comprehensive server management:

```bash
# Start all servers
./manage-ftp-servers.sh start

# Stop all servers
./manage-ftp-servers.sh stop

# Restart all servers
./manage-ftp-servers.sh restart

# Show server status
./manage-ftp-servers.sh status

# Show connection information
./manage-ftp-servers.sh info

# Test all connections
./manage-ftp-servers.sh test

# View logs (all servers or specific server 1-7)
./manage-ftp-servers.sh logs
./manage-ftp-servers.sh logs 1

# Clean up containers and volumes
./manage-ftp-servers.sh cleanup

# Show help
./manage-ftp-servers.sh help
```

## Test Structure

### Unit Tests (`unit/`)
- `test_config.c` - Configuration module tests
- `test_chunker.c` - File chunking and reconstruction tests
- `test_crypto.c` - Cryptographic operations tests
- `test_ftp_client.c` - FTP client tests with mocks
- `test_logger.c` - Logging system tests
- `test_manifest.c` - JSON manifest handling tests
- `test_repair.c` - Repair engine tests

### Integration Tests (`integration/`)
- `test_real_ftp_integration.c` - Complete NetChunk API testing with real FTP servers
- `test_upload_download.c` - Upload/download workflow tests
- `test_repair_integration.c` - End-to-end repair scenarios
- `test_end_to_end.c` - Full system integration tests

### Stress Tests (`stress/`)
- `test_large_files.c` - Large file performance testing
- `test_many_servers.c` - High server count scenarios

### Mock Infrastructure (`mocks/`)
- `mock_ftp.h/c` - Mock FTP server implementation
- `mock_filesystem.h/c` - Mock file system operations

### Test Utilities (`utils/`)
- `test_utils.h/c` - Common test helper functions
- File comparison and integrity verification
- Performance metrics and memory tracking
- Random data generation and test patterns
- Custom test assertions

## CMake Integration

The test suite is fully integrated with CMake and CTest:

```bash
# Build specific test
make test_config

# Run specific test via CTest
ctest -R test_config -V

# Run all unit tests
make run_unit_tests

# Run all integration tests (requires FTP servers)
make run_integration_tests

# Run all stress tests
make run_stress_tests

# Run all tests
make run_all_tests
ctest --verbose
```

## Configuration

### Integration Test Configuration
Integration tests use `integration/ftp-servers-config.conf` which configures:
- All 7 FTP test servers
- 3x replication factor for reliability testing
- 4MB chunk size for performance testing
- Debug logging for detailed test output

### Docker Configuration
FTP servers are configured via `docker/docker-compose.yml`:
- Isolated network for test servers
- Persistent volumes for data storage
- Unique port ranges for each server
- Proper passive mode configuration

## Test Execution Examples

### Basic Unit Test
```bash
cd build
make test_config
./tests/test_config
```

Expected output:
```
20 Tests 0 Failures 0 Ignored
OK
```

### Integration Test with Real FTP Servers
```bash
# Start FTP servers
tests/docker/manage-ftp-servers.sh start

# Run integration test
cd build
make test_real_ftp_integration
./tests/test_real_ftp_integration
```

Expected output:
```
=== NetChunk Real FTP Server Integration Tests ===

NetChunk version: 1.0.0 (1.0.0)
Health check result: 7 healthy out of 7 total servers
Uploading test file...
Upload completed with result: Success
...

=== Integration Test Performance Summary ===
Integration Tests Performance Summary:
  Duration: 1250.45 ms
  Bytes Processed: 20.0 MB
  Operations: 8
  Peak Memory: 2.4 MB
  Throughput: 16.2 MB/s

10 Tests 0 Failures 0 Ignored
OK
```

## Troubleshooting

### FTP Server Issues
```bash
# Check Docker status
docker ps

# Check server logs
tests/docker/manage-ftp-servers.sh logs 1

# Restart servers
tests/docker/manage-ftp-servers.sh restart

# Test connections
tests/docker/manage-ftp-servers.sh test
```

### Build Issues
```bash
# Clean build
rm -rf build
mkdir build
cd build
cmake ..
make -j$(nproc)
```

### Test Failures
- Ensure all dependencies are installed
- Check that FTP servers are running for integration tests
- Verify network connectivity and firewall settings
- Check test logs for specific error messages

## Development Guidelines

### Adding New Unit Tests
1. Create test file in `unit/` directory
2. Follow the pattern from `test_config.c`
3. Include Unity framework and test utilities
4. Implement setUp/tearDown functions
5. Add test to CMakeLists.txt

### Adding Integration Tests
1. Create test file in `integration/` directory
2. Use real FTP server configuration
3. Include proper error handling for missing servers
4. Add performance metrics collection
5. Update CMakeLists.txt with longer timeout

### Mock Infrastructure
Use mocks for unit tests to ensure:
- Isolated testing without external dependencies
- Predictable test results
- Fast test execution
- Ability to test error conditions

### Performance Testing
Include performance metrics in tests:
- Measure operation duration
- Track memory usage
- Calculate throughput
- Compare against baselines

## Continuous Integration

The test suite is designed for CI/CD integration:
- All tests can run in headless environments
- Docker-based FTP servers work in CI systems
- CMake/CTest integration for automated reporting
- Configurable timeouts for different test types
- Memory leak detection capabilities

## Architecture Testing

The test suite validates NetChunk's architecture:
- **Modular Design**: Each module tested independently
- **Error Handling**: All error paths tested
- **Configuration**: All config options validated
- **Distribution**: Multi-server scenarios tested
- **Replication**: Data integrity across servers tested
- **Repair**: Automatic recovery mechanisms tested
- **Performance**: Throughput and latency measured

This comprehensive test suite ensures NetChunk meets its reliability, performance, and scalability requirements before production deployment.
