#pragma once

#include <cmath>
#include <format>
#include <string>

namespace roblox {

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vector3() = default;
    Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vector3 operator+(const Vector3& other) const {
        return {x + other.x, y + other.y, z + other.z};
    }
    
    Vector3 operator-(const Vector3& other) const {
        return {x - other.x, y - other.y, z - other.z};
    }
    
    Vector3 operator*(float scalar) const {
        return {x * scalar, y * scalar, z * scalar};
    }
    
    Vector3 operator/(float scalar) const {
        return {x / scalar, y / scalar, z / scalar};
    }
    
    Vector3 operator-() const {
        return {-x, -y, -z};
    }

    Vector3& operator+=(const Vector3& other) {
        x += other.x; y += other.y; z += other.z;
        return *this;
    }
    
    Vector3& operator-=(const Vector3& other) {
        x -= other.x; y -= other.y; z -= other.z;
        return *this;
    }
    
    Vector3& operator*=(float scalar) {
        x *= scalar; y *= scalar; z *= scalar;
        return *this;
    }
    
    Vector3& operator/=(float scalar) {
        x /= scalar; y /= scalar; z /= scalar;
        return *this;
    }

    bool operator==(const Vector3& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    
    bool operator!=(const Vector3& other) const {
        return !(*this == other);
    }

    float magnitude() const {
        return std::sqrt(x * x + y * y + z * z);
    }
    
    float magnitude_squared() const {
        return x * x + y * y + z * z;
    }
    
    Vector3 normalized() const {
        float mag = magnitude();
        if (mag < 0.0001f)
            return {0, 0, 0};
        return *this / mag;
    }
    
    float dot(const Vector3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }
    
    Vector3 cross(const Vector3& other) const {
        return {
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        };
    }
    
    float distance_to(const Vector3& other) const {
        return (*this - other).magnitude();
    }
    
    Vector3 lerp(const Vector3& target, float alpha) const {
        return *this + (target - *this) * alpha;
    }

    std::string to_string() const {
        return std::format("({:.2f}, {:.2f}, {:.2f})", x, y, z);
    }

    static Vector3 zero() { return {0, 0, 0}; }
    static Vector3 one() { return {1, 1, 1}; }
    static Vector3 up() { return {0, 1, 0}; }
    static Vector3 down() { return {0, -1, 0}; }
    static Vector3 forward() { return {0, 0, -1}; }
    static Vector3 back() { return {0, 0, 1}; }
    static Vector3 left() { return {-1, 0, 0}; }
    static Vector3 right() { return {1, 0, 0}; }
};

inline Vector3 operator*(float scalar, const Vector3& v) {
    return v * scalar;
}

// =============================================================================
// CFrame (Coordinate Frame)
//
// Memory layout matches Roblox's CFrame exactly:
// The matrix is stored ROW-MAJOR as 3 rows of 3 floats:
//   r0,  r1,  r2   (row 0)
//   r10, r11, r12  (row 1)
//   r20, r21, r22  (row 2)
// Then position (x, y, z)
//
// In Roblox:
// - Right vector = (r0, r10, r20) - first column
// - Up vector    = (r1, r11, r21) - second column
// - Look vector  = (-r2, -r12, -r22) - NEGATIVE of third column (forward = -Z)
//
// =============================================================================
struct CFrame {
    // Stored as 3x3 matrix then position
    float r0 = 1.0f;   // 0x00
    float r10 = 0.0f;  // 0x04
    float r20 = 0.0f;  // 0x08
    float r1 = 0.0f;   // 0x0C
    float r11 = 1.0f;  // 0x10
    float r21 = 0.0f;  // 0x14
    float r2 = 0.0f;   // 0x18
    float r12 = 0.0f;  // 0x1C
    float r22 = 1.0f;  // 0x20
    Vector3 position;  // 0x24

    CFrame() = default;

    CFrame(const Vector3& pos) : position(pos) {}

    CFrame(const Vector3& pos, const Vector3& look_at) {
        position = pos;
        set_look_at(pos, look_at);
    }

    Vector3 look_vector() const {
        return {-r20, -r21, -r22};
    }

    Vector3 up_vector() const {
        return {r10, r11, r12};
    }

    Vector3 right_vector() const {
        return {r0, r1, r2};
    }

    void set_look_at(const Vector3& from, const Vector3& to) {
        Vector3 forward = (to - from).normalized();
        set_from_look_vector(forward);
    }

    void set_from_look_vector(const Vector3& look) {
        Vector3 forward = look.normalized();
        Vector3 world_up = Vector3::up();

        if (std::abs(forward.y) > 0.999f) {
            world_up = Vector3::forward();
        }

        Vector3 right = world_up.cross(forward).normalized();
        Vector3 up = forward.cross(right);

        // Set matrix based on Roblox layout
        // right = (r0, r1, r2)
        r0 = right.x;
        r1 = right.y;
        r2 = right.z;

        // up = (r10, r11, r12)
        r10 = up.x;
        r11 = up.y;
        r12 = up.z;

        // -look = (r20, r21, r22)
        r20 = -forward.x;
        r21 = -forward.y;
        r22 = -forward.z;
    }

    Vector3 point_to_world_space(const Vector3& local) const {
        // Using the matrix as 3 row vectors
        return {
            r0 * local.x + r10 * local.y + r20 * local.z + position.x,
            r1 * local.x + r11 * local.y + r21 * local.z + position.y,
            r2 * local.x + r12 * local.y + r22 * local.z + position.z
        };
    }

    Vector3 point_to_object_space(const Vector3& world) const {
        Vector3 rel = world - position;
        // Transpose multiply (inverse of orthogonal matrix)
        return {
            r0 * rel.x + r1 * rel.y + r2 * rel.z,
            r10 * rel.x + r11 * rel.y + r12 * rel.z,
            r20 * rel.x + r21 * rel.y + r22 * rel.z
        };
    }

    // Transform a direction (no translation)
    Vector3 vector_to_world_space(const Vector3& local) const {
        return {
            r0 * local.x + r10 * local.y + r20 * local.z,
            r1 * local.x + r11 * local.y + r21 * local.z,
            r2 * local.x + r12 * local.y + r22 * local.z
        };
    }

    Vector3 vector_to_object_space(const Vector3& world) const {
        return {
            r0 * world.x + r1 * world.y + r2 * world.z,
            r10 * world.x + r11 * world.y + r12 * world.z,
            r20 * world.x + r21 * world.y + r22 * world.z
        };
    }

    CFrame operator*(const CFrame& other) const {
        CFrame result;

        result.r0 = r0 * other.r0 + r10 * other.r1 + r20 * other.r2;
        result.r1 = r1 * other.r0 + r11 * other.r1 + r21 * other.r2;
        result.r2 = r2 * other.r0 + r12 * other.r1 + r22 * other.r2;

        result.r10 = r0 * other.r10 + r10 * other.r11 + r20 * other.r12;
        result.r11 = r1 * other.r10 + r11 * other.r11 + r21 * other.r12;
        result.r12 = r2 * other.r10 + r12 * other.r11 + r22 * other.r12;

        result.r20 = r0 * other.r20 + r10 * other.r21 + r20 * other.r22;
        result.r21 = r1 * other.r20 + r11 * other.r21 + r21 * other.r22;
        result.r22 = r2 * other.r20 + r12 * other.r21 + r22 * other.r22;

        result.position = point_to_world_space(other.position);

        return result;
    }

    CFrame inverse() const {
        CFrame result;

        result.r0 = r0;   result.r10 = r1;  result.r20 = r2;
        result.r1 = r10;  result.r11 = r11; result.r21 = r12;
        result.r2 = r20;  result.r12 = r21; result.r22 = r22;

        result.position = -result.vector_to_world_space(position);

        return result;
    }

    CFrame lerp(const CFrame& target, float alpha) const {
        CFrame result;

        // not proper slerp, but good enough for most cases
        result.r0 = r0 + (target.r0 - r0) * alpha;
        result.r1 = r1 + (target.r1 - r1) * alpha;
        result.r2 = r2 + (target.r2 - r2) * alpha;
        result.r10 = r10 + (target.r10 - r10) * alpha;
        result.r11 = r11 + (target.r11 - r11) * alpha;
        result.r12 = r12 + (target.r12 - r12) * alpha;
        result.r20 = r20 + (target.r20 - r20) * alpha;
        result.r21 = r21 + (target.r21 - r21) * alpha;
        result.r22 = r22 + (target.r22 - r22) * alpha;
        result.position = position.lerp(target.position, alpha);

        return result;
    }

    std::string to_string() const {
        return std::format("CFrame(pos: {}, look: {})",
                          position.to_string(),
                          look_vector().to_string());
    }

    static CFrame identity() {
        return CFrame();
    }
};

struct Color3 {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    
    Color3() = default;
    Color3(float r_, float g_, float b_) : r(r_), g(g_), b(b_) {}

    static Color3 from_rgb(int red, int green, int blue) {
        return {
            red / 255.0f,
            green / 255.0f,
            blue / 255.0f
        };
    }

    // #RRGGBB, RRGGBB, #RGB, RGB
    static Color3 from_hex(const std::string& hex) {
        std::string clean_hex = hex;

        if (!clean_hex.empty() && clean_hex[0] == '#') {
            clean_hex = clean_hex.substr(1);
        }

        if (clean_hex.length() == 3) {
            clean_hex = std::string(2, clean_hex[0]) +
                       std::string(2, clean_hex[1]) +
                       std::string(2, clean_hex[2]);
        }

        if (clean_hex.length() == 6) {
            int r = std::stoi(clean_hex.substr(0, 2), nullptr, 16);
            int g = std::stoi(clean_hex.substr(2, 2), nullptr, 16);
            int b = std::stoi(clean_hex.substr(4, 2), nullptr, 16);
            return from_rgb(r, g, b);
        }

        return {0, 0, 0};
    }

    Color3 lerp(const Color3& target, float alpha) const {
        return {
            r + (target.r - r) * alpha,
            g + (target.g - g) * alpha,
            b + (target.b - b) * alpha
        };
    }

    std::string to_hex() const {
        int red = static_cast<int>(r * 255.0f + 0.5f);
        int green = static_cast<int>(g * 255.0f + 0.5f);
        int blue = static_cast<int>(b * 255.0f + 0.5f);

        red = std::clamp(red, 0, 255);
        green = std::clamp(green, 0, 255);
        blue = std::clamp(blue, 0, 255);

        return std::format("{:02x}{:02x}{:02x}", red, green, blue);
    }

    static Color3 red() { return {1, 0, 0}; }
    static Color3 green() { return {0, 1, 0}; }
    static Color3 blue() { return {0, 0, 1}; }
    static Color3 yellow() { return {1, 1, 0}; }
    static Color3 cyan() { return {0, 1, 1}; }
    static Color3 magenta() { return {1, 0, 1}; }
    static Color3 white() { return {1, 1, 1}; }
    static Color3 black() { return {0, 0, 0}; }
    static Color3 orange() { return {1, 0.5f, 0}; }
    static Color3 purple() { return {0.5f, 0, 1}; }
};

} // namespace roblox

