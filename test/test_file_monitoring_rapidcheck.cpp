/**
 * @file test_file_monitoring_rapidcheck.cpp
 * @brief Property-based tests for cross-platform file monitoring with nanosecond precision
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rapidcheck.h>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

// Test the file monitoring functions by extracting the key logic from overlay.c
namespace file_monitor_test {

struct FileTimeInfo {
    time_t sec;
    long nsec;
    bool valid;
};

// Extract the cross-platform file time reading logic
static FileTimeInfo get_file_time(const char* path) {
    FileTimeInfo info = {0, 0, false};
    
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fileInfo)) {
        return info;
    }
    
    // Check if it's a regular file (not a directory)
    if (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        return info;
    }
    
    // Windows FILETIME is in 100-nanosecond intervals since 1601
    ULARGE_INTEGER uli;
    uli.LowPart = fileInfo.ftLastWriteTime.dwLowDateTime;
    uli.HighPart = fileInfo.ftLastWriteTime.dwHighDateTime;
    
    // Convert from Windows epoch to Unix epoch
    const ULONGLONG WINDOWS_TICK = 10000000; // 100ns intervals per second
    const ULONGLONG SEC_TO_UNIX_EPOCH = 11644473600ULL;
    
    info.sec = (time_t)(uli.QuadPart / WINDOWS_TICK - SEC_TO_UNIX_EPOCH);
    info.nsec = (long)((uli.QuadPart % WINDOWS_TICK) * 100); // Convert 100ns to ns
    info.valid = true;
    
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return info;
    }
    
    // Check if it's a regular file
    if (!S_ISREG(st.st_mode)) {
        return info;
    }
    
    // Check if file is readable
    if (access(path, R_OK) != 0) {
        return info;
    }
    
#if defined(__APPLE__)
    info.sec = st.st_mtimespec.tv_sec;
    info.nsec = st.st_mtimespec.tv_nsec;
#elif defined(__linux__)
    // Linux uses st_mtim (not st_mtimespec)
    info.sec = st.st_mtim.tv_sec;
    info.nsec = st.st_mtim.tv_nsec;
#else
    // Fall back to second precision on other systems
    info.sec = st.st_mtime;
    info.nsec = 0;
#endif
    info.valid = true;
#endif
    
    return info;
}

static bool has_nanosecond_precision() {
#ifdef _WIN32
    return true; // Windows has 100ns precision, converted to nanoseconds
#elif defined(__APPLE__) || defined(__linux__)
    return true; // Both have nanosecond precision
#else
    return false; // Other systems fall back to second precision
#endif
}

static bool file_changed(const FileTimeInfo& old_time, const FileTimeInfo& new_time) {
    if (!old_time.valid || !new_time.valid) {
        return true; // Treat invalid times as changed
    }
    
    return (old_time.sec != new_time.sec) || (old_time.nsec != new_time.nsec);
}

// Helper to create a test file with specific content
static bool create_test_file(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << content;
    file.close();
    return true;
}

// Helper to modify a test file
static bool modify_test_file(const std::string& path, const std::string& new_content) {
    return create_test_file(path, new_content);
}

// Helper to remove test file
static void remove_test_file(const std::string& path) {
    unlink(path.c_str());
}

} // namespace file_monitor_test

namespace {

// Helper to generate test file names
auto genTestFileName() {
    return rc::gen::apply([](int id) {
        return std::string("/tmp/rapidcheck_test_") + std::to_string(id) + ".pam";
    }, rc::gen::inRange(1000, 9999));
}

// Helper to generate file content
auto genFileContent() {
    return rc::gen::apply([](int size, int seed) {
        std::string content = "P7\n# Test PAM file\nWIDTH 10\nHEIGHT 10\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
        content.reserve(content.size() + size);
        for (int i = 0; i < size; i++) {
            content += static_cast<char>((seed + i) % 256);
        }
        return content;
    }, rc::gen::inRange(100, 1000), rc::gen::arbitrary<int>());
}

} // namespace

void test_file_monitoring_properties() {
    
    // Test 1: File time reading consistency
    rc::check("get_file_time: consistent results for same file", []() {
        const std::string filename = *genTestFileName();
        const std::string content = *genFileContent();
        
        // Create test file
        if (!file_monitor_test::create_test_file(filename, content)) {
            return; // Skip test if file creation fails
        }
        
        // Read time twice
        auto time1 = file_monitor_test::get_file_time(filename.c_str());
        auto time2 = file_monitor_test::get_file_time(filename.c_str());
        
        // Should be identical if file hasn't changed
        RC_ASSERT(time1.valid == time2.valid);
        if (time1.valid && time2.valid) {
            RC_ASSERT(time1.sec == time2.sec);
            RC_ASSERT(time1.nsec == time2.nsec);
        }
        
        file_monitor_test::remove_test_file(filename);
    });
    
    // Test 2: File modification detection
    rc::check("file_changed: detects file modifications", []() {
        const std::string filename = *genTestFileName();
        const std::string content1 = *genFileContent();
        const std::string content2 = *genFileContent();
        
        // Create initial file
        if (!file_monitor_test::create_test_file(filename, content1)) {
            return; // Skip test if file creation fails
        }
        
        auto initial_time = file_monitor_test::get_file_time(filename.c_str());
        
        // Small delay to ensure different modification time
        usleep(1000); // 1ms delay
        
        // Modify file
        if (!file_monitor_test::modify_test_file(filename, content2)) {
            file_monitor_test::remove_test_file(filename);
            return; // Skip test if modification fails
        }
        
        auto modified_time = file_monitor_test::get_file_time(filename.c_str());
        
        // Should detect change
        RC_ASSERT(initial_time.valid);
        RC_ASSERT(modified_time.valid);
        RC_ASSERT(file_monitor_test::file_changed(initial_time, modified_time));
        
        file_monitor_test::remove_test_file(filename);
    });
    
    // Test 3: Nanosecond precision validation
    rc::check("nanosecond precision: nsec field within valid range", []() {
        const std::string filename = *genTestFileName();
        const std::string content = *genFileContent();
        
        if (!file_monitor_test::create_test_file(filename, content)) {
            return;
        }
        
        auto time_info = file_monitor_test::get_file_time(filename.c_str());
        
        if (time_info.valid) {
            // Nanoseconds should be in range [0, 999999999]
            RC_ASSERT(time_info.nsec >= 0);
            RC_ASSERT(time_info.nsec < 1000000000L); // < 1 second in nanoseconds
            
            // Seconds should be reasonable (after Unix epoch)
            RC_ASSERT(time_info.sec > 0);
        }
        
        file_monitor_test::remove_test_file(filename);
    });
    
    // Test 4: Platform precision capabilities
    rc::check("platform precision: nanosecond support detection", []() {
        bool has_ns_precision = file_monitor_test::has_nanosecond_precision();
        
        const std::string filename = *genTestFileName();
        const std::string content = *genFileContent();
        
        if (!file_monitor_test::create_test_file(filename, content)) {
            return;
        }
        
        auto time_info = file_monitor_test::get_file_time(filename.c_str());
        
        if (time_info.valid) {
            if (has_ns_precision) {
                // On platforms with nanosecond precision, nsec field should be meaningful
                // (Though it might still be 0 depending on filesystem)
                RC_ASSERT(time_info.nsec >= 0);
            } else {
                // On platforms without nanosecond precision, nsec should be 0
                RC_ASSERT(time_info.nsec == 0);
            }
        }
        
        file_monitor_test::remove_test_file(filename);
    });
    
    // Test 5: Invalid file handling
    rc::check("invalid files: proper error handling", []() {
        // Test non-existent file
        const std::string nonexistent = *genTestFileName();
        auto time_info = file_monitor_test::get_file_time(nonexistent.c_str());
        RC_ASSERT(!time_info.valid);
        
        // Test directory (if we can create one)
        const std::string dirname = *genTestFileName() + "_dir";
        if (mkdir(dirname.c_str(), 0755) == 0) {
            auto dir_time = file_monitor_test::get_file_time(dirname.c_str());
            RC_ASSERT(!dir_time.valid); // Should reject directories
            rmdir(dirname.c_str());
        }
    });
    
    // Test 6: Time comparison edge cases
    rc::check("time comparison: handles edge cases correctly", []() {
        file_monitor_test::FileTimeInfo time1 = {1000, 500000000, true};  // 1000.5 seconds
        file_monitor_test::FileTimeInfo time2 = {1000, 500000001, true};  // 1000.500000001 seconds
        file_monitor_test::FileTimeInfo time3 = {1001, 0, true};          // 1001.0 seconds
        file_monitor_test::FileTimeInfo invalid = {0, 0, false};
        
        // Same time should not be considered changed
        RC_ASSERT(!file_monitor_test::file_changed(time1, time1));
        
        // Different nanoseconds should be detected as changed
        RC_ASSERT(file_monitor_test::file_changed(time1, time2));
        
        // Different seconds should be detected as changed
        RC_ASSERT(file_monitor_test::file_changed(time1, time3));
        
        // Invalid times should be considered changed
        RC_ASSERT(file_monitor_test::file_changed(invalid, time1));
        RC_ASSERT(file_monitor_test::file_changed(time1, invalid));
    });
    
    // Test 7: Rapid modification detection
    rc::check("rapid modifications: detects quick successive changes", []() {
        const std::string filename = *genTestFileName();
        const std::string content1 = *genFileContent();
        const std::string content2 = *genFileContent();
        const std::string content3 = *genFileContent();
        
        if (!file_monitor_test::create_test_file(filename, content1)) {
            return;
        }
        
        auto time1 = file_monitor_test::get_file_time(filename.c_str());
        
        // Rapid modifications with minimal delay
        usleep(100); // 0.1ms
        if (!file_monitor_test::modify_test_file(filename, content2)) {
            file_monitor_test::remove_test_file(filename);
            return;
        }
        
        auto time2 = file_monitor_test::get_file_time(filename.c_str());
        
        usleep(100); // 0.1ms
        if (!file_monitor_test::modify_test_file(filename, content3)) {
            file_monitor_test::remove_test_file(filename);
            return;
        }
        
        auto time3 = file_monitor_test::get_file_time(filename.c_str());
        
        // All times should be valid
        RC_ASSERT(time1.valid && time2.valid && time3.valid);
        
        // Should detect all changes (if filesystem has sufficient precision)
        bool change1to2 = file_monitor_test::file_changed(time1, time2);
        bool change2to3 = file_monitor_test::file_changed(time2, time3);
        
        // At minimum, one of the changes should be detected
        // (Some filesystems may not have enough precision for both)
        RC_ASSERT(change1to2 || change2to3);
        
        file_monitor_test::remove_test_file(filename);
    });
    
    // Test 8: Cross-platform time precision consistency
    rc::check("cross-platform: time precision behavior is consistent", []() {
        const std::string filename = *genTestFileName();
        const std::string content = *genFileContent();
        
        if (!file_monitor_test::create_test_file(filename, content)) {
            return;
        }
        
        auto time_info = file_monitor_test::get_file_time(filename.c_str());
        
        if (time_info.valid) {
            bool has_ns = file_monitor_test::has_nanosecond_precision();
            
            // Validate platform-specific behavior
#ifdef _WIN32
            // Windows should have nanosecond precision (converted from 100ns)
            RC_ASSERT(has_ns);
            // Windows precision should be multiples of 100 (converted from 100ns intervals)
            RC_ASSERT(time_info.nsec % 100 == 0);
#elif defined(__APPLE__) || defined(__linux__)
            // macOS and Linux should have nanosecond precision
            RC_ASSERT(has_ns);
            // No specific alignment requirements for Unix systems
#else
            // Other systems fall back to second precision
            RC_ASSERT(!has_ns);
            RC_ASSERT(time_info.nsec == 0);
#endif
        }
        
        file_monitor_test::remove_test_file(filename);
    });
    
    // Test 9: File size independence
    rc::check("file monitoring: modification detection independent of file size", []() {
        const std::string filename = *genTestFileName();
        const std::string small_content = "small";
        const std::string large_content = *genFileContent();
        
        // Start with small file
        if (!file_monitor_test::create_test_file(filename, small_content)) {
            return;
        }
        
        auto time_small = file_monitor_test::get_file_time(filename.c_str());
        
        usleep(1000); // 1ms delay
        
        // Change to large file
        if (!file_monitor_test::modify_test_file(filename, large_content)) {
            file_monitor_test::remove_test_file(filename);
            return;
        }
        
        auto time_large = file_monitor_test::get_file_time(filename.c_str());
        
        usleep(1000); // 1ms delay
        
        // Change back to small file
        if (!file_monitor_test::modify_test_file(filename, small_content)) {
            file_monitor_test::remove_test_file(filename);
            return;
        }
        
        auto time_small_again = file_monitor_test::get_file_time(filename.c_str());
        
        // All changes should be detected regardless of file size
        RC_ASSERT(time_small.valid && time_large.valid && time_small_again.valid);
        RC_ASSERT(file_monitor_test::file_changed(time_small, time_large));
        RC_ASSERT(file_monitor_test::file_changed(time_large, time_small_again));
        
        file_monitor_test::remove_test_file(filename);
    });
}