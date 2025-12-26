// Global configuration constants for the Angry Birds style game.
#pragma once

#include <string>

namespace config {

// Target fixed timestep for stable physics.
constexpr float kFixedDelta = 1.0f / 60.0f;
// Pixels per meter to keep physics numbers reasonable.
constexpr float kPixelsPerMeter = 30.0f;
// Gravity acceleration in "pixels / s^2" (9.8 m/s^2 scaled by pixels-per-meter).
constexpr float kGravity = 9.8f * kPixelsPerMeter;
// Global maximum speed for dynamic bodies (pixels per second).
constexpr float kMaxBodySpeed = 800.0f;
// Air resistance acceleration for birds and pigs (m/s^2)
constexpr float kAirResistanceAccel = -0.25f;  // m/s^2

// Bird-specific speed limits
namespace bird_speed {
    // Red bird
    constexpr float kRedInitialMax = 600.0f;  // Max initial launch speed
    constexpr float kRedMaxSpeed = 800.0f;     // Max speed limit
    
    // Yellow bird
    constexpr float kYellowInitialMax = 500.0f;  // Max initial launch speed
    constexpr float kYellowMaxSpeed = 1500.0f;   // High max speed limit (can accelerate to 2x)
    
    // Bomb bird
    constexpr float kBombInitialMax = 550.0f;  // Max initial launch speed
    constexpr float kBombMaxSpeed = 800.0f;     // Max speed limit
}

// Level related paths.
// Relative to executable working directory (build/bin when using run.bat).
constexpr const char* kLevelDirectory = "levels";
// Font search order (Windows): prefer CJK fonts to avoid garbled Chinese text.
constexpr const char* kFontPathPrimary = "C:/Windows/Fonts/msyh.ttc";     // 微软雅黑 (常见中文字体)
constexpr const char* kFontPathSecondary = "C:/Windows/Fonts/simhei.ttf"; // 黑体
constexpr const char* kFontPathFallback = "C:/Windows/Fonts/arial.ttf";   // 仅作最后兜底

// Window setup.
constexpr unsigned int kWindowWidth = 1920;  // Increased to show full game world
constexpr unsigned int kWindowHeight = 1080;  // Increased for better aspect ratio
constexpr char kWindowTitle[] = "Angry Birds SFML 3 Demo";

// Slingshot mechanics.
constexpr float kMaxPullDistance = 220.0f;
constexpr float kSlingshotStiffness = 10.0f;
// Slingshot position (where birds are launched from)
constexpr float kSlingshotX = 200.0f;
constexpr float kSlingshotY = 500.0f;

inline std::string levelPath(int index) {
    return std::string(kLevelDirectory) + "/level" + std::to_string(index) + ".json";
}

}  // namespace config


