#pragma once

#include "roblox/instance.hpp"
#include "roblox/offsets.hpp"
#include "roblox/math.hpp"

namespace roblox {

class BasePart : public Instance {
public:
    using Instance::Instance;
    
    BasePart(const Instance& inst) : Instance(inst) {}
    
    // Position/Rotation
    CFrame cframe() const {
        vm_address_t props_ptr = 0;
        if (!memory::read_value(task(), address() + offsets::BasePart::BASEPART_PROPERTIES, props_ptr)) {
            return {};
        }
        if (props_ptr == 0)
            return {};
        
        CFrame cf;
        memory::read_value(task(), props_ptr + offsets::Primitive::BASEPART_PROPS_CFRAME, cf);
        return cf;
    }
    
    bool set_cframe(const CFrame& cf) const {
        vm_address_t props_ptr = 0;
        if (!memory::read_value(task(), address() + offsets::BasePart::BASEPART_PROPERTIES, props_ptr)) {
            return false;
        }
        if (props_ptr == 0)
            return false;
        
        return vm_write(task(), props_ptr + offsets::Primitive::BASEPART_PROPS_CFRAME,
                       reinterpret_cast<vm_offset_t>(&cf), sizeof(cf)) == KERN_SUCCESS;
    }
    
    Vector3 position() const {
        return cframe().position;
    }
    
    bool set_position(const Vector3& pos) const {
        CFrame cf = cframe();
        cf.position = pos;
        return set_cframe(cf);
    }
    
    // Velocity
    Vector3 velocity() const {
        vm_address_t props_ptr = 0;
        if (!memory::read_value(task(), address() + offsets::BasePart::BASEPART_PROPERTIES, props_ptr)) {
            return {};
        }
        if (props_ptr == 0)
            return {};
        
        Vector3 vel;
        memory::read_value(task(), props_ptr + offsets::Primitive::BASEPART_PROPS_VELOCITY, vel);
        return vel;
    }
    
    bool set_velocity(const Vector3& vel) const {
        vm_address_t props_ptr = 0;
        if (!memory::read_value(task(), address() + offsets::BasePart::BASEPART_PROPERTIES, props_ptr)) {
            return false;
        }
        if (props_ptr == 0)
            return false;
        
        return vm_write(task(), props_ptr + offsets::Primitive::BASEPART_PROPS_VELOCITY,
                       reinterpret_cast<vm_offset_t>(&vel), sizeof(vel)) == KERN_SUCCESS;
    }
    
    // Angular velocity
    Vector3 rotational_velocity() const {
        vm_address_t props_ptr = 0;
        if (!memory::read_value(task(), address() + offsets::BasePart::BASEPART_PROPERTIES, props_ptr)) {
            return {};
        }
        if (props_ptr == 0)
            return {};
        
        Vector3 rot_vel;
        memory::read_value(task(), props_ptr + offsets::Primitive::BASEPART_PROPS_ROTVELOCITY, rot_vel);
        return rot_vel;
    }

    Vector3 size() const {
        vm_address_t props_ptr = 0;
        if (!memory::read_value(task(), address() + offsets::BasePart::BASEPART_PROPERTIES, props_ptr)) {
            return {};
        }
        if (props_ptr == 0)
            return {};
        
        Vector3 sz;
        memory::read_value(task(), props_ptr + offsets::Primitive::BASEPART_PROPS_SIZE, sz);
        return sz;
    }

    bool can_collide() const {
        vm_address_t props_ptr = 0;
        if (!memory::read_value(task(), address() + offsets::BasePart::BASEPART_PROPERTIES, props_ptr)) {
            return true;
        }
        if (props_ptr == 0)
            return true;
        
        uint8_t value = 0;
        memory::read_value(task(), props_ptr + offsets::Primitive::BASEPART_PROPS_CANCOLLIDE, value);
        return value != 0;
    }
    
    bool set_can_collide(bool collide) const {
        vm_address_t props_ptr = 0;
        if (!memory::read_value(task(), address() + offsets::BasePart::BASEPART_PROPERTIES, props_ptr)) {
            return false;
        }
        if (props_ptr == 0)
            return false;
        
        uint8_t value = collide ? 1 : 0;
        return vm_write(task(), props_ptr + offsets::Primitive::BASEPART_PROPS_CANCOLLIDE,
                       reinterpret_cast<vm_offset_t>(&value), 1) == KERN_SUCCESS;
    }
};

class Humanoid : public Instance {
public:
    using Instance::Instance;
    
    Humanoid(const Instance& inst) : Instance(inst) {}
    
    float health() const {
        return read_property<float>(offsets::Humanoid::HUMANOID_HEALTH).value_or(0.0f);
    }
    
    bool set_health(float value) const {
        return write_property(offsets::Humanoid::HUMANOID_HEALTH, value);
    }
    
    float walk_speed() const {
        return read_property<float>(offsets::Humanoid::HUMANOID_WALKSPEED).value_or(16.0f);
    }
    
    bool set_walk_speed(float value) const {
        return write_property(offsets::Humanoid::HUMANOID_WALKSPEED, value);
    }
    
    float hip_height() const {
        return read_property<float>(offsets::Humanoid::HUMANOID_HIPHEIGHT).value_or(2.0f);
    }
    
    std::optional<std::string> display_name() const {
        return read_rbx_string_at(task(), address(), offsets::Humanoid::HUMANOID_DISPLAYNAME);
    }
    
    Instance seat_part() const {
        vm_address_t seat_addr = 0;
        if (memory::read_value(task(), address() + offsets::Humanoid::HUMANOID_SEATPART, seat_addr)) {
            return Instance(task(), seat_addr);
        }
        return {};
    }
    
    bool is_seated() const {
        return seat_part().is_valid();
    }
};

class Camera : public Instance {
public:
    using Instance::Instance;
    
    Camera(const Instance& inst) : Instance(inst) {}
    
    CFrame cframe() const {
        CFrame cf;
        memory::read_value(task(), address() + offsets::Camera::CAMERA_CFRAME, cf);
        return cf;
    }
    
    bool set_cframe(const CFrame& cf) const {
        return write_property(offsets::Camera::CAMERA_CFRAME, cf);
    }

    // field of view is in degrees
    float field_of_view() const {
        return read_property<float>(offsets::Camera::CAMERA_FIELDOFVIEW).value_or(70.0f);
    }
    
    bool set_field_of_view(float fov) const {
        return write_property(offsets::Camera::CAMERA_FIELDOFVIEW, fov);
    }
    
    Instance camera_subject() const {
        vm_address_t subject_addr = 0;
        if (memory::read_value(task(), address() + offsets::Camera::CAMERA_CAMERASUBJECT, subject_addr)) {
            return Instance(task(), subject_addr);
        }
        return {};
    }
    
    Vector3 position() const {
        return cframe().position;
    }
    
    Vector3 look_vector() const {
        return cframe().look_vector();
    }
};

class Player : public Instance {
public:
    using Instance::Instance;
    
    Player(const Instance& inst) : Instance(inst) {}
    
    Instance character() const {
        vm_address_t char_addr = 0;
        if (memory::read_value(task(), address() + offsets::Player::PLAYER_CHARACTER, char_addr)) {
            return Instance(task(), char_addr);
        }
        return {};
    }
    
    Instance team() const {
        vm_address_t team_addr = 0;
        if (memory::read_value(task(), address() + offsets::Player::PLAYER_TEAM, team_addr)) {
            return Instance(task(), team_addr);
        }
        return {};
    }
    
    std::optional<std::string> display_name() const {
        return read_rbx_string_at(task(), address(), offsets::Player::PLAYER_DISPLAYNAME);
    }
    
    double last_input_timestamp() const {
        return read_property<double>(offsets::Player::PLAYER_LAST_INPUT_TIMESTAMP).value_or(0.0);
    }
    
    bool set_last_input_timestamp(double value) const {
        return write_property(offsets::Player::PLAYER_LAST_INPUT_TIMESTAMP, value);
    }
    
    // Convenience: Get the HumanoidRootPart
    BasePart humanoid_root_part() const {
        auto chr = character();
        if (!chr)
            return {};
        return BasePart(chr.find_first_child("HumanoidRootPart"));
    }
    
    // Convenience: Get the Humanoid
    Humanoid humanoid() const {
        auto chr = character();
        if (!chr)
            return {};
        return Humanoid(chr.find_first_child_of_class("Humanoid"));
    }
};

class Model : public Instance {
public:
    using Instance::Instance;
    
    Model(const Instance& inst) : Instance(inst) {}
    
    BasePart primary_part() const {
        vm_address_t part_addr = 0;
        if (memory::read_value(task(), address() + offsets::ModelPrimative::MODEL_PRIMARYPART, part_addr)) {
            return BasePart(Instance(task(), part_addr));
        }
        return {};
    }

    BasePart humanoid_root_part() const {
        return BasePart(find_first_child("HumanoidRootPart"));
    }

    Humanoid humanoid() const {
        return Humanoid(find_first_child_of_class("Humanoid"));
    }
};

class Team : public Instance {
public:
    using Instance::Instance;
    
    Team(const Instance& inst) : Instance(inst) {}
    
    int brick_color() const {
        return read_property<int>(offsets::Team::TEAM_BRICKCOLOR).value_or(0);
    }
};

class IntValue : public Instance {
public:
    using Instance::Instance;
    IntValue(const Instance& inst) : Instance(inst) {}
    
    int value() const {
        return read_property<int>(offsets::ValueObjects::INTVALUE_VALUE).value_or(0);
    }
    
    bool set_value(int v) const {
        return write_property(offsets::ValueObjects::INTVALUE_VALUE, v);
    }
};

class StringValue : public Instance {
public:
    using Instance::Instance;
    StringValue(const Instance& inst) : Instance(inst) {}
    
    std::optional<std::string> value() const {
        return read_rbx_string_at(task(), address(), offsets::ValueObjects::STRINGVALUE_VALUE);
    }
};

class Players : public Instance {
public:
    using Instance::Instance;
    
    Players(const Instance& inst) : Instance(inst) {}
    
    int max_players() const {
        return read_property<int>(offsets::Players::PLAYERS_MAXPLAYERS).value_or(0);
    }

    Player local_player() const {
        return Player(find_first_child_of_class("Player"));
    }

    std::vector<Player> get_players() const {
        std::vector<Player> result;
        for (const auto& child : children()) {
            if (child.is_a("Player")) {
                result.emplace_back(child);
            }
        }
        return result;
    }

    Player find_player(std::string_view name_to_find) const {
        for (const auto& child : children()) {
            if (child.is_a("Player") && child.is_named(name_to_find)) {
                return Player(child);
            }
        }
        return {};
    }

    Player get_player_from_character(const Instance& character) const {
        if (!character) return {};
        
        for (const auto& child : children()) {
            Player player(child);
            if (player.character() == character) {
                return player;
            }
        }
        return {};
    }
};

class Workspace : public Instance {
public:
    using Instance::Instance;
    
    Workspace(const Instance& inst) : Instance(inst) {}
    
    Camera current_camera() const {
        return Camera(find_first_child_of_class("Camera"));
    }
};

} // namespace roblox
