#pragma once

#include "roblox.hpp"
#include "esp_controller.hpp"

#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace games {

struct Target {
    roblox::Instance character;
    roblox::Vector3 aim_position;       // World position to aim at
    roblox::Vector3 screen_position;    // Screen position (x, y, depth)
    float distance_3d;                  // World distance from local player
    float delta_distance;               // Distance from crosshair ray (studs)
    float health;
    std::string name;
    bool is_teammate;
    bool is_valid;

    vm_address_t custom_aim_part = 0;
};

enum class AimStyle { Silent, Legit, Snap };
enum class TargetSelection { ClosestToCrosshair, ClosestDistance, LowestHealth };
enum class AimPart { Head, Torso, Custom };
enum class AimKey { LeftMouse, RightMouse, KeyE };

struct AimSettings {
    bool enabled = true;
    float max_delta_dist = 8.0f;
    float max_distance = 400.0f;
    float smoothing = 5.0f;

    AimStyle style = AimStyle::Silent;
    TargetSelection selection = TargetSelection::ClosestToCrosshair;
    AimPart aim_part = AimPart::Torso;
    std::string custom_part_name = "HumanoidRootPart";
    AimKey aim_key = AimKey::LeftMouse;
};

class GameProfile {
public:
    virtual ~GameProfile() = default;

    virtual std::string name() const = 0;
    virtual uint64_t place_id() const = 0;

    virtual bool detect(const roblox::GameContext& game) const {
        return game.place_id() == place_id();
    }

    // Override in subclass if needed
    virtual void initialize(roblox::GameContext& game) {

    }

    // Override in subclass if needed - Called every frame for game specific state updates
    virtual void update(roblox::GameContext& game) {

    }

    virtual std::vector<Target> find_targets(roblox::GameContext& game, const roblox::CFrame& camera_cf, float fov_radians) = 0;

    virtual ESPColor get_target_color(const Target& target, bool is_aim_target) const {
        if (is_aim_target) {
            return ESPColor{1.0f, 0.0f, 1.0f, 1.0f};  // Magenta for aim target
        }
        if (target.is_teammate) {
            return ESPColor{0.0f, 1.0f, 0.0f, 1.0f};  // Green for teammates
        }
        return ESPColor{1.0f, 0.0f, 0.0f, 1.0f};      // Red for enemies
    }

    virtual float get_target_border_width(const Target& target, bool is_aim_target) const {
        return is_aim_target ? 3.0f : 2.0f;
    }

    virtual AimKey default_aim_key() const { return AimKey::LeftMouse; }

    // Override in subclass if needed
    virtual void set_aim_part(const std::string& part_name) {

    }

    void set_screen_size(float width, float height) {
        m_screenWidth = width;
        m_screenHeight = height;
    }

    float screen_width() const { return m_screenWidth; }
    float screen_height() const { return m_screenHeight; }

    virtual void apply_aim(const Target& target, roblox::GameContext& game, const roblox::CFrame& camera_cf, const AimSettings& settings)
    {
        auto camera = game.camera();
        if (!camera)
            return;

        roblox::Vector3 camera_pos = camera_cf.position;
        roblox::Vector3 target_direction = (target.aim_position - camera_pos).normalized();

        roblox::Vector3 new_look;

        switch (settings.style) {
            case AimStyle::Silent:
            case AimStyle::Snap: {
                // Instant lock
                new_look = target_direction;
                break;
            }

            case AimStyle::Legit: {
                // Smooth interpolation toward target
                roblox::Vector3 current_look = camera_cf.look_vector();
                float alpha = 1.0f / settings.smoothing;
                new_look = current_look.lerp(target_direction, alpha).normalized();
                break;
            }
        }

        // Apply new look direction to camera
        roblox::CFrame new_cf = camera_cf;
        new_cf.r20 = -new_look.x;
        new_cf.r21 = -new_look.y;
        new_cf.r22 = -new_look.z;
        camera.set_cframe(new_cf);
    }

protected:
    float m_screenWidth = 1920.0f;
    float m_screenHeight = 1080.0f;

    // Calculate distance from crosshair ray
    float calculate_delta_distance(const roblox::CFrame& camera_cf, const roblox::Vector3& target_pos)
    {
        roblox::Vector3 camera_pos = camera_cf.position;
        roblox::Vector3 camera_look = camera_cf.look_vector();
        
        roblox::Vector3 to_target = target_pos - camera_pos;
        float depth = to_target.dot(camera_look);
        
        if (depth <= 0)
            return 99999.0f;  // Behind camera
        
        roblox::Vector3 crosshair_world = camera_pos + camera_look * depth;
        return crosshair_world.distance_to(target_pos);
    }

    roblox::Vector3 world_to_screen(
        const roblox::Vector3& world_pos,
        const roblox::CFrame& camera_cf,
        float fov_radians,
        float screen_width,
        float screen_height)
    {
        roblox::Vector3 camera_pos = camera_cf.position;
        roblox::Vector3 look = camera_cf.look_vector();
        roblox::Vector3 right = camera_cf.right_vector();
        roblox::Vector3 up = camera_cf.up_vector();

        roblox::Vector3 to_target = world_pos - camera_pos;

        float depth = to_target.dot(look);
        if (depth <= 0) {
            return roblox::Vector3(0, 0, -1);  // Behind camera
        }

        float right_offset = to_target.dot(right);
        float up_offset = to_target.dot(up);

        float tan_half_fov = std::tan(fov_radians * 0.5f);
        float plane_height = 2.0f * depth * tan_half_fov;
        float plane_width = plane_height * (screen_width / screen_height);

        float x = (right_offset / plane_width + 0.5f) * screen_width;
        float y = (0.5f + up_offset / plane_height) * screen_height;  // + not - (screen Y is inverted)

        return roblox::Vector3(x, y, depth);
    }
};

class GameProfileFactory {
public:
    template<typename T>
    void register_profile() {
        m_profiles.push_back(std::make_unique<T>());
    }

    GameProfile* detect_game(const roblox::GameContext& game) {
        for (auto& profile : m_profiles) {
            if (profile->detect(game)) {
                return profile.get();
            }
        }
        return nullptr;  // No profile, use generic
    }

    GameProfile* get_profile(uint64_t place_id) {
        for (auto& profile : m_profiles) {
            if (profile->place_id() == place_id) {
                return profile.get();
            }
        }
        return nullptr;
    }

    const std::vector<std::unique_ptr<GameProfile>>& get_all_profiles() const {
        return m_profiles;
    }
    
private:
    std::vector<std::unique_ptr<GameProfile>> m_profiles;
};

} // namespace games
