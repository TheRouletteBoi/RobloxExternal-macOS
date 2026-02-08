#pragma once

#include "memory/memory.hpp"
#include "macho/macho.hpp"
#include "scanner/scanner.hpp"
#include "roblox/offsets.hpp"
#include "roblox/math.hpp"
#include "roblox/string.hpp"

#include <print>
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <cxxabi.h>

namespace dumper {

namespace constant_find {
    constexpr float FIELD_OF_VIEW = 113.2f;

    constexpr float CAMERA_POSITION_X = 45.2f;
    constexpr float CAMERA_POSITION_Y = 19.4f;
    constexpr float CAMERA_POSITION_Z = 50.0f;

    constexpr float HUMANOID_HEALTH = 87.3f;
    constexpr float HUMANOID_MAX_HEALTH = 87.3f;
    constexpr float HUMANOID_WALK_SPEED = 23.7f;
    constexpr float HUMANOID_HIP_HEIGHT = 3.14159f;
    constexpr float HUMANOID_JUMP_POWER = 67.89f;
    constexpr float HUMANOID_JUMP_HEIGHT = 11.22f;

    constexpr float PART_POSITION_X = 123.456f;
    constexpr float PART_POSITION_Y = 78.9f;
    constexpr float PART_POSITION_Z = -42.42f;

    constexpr float PART_SIZE_X = 5.55f;
    constexpr float PART_SIZE_Y = 3.33f;
    constexpr float PART_SIZE_Z = 7.77f;
}

inline std::optional<std::string> try_read_string_at(
    task_t task,
    vm_address_t object_address,
    int offset
) {
    vm_address_t string_pointer = 0;
    if (!memory::read_value(task, object_address + offset, string_pointer) ||
        string_pointer == 0) {
        return std::nullopt;
    }

    std::string result;
    if (!memory::read_cstring(task, string_pointer, result, 256)) {
        return std::nullopt;
    }

    // Validate that string contains only printable ASCII
    for (char c : result) {
        if (c != 0 && (c < 0x20 || c > 0x7E)) {
            return std::nullopt;
        }
    }

    return result.empty() ? std::nullopt : std::optional<std::string>(result);
}

inline std::optional<std::string> read_class_name(
    task_t task,
    vm_address_t object_address,
    int class_info_offset = 0x18
) {
    vm_address_t class_info = 0;
    if (!memory::read_value(task, object_address + class_info_offset, class_info) ||
        class_info == 0) {
        return std::nullopt;
    }

    vm_address_t class_name_ptr = 0;
    if (!memory::read_value(task, class_info + 8, class_name_ptr) ||
        class_name_ptr == 0) {
        return std::nullopt;
    }

    std::string class_name;
    if (!memory::read_cstring(task, class_name_ptr, class_name, 128)) {
        return std::nullopt;
    }

    return std::optional(class_name);
}

inline bool is_valid_cframe(const roblox::CFrame& cframe) {
    float values[] = {
        cframe.r0, cframe.r1, cframe.r2,
        cframe.r10, cframe.r11, cframe.r12,
        cframe.r20, cframe.r21, cframe.r22,
        cframe.position.x, cframe.position.y, cframe.position.z
    };

    for (float value : values) {
        if (std::isnan(value) || std::isinf(value)) {
            return false;
        }
    }

    auto magnitude = [](float a, float b, float c) {
        return std::sqrt(a * a + b * b + c * c);
    };

    // Each row of the rotation matrix should have magnitude ~1.0
    constexpr float MAGNITUDE_TOLERANCE = 0.05f;
    if (std::abs(magnitude(cframe.r0,  cframe.r1,  cframe.r2)  - 1.0f) > MAGNITUDE_TOLERANCE) return false;
    if (std::abs(magnitude(cframe.r10, cframe.r11, cframe.r12) - 1.0f) > MAGNITUDE_TOLERANCE) return false;
    if (std::abs(magnitude(cframe.r20, cframe.r21, cframe.r22) - 1.0f) > MAGNITUDE_TOLERANCE) return false;

    // Position should be reasonable (not astronomical values)
    constexpr float MAX_POSITION = 1e6f;
    if (std::abs(cframe.position.x) > MAX_POSITION ||
        std::abs(cframe.position.y) > MAX_POSITION ||
        std::abs(cframe.position.z) > MAX_POSITION) {
        return false;
    }

    return true;
}

struct OffsetInfo {
    std::string name;
    uintptr_t offset;
    bool found;
    std::string category;
};

class DumperContext {
public:
    DumperContext(task_t task)
        : m_task(task) {}

    struct LiveInstances {
        vm_address_t game = 0;
        vm_address_t workspace = 0;
        vm_address_t players = 0;
        vm_address_t camera = 0;
        vm_address_t local_player = 0;
        vm_address_t character = 0;
        vm_address_t humanoid = 0;
        vm_address_t hrp = 0;
    };

    void find_studio_offsets(const LiveInstances& live) {
        std::println("\n=== Starting Offset Finder ===\n");

        if (live.camera) {
            find_camera_offsets(live.camera);
        }

        if (live.local_player) {
            find_player_offsets(live.local_player, live.character);
        }

        if (live.humanoid) {
            find_humanoid_offsets(live.humanoid);
        }

        if (live.hrp) {
            find_basepart_offsets(live.hrp);
        }

        if (live.players && live.local_player) {
            find_players_service_offsets(live.players, live.local_player);
        }

        if (live.workspace && live.camera) {
            find_workspace_offsets(live.workspace, live.camera);
        }
    }

    void print_found_offsets() const {
        std::println("\n=== Offset Results ===\n");

        std::map<std::string, std::vector<OffsetInfo>> by_category;
        for (const auto& offset : m_offsets) {
            by_category[offset.category].push_back(offset);
        }

        int total = 0;
        int found = 0;

        for (const auto& [category, offsets] : by_category) {
            std::println("[{}]", category);
            for (const auto& offset : offsets) {
                total++;
                if (offset.found) {
                    found++;
                    std::println("  {} = {:#x}", offset.name, offset.offset);
                } else {
                    std::println("  {} = NOT FOUND", offset.name);
                }
            }
            std::println("");
        }

        std::println("Total: {}/{} offsets found ({:.1f}%)\n",
                    found, total, (100.0 * found) / total);
    }

    bool write_offsets_hpp(const std::string& output_path) const {
        std::ofstream file(output_path);
        if (!file.is_open()) {
            std::println("Failed to open {} for writing", output_path);
            return false;
        }

        // TODO(Roulette): get roblox version
        std::string robloxVersion = "TODO";

        int found_count = 0;
        int total_count = static_cast<int>(m_offsets.size());
        for (const auto& offset : m_offsets) {
            if (offset.found) {
                ++found_count;
            }
        }

        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm gmt_tm;
        gmtime_r(&time_t_now, &gmt_tm);

        const char* weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

        std::string timestamp = std::format("{}, {:02d} {} {} {:02d}:{:02d}:{:02d} GMT",
            weekdays[gmt_tm.tm_wday],
            gmt_tm.tm_mday,
            months[gmt_tm.tm_mon],
            gmt_tm.tm_year + 1900,
            gmt_tm.tm_hour,
            gmt_tm.tm_min,
            gmt_tm.tm_sec);

        file << "/*\n";
        file << "* App Name: RobloxExternal-macOS\n";
        file << "* Author: TheRouLetteBoi\n";
        file << "* Repository: https://github.com/TheRouletteBoi/RobloxExternal-macOS\n";
        file << "* Generated on: " << timestamp << "\n";
        file << "* Total Offsets: " << found_count << "/" << total_count << "\n";
        file << "* Roblox Version: " << robloxVersion << "\n";
        file << "**/\n\n";
        file << "#pragma once\n\n";

        std::map<std::string, std::vector<OffsetInfo>> by_category;
        for (const auto& offset : m_offsets) {
            if (offset.found) {
                by_category[offset.category].push_back(offset);
            }
        }

        file << "namespace offsets {\n\n";

        for (const auto& [category, offsets] : by_category) {
            file << "    namespace " << category << " {\n";
            for (const auto& offset : offsets) {
                file << "        inline uintptr_t " << offset.name
                     << " = " << std::format("{:#x}", offset.offset) << ";\n";
            }
            file << "    }\n\n";
        }

        file << "} // namespace offsets\n";
        file.close();

        std::println("Wrote offsets to: {}", output_path);
        return true;
    }

private:
    task_t m_task;
    std::vector<OffsetInfo> m_offsets;

    void set_offset_found(const std::string& category, const std::string& name,
                         uintptr_t& global_offset, uintptr_t found_offset) {
        global_offset = found_offset;
        m_offsets.push_back({name, found_offset, true, category});
        std::println("  {} at offset {:#x}", name, found_offset);
    }

    void set_offset_failed(const std::string& category, const std::string& name) {
        m_offsets.push_back({name, 0, false, category});
        std::println("  {} NOT FOUND", name);
    }

    std::optional<int> scan_for_float(
        vm_address_t base_address,
        float target_value,
        int start_offset,
        int end_offset,
        int alignment = 4,
        float tolerance = 0.01f
    ) {
        for (int offset = start_offset; offset < end_offset; offset += alignment) {
            float value = 0;
            if (memory::read_value(m_task, base_address + offset, value)) {
                if (std::abs(value - target_value) < tolerance) {
                    return offset;
                }
            }
        }
        return std::nullopt;
    }

    std::optional<int> scan_for_vector3(
        vm_address_t base_address,
        const roblox::Vector3& target,
        int start_offset,
        int end_offset,
        int alignment = 4,
        float tolerance = 0.01f
    ) {
        for (int offset = start_offset; offset < end_offset; offset += alignment) {
            roblox::Vector3 vec;
            if (memory::read_value(m_task, base_address + offset, vec)) {
                if (std::abs(vec.x - target.x) < tolerance &&
                    std::abs(vec.y - target.y) < tolerance &&
                    std::abs(vec.z - target.z) < tolerance) {
                    return offset;
                }
            }
        }
        return std::nullopt;
    }

    std::optional<int> scan_for_pointer(
        vm_address_t base_address,
        vm_address_t target_pointer,
        int start_offset,
        int end_offset
    ) {
        for (int offset = start_offset; offset < end_offset; offset += 8) {
            vm_address_t pointer = 0;
            if (memory::read_value(m_task, base_address + offset, pointer)) {
                if (pointer == target_pointer) {
                    return offset;
                }
            }
        }
        return std::nullopt;
    }

    std::vector<int> scan_for_cframes(
        vm_address_t base_address,
        int start_offset,
        int end_offset
    ) {
        std::vector<int> results;

        for (int offset = start_offset; offset < end_offset; offset += 4) {
            roblox::CFrame cframe;
            if (memory::read_value(m_task, base_address + offset, cframe)) {
                if (is_valid_cframe(cframe)) {
                    results.push_back(offset);
                }
            }
        }

        return results;
    }

    void find_camera_offsets(vm_address_t camera_address) {
        std::println("Camera at {:#x}", camera_address);

        auto fov_offset = scan_for_float(
            camera_address,
            constant_find::FIELD_OF_VIEW,
            0x100,
            0x200
        );

        if (fov_offset) {
            set_offset_found("Camera", "CAMERA_FIELDOFVIEW",
                           offsets::Camera::CAMERA_FIELDOFVIEW, *fov_offset);
        } else {
            set_offset_failed("Camera", "CAMERA_FIELDOFVIEW");
        }

        roblox::Vector3 constant_position{
            constant_find::CAMERA_POSITION_X,
            constant_find::CAMERA_POSITION_Y,
            constant_find::CAMERA_POSITION_Z
        };

        for (int offset : scan_for_cframes(camera_address, 0x80, 0x150)) {
            roblox::CFrame cframe;
            memory::read_value(m_task, camera_address + offset, cframe);

            if (std::abs(cframe.position.x - constant_position.x) < 0.1f &&
                std::abs(cframe.position.y - constant_position.y) < 0.1f &&
                std::abs(cframe.position.z - constant_position.z) < 0.1f) {

                set_offset_found("Camera", "CAMERA_CFRAME",
                               offsets::Camera::CAMERA_CFRAME, offset);
                break;
            }
        }

        for (int offset = 0xc0; offset < 0x120; offset += 8) {
            vm_address_t pointer = 0;
            if (memory::read_value(m_task, camera_address + offset, pointer) &&
                pointer != 0) {

                auto class_name = read_class_name(m_task, pointer);
                if (class_name && *class_name == "Humanoid") {
                    set_offset_found("Camera", "CAMERA_CAMERASUBJECT",
                                   offsets::Camera::CAMERA_CAMERASUBJECT, offset);
                    break;
                }
            }
        }
    }

    void find_player_offsets(vm_address_t player_address, vm_address_t character_address) {
        std::println("Player at {:#x}", player_address);

        if (character_address) {
            auto character_offset = scan_for_pointer(
                player_address,
                character_address,
                0x300,
                0x400
            );

            if (character_offset) {
                set_offset_found("Player", "PLAYER_CHARACTER",
                               offsets::Player::PLAYER_CHARACTER, *character_offset);
            } else {
                set_offset_failed("Player", "PLAYER_CHARACTER");
            }
        }

        for (int offset = 0x100; offset < 0x200; offset += 8) {
            auto display_name = try_read_string_at(m_task, player_address, offset);
            if (display_name && !display_name->empty()) {
                set_offset_found("Player", "PLAYER_DISPLAYNAME",
                               offsets::Player::PLAYER_DISPLAYNAME, offset);
                break;
            }
        }

        for (int offset = 0x200; offset < 0x300; offset += 8) {
            vm_address_t pointer = 0;
            if (memory::read_value(m_task, player_address + offset, pointer) &&
                pointer != 0) {

                auto class_name = read_class_name(m_task, pointer);
                if (class_name && *class_name == "Team") {
                    set_offset_found("Player", "PLAYER_TEAM",
                                   offsets::Player::PLAYER_TEAM, offset);
                    break;
                }
            }
        }

        for (int offset = 0x260; offset < 0x2a0; offset += 4) {
            int64_t user_id = 0;
            if (memory::read_value(m_task, player_address + offset, user_id) &&
                user_id > 0 && user_id < 10000000000LL) {
                set_offset_found("Player", "PLAYER_USERID",
                               offsets::Player::PLAYER_USERID, offset);
                break;
            }
        }

        for (int offset = 0x2a0; offset < 0x2e0; offset += 4) {
            int account_age = 0;
            if (memory::read_value(m_task, player_address + offset, account_age) &&
                account_age >= 0 && account_age <= 10000) {
                set_offset_found("Player", "PLAYER_ACCOUNTAGE",
                               offsets::Player::PLAYER_ACCOUNTAGE, offset);
                break;
            }
        }
    }

    void find_humanoid_offsets(vm_address_t humanoid_address) {
        std::println("Humanoid at {:#x}", humanoid_address);

        auto health_offset = scan_for_float(
            humanoid_address,
            constant_find::HUMANOID_HEALTH,
            0x170,
            0x1a0
        );

        if (health_offset) {
            set_offset_found("Humanoid", "HUMANOID_HEALTH",
                           offsets::Humanoid::HUMANOID_HEALTH, *health_offset);
        } else {
            set_offset_failed("Humanoid", "HUMANOID_HEALTH");
        }

        auto max_health_offset = scan_for_float(
            humanoid_address,
            constant_find::HUMANOID_MAX_HEALTH,
            0x1a0,
            0x1c0
        );

        if (max_health_offset) {
            set_offset_found("Humanoid", "HUMANOID_MAXHEALTH",
                           offsets::Humanoid::HUMANOID_MAXHEALTH, *max_health_offset);
        } else {
            set_offset_failed("Humanoid", "HUMANOID_MAXHEALTH");
        }

        auto walkspeed_offset = scan_for_float(
            humanoid_address,
            constant_find::HUMANOID_WALK_SPEED,
            0x1c0,
            0x1e0
        );

        if (walkspeed_offset) {
            set_offset_found("Humanoid", "HUMANOID_WALKSPEED",
                           offsets::Humanoid::HUMANOID_WALKSPEED, *walkspeed_offset);
        } else {
            set_offset_failed("Humanoid", "HUMANOID_WALKSPEED");
        }

        auto hipheight_offset = scan_for_float(
            humanoid_address,
            constant_find::HUMANOID_HIP_HEIGHT,
            0x180,
            0x1a0
        );

        if (hipheight_offset) {
            set_offset_found("Humanoid", "HUMANOID_HIPHEIGHT",
                           offsets::Humanoid::HUMANOID_HIPHEIGHT, *hipheight_offset);
        } else {
            set_offset_failed("Humanoid", "HUMANOID_HIPHEIGHT");
        }

        auto jumppower_offset = scan_for_float(
            humanoid_address,
            constant_find::HUMANOID_JUMP_POWER,
            0x190,
            0x1b0
        );

        if (jumppower_offset) {
            set_offset_found("Humanoid", "HUMANOID_JUMPPOWER",
                           offsets::Humanoid::HUMANOID_JUMPPOWER, *jumppower_offset);
        } else {
            set_offset_failed("Humanoid", "HUMANOID_JUMPPOWER");
        }

        auto jumpheight_offset = scan_for_float(
            humanoid_address,
            constant_find::HUMANOID_JUMP_HEIGHT,
            0x190,
            0x1b0
        );

        if (jumpheight_offset) {
            set_offset_found("Humanoid", "HUMANOID_JUMPHEIGHT",
                           offsets::Humanoid::HUMANOID_JUMPHEIGHT, *jumpheight_offset);
        } else {
            set_offset_failed("Humanoid", "HUMANOID_JUMPHEIGHT");
        }

        for (int offset = 0xc0; offset < 0x100; offset += 8) {
            auto display_name = try_read_string_at(m_task, humanoid_address, offset);
            if (display_name && !display_name->empty()) {
                set_offset_found("Humanoid", "HUMANOID_DISPLAYNAME",
                               offsets::Humanoid::HUMANOID_DISPLAYNAME, offset);
                break;
            }
        }

        for (int offset = 0x100; offset < 0x140; offset += 8) {
            vm_address_t pointer = 0;
            if (memory::read_value(m_task, humanoid_address + offset, pointer)) {
                if (pointer == 0) {
                    set_offset_found("Humanoid", "HUMANOID_SEATPART",
                                   offsets::Humanoid::HUMANOID_SEATPART, offset);
                    break;
                }
            }
        }
    }

    void find_basepart_offsets(vm_address_t basepart_address) {
        std::println("BasePart at {:#x}", basepart_address);

        vm_address_t properties_address = 0;
        std::optional<int> properties_offset;

        for (int offset = 0x100; offset < 0x200; offset += 8) {
            vm_address_t pointer = 0;
            if (!memory::read_value(m_task, basepart_address + offset, pointer) ||
                pointer == 0) {
                continue;
            }

            roblox::Vector3 constant_position{
                constant_find::PART_POSITION_X,
                constant_find::PART_POSITION_Y,
                constant_find::PART_POSITION_Z
            };

            auto position_offset = scan_for_vector3(
                pointer,
                constant_position,
                0xd0,
                0x110,
                4,
                0.1f
            );

            if (position_offset) {
                properties_offset = offset;
                properties_address = pointer;
                break;
            }
        }

        if (!properties_offset) {
            set_offset_failed("BasePart", "BASEPART_PROPERTIES");
            return;
        }

        set_offset_found("BasePart", "BASEPART_PROPERTIES",
                        offsets::BasePart::BASEPART_PROPERTIES, *properties_offset);

        std::println("Primitive at {:#x}", properties_address);

        roblox::Vector3 constant_position{
            constant_find::PART_POSITION_X,
            constant_find::PART_POSITION_Y,
            constant_find::PART_POSITION_Z
        };

        auto position_offset = scan_for_vector3(
            properties_address,
            constant_position,
            0xd0,
            0x110
        );

        if (position_offset) {
            int cframe_offset = *position_offset - 36; // Position is 36 bytes into CFrame

            roblox::CFrame cframe;
            if (memory::read_value(m_task, properties_address + cframe_offset, cframe) &&
                is_valid_cframe(cframe)) {

                set_offset_found("Primitive", "BASEPART_PROPS_CFRAME",
                               offsets::Primitive::BASEPART_PROPS_CFRAME, cframe_offset);
                set_offset_found("Primitive", "BASEPART_PROPS_POSITION",
                               offsets::Primitive::BASEPART_PROPS_POSITION, *position_offset);

                for (int delta = 0x30; delta <= 0x40; delta += 4) {
                    roblox::Vector3 velocity;
                    if (memory::read_value(m_task, properties_address + cframe_offset + delta, velocity) &&
                        velocity.magnitude() < 1.0f) {

                        set_offset_found("Primitive", "BASEPART_PROPS_VELOCITY",
                                       offsets::Primitive::BASEPART_PROPS_VELOCITY, cframe_offset + delta);
                        set_offset_found("Primitive", "BASEPART_PROPS_ROTVELOCITY",
                                       offsets::Primitive::BASEPART_PROPS_ROTVELOCITY, cframe_offset + delta + 0xC);
                        break;
                    }
                }

                set_offset_found("Primitive", "BASEPART_PROPS_RECEIVEAGE",
                               offsets::Primitive::BASEPART_PROPS_RECEIVEAGE, cframe_offset - 4);
            } else {
                set_offset_failed("Primitive", "BASEPART_PROPS_CFRAME");
            }
        } else {
            set_offset_failed("Primitive", "BASEPART_PROPS_CFRAME");
        }

        roblox::Vector3 constant_size{
            constant_find::PART_SIZE_X,
            constant_find::PART_SIZE_Y,
            constant_find::PART_SIZE_Z
        };

        auto size_offset = scan_for_vector3(
            properties_address,
            constant_size,
            0x100,
            0x250,
            4,
            0.1f
        );

        if (size_offset) {
            set_offset_found("Primitive", "BASEPART_PROPS_SIZE",
                           offsets::Primitive::BASEPART_PROPS_SIZE, *size_offset);

            set_offset_found("Primitive", "BASEPART_PROPS_CANCOLLIDE",
                           offsets::Primitive::BASEPART_PROPS_CANCOLLIDE, *size_offset - 2);
        } else {
            set_offset_failed("Primitive", "BASEPART_PROPS_SIZE");
        }

        for (int offset = 0x170; offset < 0x1a0; offset += 4) {
            roblox::Vector3 color;
            if (memory::read_value(m_task, properties_address + offset, color) &&
                color.x >= 0.0f && color.x <= 1.0f &&
                color.y >= 0.0f && color.y <= 1.0f &&
                color.z >= 0.0f && color.z <= 1.0f) {
                set_offset_found("BasePart", "BASEPART_COLOR",
                               offsets::BasePart::BASEPART_COLOR, offset);
                break;
            }
        }

        for (int offset = 0xe0; offset < 0x110; offset += 4) {
            float transparency = 0;
            if (memory::read_value(m_task, properties_address + offset, transparency) &&
                transparency >= 0.0f && transparency <= 1.0f) {
                set_offset_found("BasePart", "BASEPART_TRANSPARENCY",
                               offsets::BasePart::BASEPART_TRANSPARENCY, offset);
                break;
            }
        }
    }

    void find_players_service_offsets(
        vm_address_t players_address,
        vm_address_t local_player_address
    ) {
        std::println("Players at {:#x}", players_address);

        if (local_player_address) {
            auto local_player_offset = scan_for_pointer(
                players_address,
                local_player_address,
                0x100,
                0x200
            );

            if (local_player_offset) {
                set_offset_found("Players", "PLAYERS_LOCALPLAYER",
                               offsets::Players::PLAYERS_LOCALPLAYER, *local_player_offset);
            } else {
                set_offset_failed("Players", "PLAYERS_LOCALPLAYER");
            }
        }

        for (int offset = 0x100; offset < 0x200; offset += 4) {
            int max_players = 0;
            if (memory::read_value(m_task, players_address + offset, max_players) &&
                max_players > 0 && max_players <= 700) {

                set_offset_found("Players", "PLAYERS_MAXPLAYERS",
                               offsets::Players::PLAYERS_MAXPLAYERS, offset);
                return;
            }
        }

        set_offset_failed("Players", "PLAYERS_MAXPLAYERS");
    }

    void find_workspace_offsets(
        vm_address_t workspace_address,
        vm_address_t camera_address
    ) {
        std::println("Workspace at {:#x}", workspace_address);

        if (camera_address) {
            auto current_camera_offset = scan_for_pointer(
                workspace_address,
                camera_address,
                0x200,
                0x600
            );

            if (current_camera_offset) {
                set_offset_found("Workspace", "WORKSPACE_CURRENTCAMERA",
                               offsets::Workspace::WORKSPACE_CURRENTCAMERA, *current_camera_offset);
            } else {
                set_offset_failed("Workspace", "WORKSPACE_CURRENTCAMERA");
            }
        }
    }
};

inline bool validate_instance(
    task_t task,
    vm_address_t instance_address,
    const std::string& expected_name
) {
    vm_address_t self_pointer = 0;
    if (!memory::read_value(task, instance_address + offsets::Instance::INSTANCE_SELF, self_pointer) ||
        self_pointer != instance_address) {
        return false;
    }

    auto actual_name = try_read_string_at(task, instance_address, offsets::Instance::INSTANCE_NAME);
    return actual_name && *actual_name == expected_name;
}

} // namespace dumper
