#pragma once

#include "memory/memory.hpp"
#include "macho/macho.hpp"

#include <vector>
#include <string>
#include <string_view>
#include <utility>
#include <print>
#include <cctype>
#include <cstdlib>
#include <algorithm>

namespace scanner {

struct ScanResult {
    vm_address_t address = 0;
    vm_address_t offset = 0;  // Offset from region start
};

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;  // true = match, false = wildcard
    
    bool empty() const { return bytes.empty(); }
    size_t size() const { return bytes.size(); }
};

// Parse pattern from string (Example: "48 8B ?? ?? 90")
inline Pattern parse_pattern(std::string_view pattern_str) {
    Pattern pattern;

    for (size_t i = 0; i < pattern_str.size();) {
        if (std::isspace((unsigned char)pattern_str[i])) {
            ++i;
            continue;
        }

        if (pattern_str[i] == '?') {
            pattern.bytes.push_back(0);
            pattern.mask.push_back(false);
            ++i;
            if (i < pattern_str.size() && pattern_str[i] == '?')
                ++i;
            continue;
        }

        if (i + 1 >= pattern_str.size() ||
            !std::isxdigit(pattern_str[i]) ||
            !std::isxdigit(pattern_str[i + 1])) {
            break;
        }

        uint8_t byte = static_cast<uint8_t>(
            std::strtoul(std::string(pattern_str.substr(i, 2)).c_str(), nullptr, 16)
        );

        pattern.bytes.push_back(byte);
        pattern.mask.push_back(true);
        i += 2;
    }

    return pattern;
}

// Create pattern from raw bytes (no wildcards)
inline Pattern from_bytes(const std::vector<uint8_t>& bytes) {
    Pattern pattern;
    pattern.bytes = bytes;
    pattern.mask.resize(bytes.size(), true);
    return pattern;
}

// Create pattern from string literal (for string searching)
inline Pattern from_string(std::string_view str, bool include_null = true) {
    Pattern pattern;
    pattern.bytes.assign(str.begin(), str.end());
    if (include_null) {
        pattern.bytes.push_back(0);
    }
    pattern.mask.resize(pattern.bytes.size(), true);
    return pattern;
}

inline std::vector<ScanResult> scan_region(
    task_t task,
    vm_address_t start,
    vm_size_t size,
    const Pattern& pattern)
{
    std::vector<ScanResult> results;

    if (pattern.empty() || size < pattern.size())
        return results;

    constexpr size_t CHUNK = 0x10000; // 64 KB
    std::vector<uint8_t> buffer(CHUNK + pattern.size());

    bool use_mask = !pattern.mask.empty();

    for (vm_size_t offset = 0; offset < size; offset += CHUNK) {
        vm_size_t to_read = std::min<vm_size_t>(CHUNK + pattern.size(), size - offset);

        if (!memory::read_bytes(task, start + offset, buffer.data(), to_read))
            continue;

        for (size_t i = 0; i + pattern.size() <= to_read; ++i) {
            bool found = true;

            for (size_t j = 0; j < pattern.size(); ++j) {
                if (use_mask && !pattern.mask[j])
                    continue;

                if (buffer[i + j] != pattern.bytes[j]) {
                    found = false;
                    break;
                }
            }

            if (found) {
                results.push_back({
                    start + offset + i,
                    offset + i
                });
            }
        }
    }

    return results;
}

inline std::vector<ScanResult> scan_region(
    task_t task,
    vm_address_t start,
    vm_size_t size,
    const std::vector<uint8_t>& bytes)
{
    return scan_region(task, start, size, from_bytes(bytes));
}

inline std::vector<ScanResult> scan_region(
    task_t task,
    vm_address_t start,
    vm_size_t size,
    std::string_view pattern_str)
{
    return scan_region(task, start, size, parse_pattern(pattern_str));
}

inline std::vector<ScanResult> scan_segment(
    task_t task,
    vm_address_t image_base,
    std::string_view segment_name,
    const Pattern& pattern)
{
    auto seg = macho::get_segment(task, image_base, segment_name);
    if (!seg) {
        std::println("Failed to find segment: {}", segment_name);
        return {};
    }

    return scan_region(task, seg->address, seg->size, pattern);
}

inline std::vector<ScanResult> scan_text(task_t task, vm_address_t image_base, const Pattern& pattern) {
    return scan_segment(task, image_base, "__TEXT", pattern);
}

inline std::vector<ScanResult> scan_data(task_t task, vm_address_t image_base, const Pattern& pattern) {
    auto results = scan_segment(task, image_base, "__DATA", pattern);
    if (results.empty()) {
        results = scan_segment(task, image_base, "__DATA_CONST", pattern);
    }
    return results;
}

inline std::vector<ScanResult> scan_all_data(task_t task, vm_address_t image_base, const Pattern& pattern) {
    std::vector<ScanResult> all_results;

    auto segments = macho::get_all_segments(task, image_base);
    for (const auto& seg : segments) {
        if (seg.name.starts_with("__DATA")) {
            auto results = scan_region(task, seg.address, seg.size, pattern);
            all_results.insert(all_results.end(), results.begin(), results.end());
        }
    }

    return all_results;
}

inline std::vector<ScanResult> scan_section(
    task_t task,
    vm_address_t image_base,
    std::string_view segment_name,
    std::string_view section_name,
    const Pattern& pattern)
{
    auto sect = macho::get_section(task, image_base, segment_name, section_name);
    if (!sect) {
        std::println("Failed to find section: {},{}", segment_name, section_name);
        return {};
    }

    return scan_region(task, sect->address, sect->size, pattern);
}

// Scan executable code only (__TEXT,__text)
inline std::vector<ScanResult> scan_code(task_t task, vm_address_t image_base, const Pattern& pattern) {
    return scan_section(task, image_base, "__TEXT", "__text", pattern);
}

inline std::vector<ScanResult> scan_code(task_t task, vm_address_t image_base, std::string_view pattern_str) {
    return scan_code(task, image_base, parse_pattern(pattern_str));
}

// Scan C string literals (__TEXT,__cstring)
inline std::vector<ScanResult> scan_cstrings(task_t task, vm_address_t image_base, const Pattern& pattern) {
    return scan_section(task, image_base, "__TEXT", "__cstring", pattern);
}

// Search for a string literal
inline std::vector<ScanResult> find_string(task_t task, vm_address_t image_base, std::string_view str) {
    return scan_cstrings(task, image_base, from_string(str));
}

// Search for string in __DATA segment (for mutable strings)
inline std::vector<ScanResult> find_string_in_data(task_t task, vm_address_t image_base, std::string_view str) {
    return scan_section(task, image_base, "__DATA", "__data", from_string(str));
}

// Scan initialized data (__DATA,__data)
inline std::vector<ScanResult> scan_data_section(task_t task, vm_address_t image_base, const Pattern& pattern) {
    return scan_section(task, image_base, "__DATA", "__data", pattern);
}

// Scan BSS (__DATA,__bss)
inline std::vector<ScanResult> scan_bss(task_t task, vm_address_t image_base, const Pattern& pattern) {
    return scan_section(task, image_base, "__DATA", "__bss", pattern);
}

// Scan const data
inline std::vector<ScanResult> scan_const(task_t task, vm_address_t image_base, const Pattern& pattern) {
    auto result = scan_section(task, image_base, "__DATA_CONST", "__const", pattern);
    if (result.empty()) {
        result = scan_section(task, image_base, "__DATA", "__const", pattern);
    }
    return result;
}

// Scan GOT
inline std::vector<ScanResult> scan_got(task_t task, vm_address_t image_base, const Pattern& pattern) {
    auto result = scan_section(task, image_base, "__DATA_CONST", "__got", pattern);
    if (result.empty()) {
        result = scan_section(task, image_base, "__DATA", "__got", pattern);
    }
    return result;
}

// Scan stubs
inline std::vector<ScanResult> scan_stubs(task_t task, vm_address_t image_base, const Pattern& pattern) {
    return scan_section(task, image_base, "__TEXT", "__stubs", pattern);
}

// Scan ObjC method names
inline std::vector<ScanResult> find_objc_method(task_t task, vm_address_t image_base, std::string_view method_name) {
    return scan_section(task, image_base, "__TEXT", "__objc_methname", from_string(method_name));
}

// Scan ObjC class names
inline std::vector<ScanResult> find_objc_class(task_t task, vm_address_t image_base, std::string_view class_name) {
    return scan_section(task, image_base, "__TEXT", "__objc_classname", from_string(class_name));
}

inline std::optional<vm_address_t> find_first(
    task_t task,
    vm_address_t image_base,
    std::string_view segment_name,
    const Pattern& pattern)
{
    auto results = scan_segment(task, image_base, segment_name, pattern);
    if (results.empty()) return std::nullopt;
    return results[0].address;
}

inline std::optional<vm_address_t> find_first_in_code(task_t task, vm_address_t image_base, const Pattern& pattern) {
    auto results = scan_code(task, image_base, pattern);
    if (results.empty()) return std::nullopt;
    return results[0].address;
}

inline std::optional<vm_address_t> find_first_string(task_t task, vm_address_t image_base, std::string_view str) {
    auto results = find_string(task, image_base, str);
    if (results.empty()) return std::nullopt;
    return results[0].address;
}

} // namespace scanner
