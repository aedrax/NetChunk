cmake_minimum_required(VERSION 3.10)
project(netchunk VERSION 1.0.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# Find libcurl
find_package(CURL REQUIRED)
if(CURL_FOUND)
    include_directories(${CURL_INCLUDE_DIR})
endif()

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Find cjson
pkg_check_modules(CJSON REQUIRED libcjson)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CJSON_INCLUDE_DIRS})

# Create list of source files
set(NETCHUNK_SOURCES
    src/netchunk.c
    src/config.c
    src/ftp_client.c
    src/chunker.c
    src/manifest.c
    src/crypto.c
    src/repair.c
    src/logger.c
)

# Create shared library
add_library(netchunk SHARED ${NETCHUNK_SOURCES})

# Link libraries to shared library
target_link_libraries(netchunk
    ${CURL_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${CJSON_LIBRARIES}
    Threads::Threads
    m  # math library
)

# Set library compile flags
target_compile_options(netchunk PRIVATE ${CJSON_CFLAGS_OTHER})

# Create main executable
add_executable(netchunk-cli src/main.c)
target_link_libraries(netchunk-cli netchunk)

# Installation
install(TARGETS netchunk netchunk-cli
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)

# Enable testing
enable_testing()

# Add test executables
add_subdirectory(tests)

# Create example config during build
configure_file(
    "${CMAKE_SOURCE_DIR}/config/netchunk.conf.example.in"
    "${CMAKE_BINARY_DIR}/config/netchunk.conf.example"
    @ONLY
)

# Install example config
install(FILES "${CMAKE_BINARY_DIR}/config/netchunk.conf.example"
    DESTINATION /etc/netchunk/
)

# Package configuration
set(CPACK_PACKAGE_NAME "netchunk")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Distributed file storage client over multiple FTP servers")
set(CPACK_PACKAGE_CONTACT "paulsorensen5@gmail.com")
include(CPack)
