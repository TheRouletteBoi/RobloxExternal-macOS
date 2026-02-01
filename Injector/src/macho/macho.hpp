#pragma once

#include "memory/memory.hpp"

#include <mach/mach.h>
#include <mach-o/loader.h>
#include <mach-o/dyld_images.h>
#include <mach-o/nlist.h>
#include <optional>
#include <vector>
#include <string>
#include <string_view>
#include <print>
#include <cctype>
#include <algorithm>

namespace macho {

struct ImageInfo {
    vm_address_t base = 0;
    vm_address_t slide = 0;
};

struct SegmentInfo {
    vm_address_t address = 0;
    vm_size_t size = 0;
    std::string name;
};

struct SectionInfo {
    vm_address_t address = 0;
    vm_size_t size = 0;
    std::string segment_name;
    std::string section_name;
    uint32_t flags = 0;
};

namespace detail {

inline std::string image_name_from_path(const std::string& path) {
    auto slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return path;
    return path.substr(slash + 1);
}

inline bool ends_with_icase(std::string_view s, std::string_view suffix) {
    if (suffix.size() > s.size())
        return false;

    return std::equal(
        s.end() - suffix.size(), s.end(),
        suffix.begin(), suffix.end(),
        [](char a, char b) {
            return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
        }
    );
}

inline bool read_preferred_address(task_t task, vm_address_t image_base, vm_address_t& preferred) {
    mach_header_64 hdr{};
    if (!memory::read_value(task, image_base, hdr))
        return false;

    if (hdr.magic != MH_MAGIC_64)
        return false;

    vm_address_t cursor = image_base + sizeof(hdr);

    for (uint32_t i = 0; i < hdr.ncmds; ++i) {
        load_command lc{};
        if (!memory::read_value(task, cursor, lc))
            return false;

        if (lc.cmd == LC_SEGMENT_64) {
            segment_command_64 seg{};
            if (!memory::read_value(task, cursor, seg))
                return false;

            if (seg.vmaddr != 0 && seg.vmsize != 0) {
                preferred = seg.vmaddr;
                return true;
            }
        }

        cursor += lc.cmdsize;
    }

    return false;
}

inline vm_address_t calculate_slide(task_t task, vm_address_t image_base) {
    vm_address_t preferred = 0;
    if (read_preferred_address(task, image_base, preferred)) {
        return image_base - preferred;
    }
    return 0;
}

} // namespace detail

inline ImageInfo get_image_info(task_t task, std::string_view image_name) {
    ImageInfo result{};

    task_dyld_info dyld{};
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;

    if (task_info(task, TASK_DYLD_INFO, reinterpret_cast<task_info_t>(&dyld), &count) != KERN_SUCCESS) {
        return result;
    }

    dyld_all_image_infos infos{};
    if (!memory::read_value(task, dyld.all_image_info_addr, infos)) {
        return result;
    }

    for (uint32_t i = 0; i < infos.infoArrayCount; ++i) {
        dyld_image_info img{};
        vm_address_t img_info_addr = reinterpret_cast<vm_address_t>(infos.infoArray) + i * sizeof(img);

        if (!memory::read_value(task, img_info_addr, img))
            continue;

        std::string path;
        if (!memory::read_cstring(task, reinterpret_cast<vm_address_t>(img.imageFilePath), path, 512))
            continue;

        std::string name = detail::image_name_from_path(path);

        if (!detail::ends_with_icase(name, image_name))
            continue;

        result.base = reinterpret_cast<vm_address_t>(img.imageLoadAddress);
        result.slide = detail::calculate_slide(task, result.base);
        return result;
    }

    return result;
}

inline std::vector<std::pair<std::string, ImageInfo>> get_all_images(task_t task) {
    std::vector<std::pair<std::string, ImageInfo>> images;

    task_dyld_info dyld{};
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;

    if (task_info(task, TASK_DYLD_INFO, reinterpret_cast<task_info_t>(&dyld), &count) != KERN_SUCCESS) {
        return images;
    }

    dyld_all_image_infos infos{};
    if (!memory::read_value(task, dyld.all_image_info_addr, infos)) {
        return images;
    }

    for (uint32_t i = 0; i < infos.infoArrayCount; ++i) {
        dyld_image_info img{};
        vm_address_t img_info_addr = reinterpret_cast<vm_address_t>(infos.infoArray) + i * sizeof(img);

        if (!memory::read_value(task, img_info_addr, img))
            continue;

        std::string path;
        if (!memory::read_cstring(task, reinterpret_cast<vm_address_t>(img.imageFilePath), path, 512))
            continue;

        ImageInfo info;
        info.base = reinterpret_cast<vm_address_t>(img.imageLoadAddress);
        info.slide = detail::calculate_slide(task, info.base);

        images.emplace_back(detail::image_name_from_path(path), info);
    }

    return images;
}

inline std::optional<SegmentInfo> get_segment(task_t task, vm_address_t image_base, std::string_view segment_name) {
    mach_header_64 hdr{};
    if (!memory::read_value(task, image_base, hdr))
        return std::nullopt;

    if (hdr.magic != MH_MAGIC_64)
        return std::nullopt;

    vm_address_t slide = detail::calculate_slide(task, image_base);
    vm_address_t cursor = image_base + sizeof(hdr);

    for (uint32_t i = 0; i < hdr.ncmds; ++i) {
        load_command lc{};
        if (!memory::read_value(task, cursor, lc))
            return std::nullopt;

        if (lc.cmd == LC_SEGMENT_64) {
            segment_command_64 seg{};
            if (!memory::read_value(task, cursor, seg))
                return std::nullopt;

            if (segment_name == seg.segname) {
                SegmentInfo info{};
                info.address = seg.vmaddr + slide;
                info.size = seg.vmsize;
                info.name = seg.segname;
                return info;
            }
        }

        cursor += lc.cmdsize;
    }

    return std::nullopt;
}

inline std::vector<SegmentInfo> get_all_segments(task_t task, vm_address_t image_base) {
    std::vector<SegmentInfo> segments;

    mach_header_64 hdr{};
    if (!memory::read_value(task, image_base, hdr))
        return segments;

    if (hdr.magic != MH_MAGIC_64)
        return segments;

    vm_address_t slide = detail::calculate_slide(task, image_base);
    vm_address_t cursor = image_base + sizeof(hdr);

    for (uint32_t i = 0; i < hdr.ncmds; ++i) {
        load_command lc{};
        if (!memory::read_value(task, cursor, lc))
            break;

        if (lc.cmd == LC_SEGMENT_64) {
            segment_command_64 seg{};
            if (memory::read_value(task, cursor, seg)) {
                SegmentInfo info;
                info.address = seg.vmaddr + slide;
                info.size = seg.vmsize;
                info.name = seg.segname;
                segments.push_back(info);
            }
        }

        cursor += lc.cmdsize;
    }

    return segments;
}

inline std::optional<SectionInfo> get_section(
    task_t task,
    vm_address_t image_base,
    std::string_view segment_name,
    std::string_view section_name) 
{
    mach_header_64 hdr{};
    if (!memory::read_value(task, image_base, hdr))
        return std::nullopt;

    if (hdr.magic != MH_MAGIC_64)
        return std::nullopt;

    vm_address_t slide = detail::calculate_slide(task, image_base);
    vm_address_t cursor = image_base + sizeof(hdr);

    for (uint32_t i = 0; i < hdr.ncmds; ++i) {
        load_command lc{};
        if (!memory::read_value(task, cursor, lc))
            return std::nullopt;

        if (lc.cmd == LC_SEGMENT_64) {
            segment_command_64 seg{};
            if (!memory::read_value(task, cursor, seg)) {
                cursor += lc.cmdsize;
                continue;
            }

            if (segment_name == seg.segname) {
                vm_address_t section_cursor = cursor + sizeof(segment_command_64);

                for (uint32_t j = 0; j < seg.nsects; ++j) {
                    section_64 sect{};
                    if (!memory::read_value(task, section_cursor, sect)) {
                        section_cursor += sizeof(section_64);
                        continue;
                    }

                    if (section_name == sect.sectname) {
                        SectionInfo info;
                        info.address = sect.addr + slide;
                        info.size = sect.size;
                        info.segment_name = sect.segname;
                        info.section_name = sect.sectname;
                        info.flags = sect.flags;
                        return info;
                    }

                    section_cursor += sizeof(section_64);
                }
            }
        }

        cursor += lc.cmdsize;
    }

    return std::nullopt;
}

inline std::vector<SectionInfo> get_all_sections(task_t task, vm_address_t image_base) {
    std::vector<SectionInfo> sections;

    mach_header_64 hdr{};
    if (!memory::read_value(task, image_base, hdr))
        return sections;

    if (hdr.magic != MH_MAGIC_64)
        return sections;

    vm_address_t slide = detail::calculate_slide(task, image_base);
    vm_address_t cursor = image_base + sizeof(hdr);

    for (uint32_t i = 0; i < hdr.ncmds; ++i) {
        load_command lc{};
        if (!memory::read_value(task, cursor, lc))
            break;

        if (lc.cmd == LC_SEGMENT_64) {
            segment_command_64 seg{};
            if (memory::read_value(task, cursor, seg)) {
                vm_address_t section_cursor = cursor + sizeof(segment_command_64);

                for (uint32_t j = 0; j < seg.nsects; ++j) {
                    section_64 sect{};
                    if (memory::read_value(task, section_cursor, sect)) {
                        SectionInfo info;
                        info.address = sect.addr + slide;
                        info.size = sect.size;
                        info.segment_name = sect.segname;
                        info.section_name = sect.sectname;
                        info.flags = sect.flags;
                        sections.push_back(info);
                    }
                    section_cursor += sizeof(section_64);
                }
            }
        }

        cursor += lc.cmdsize;
    }

    return sections;
}

inline std::vector<SectionInfo> get_sections_in_segment(
    task_t task,
    vm_address_t image_base,
    std::string_view segment_name) 
{
    auto all_sections = get_all_sections(task, image_base);
    std::vector<SectionInfo> result;

    for (const auto& sect : all_sections) {
        if (sect.segment_name == segment_name) {
            result.push_back(sect);
        }
    }

    return result;
}

inline bool is_code_section(const SectionInfo& sect) {
    return (sect.flags & S_ATTR_PURE_INSTRUCTIONS) || (sect.flags & S_ATTR_SOME_INSTRUCTIONS);
}

inline vm_size_t get_total_code_size(task_t task, vm_address_t image_base) {
    auto sections = get_all_sections(task, image_base);
    vm_size_t total = 0;

    for (const auto& sect : sections) {
        if (is_code_section(sect)) {
            total += sect.size;
        }
    }

    return total;
}

inline void print_segments(task_t task, vm_address_t image_base) {
    auto segments = get_all_segments(task, image_base);

    std::println("{:<16} {:>16} {:>16} {:>12}", "Segment", "Address", "End", "Size");
    std::println("{:-<16} {:-<16} {:-<16} {:-<12}", "", "", "", "");

    for (const auto& seg : segments) {
        std::println("{:<16} 0x{:014X} 0x{:014X} 0x{:X}",
                     seg.name, seg.address, seg.address + seg.size, seg.size);
    }
}

inline void print_sections(task_t task, vm_address_t image_base) {
    auto sections = get_all_sections(task, image_base);

    std::println("{:<16} {:<20} {:>16} {:>12}", "Segment", "Section", "Address", "Size");
    std::println("{:-<16} {:-<20} {:-<16} {:-<12}", "", "", "", "");

    for (const auto& sect : sections) {
        std::println("{:<16} {:<20} 0x{:014X} 0x{:X}",
                     sect.segment_name, sect.section_name, sect.address, sect.size);
    }
}

} // namespace macho
