// Collision damage configuration system
#pragma once

#include <string>
#include <unordered_map>

// Material strength hierarchy (higher = stronger)
namespace MaterialStrength {
    constexpr float kGlass = 1.0f;
    constexpr float kWood = 2.0f;
    constexpr float kWoodboard = 2.5f;
    constexpr float kStoneslab = 3.5f;
    constexpr float kStone = 4.0f;
    constexpr float kPig = 1.5f;  // Pigs are weaker than most materials
}

// Damage multipliers based on material weight
namespace DamageMultiplier {
    constexpr float kGlass = 0.3f;      // Light, low damage
    constexpr float kWood = 0.6f;       // Medium weight
    constexpr float kWoodboard = 0.7f;  // Medium-heavy
    constexpr float kStoneslab = 1.2f;  // Heavy, high damage
    constexpr float kStone = 1.5f;      // Very heavy, very high damage
    constexpr float kPig = 0.5f;        // Pigs cause moderate damage
}

// Speed thresholds for damage (pixels/s)
namespace SpeedThreshold {
    constexpr float kMinDamageSpeed = 10.0f;   // Minimum speed to cause damage
    constexpr float kLowSpeed = 30.0f;         // Low speed threshold
    constexpr float kMediumSpeed = 60.0f;      // Medium speed threshold
    constexpr float kHighSpeed = 100.0f;       // High speed threshold
    constexpr float kVeryHighSpeed = 150.0f;   // Very high speed threshold
}

// Base damage values (reduced by 40%)
namespace BaseDamage {
    constexpr float kBlockToBlock = 9.0f;     // Base damage for block-to-block collision (15.0 * 0.6)
    constexpr float kBlockToPig = 12.0f;     // Base damage for block-to-pig collision (20.0 * 0.6)
    constexpr float kPigToPig = 6.0f;         // Base damage for pig-to-pig collision (10.0 * 0.6)
}

// Speed-based damage multipliers
namespace SpeedDamageMultiplier {
    constexpr float kLow = 0.5f;        // Low speed: 50% damage
    constexpr float kMedium = 1.0f;     // Medium speed: 100% damage
    constexpr float kHigh = 1.5f;       // High speed: 150% damage
    constexpr float kVeryHigh = 2.0f;   // Very high speed: 200% damage
}

// Helper function to get material strength
inline float getMaterialStrength(const std::string& materialName) {
    if (materialName == "glass") return MaterialStrength::kGlass;
    if (materialName == "wood") return MaterialStrength::kWood;
    if (materialName == "woodboard") return MaterialStrength::kWoodboard;
    if (materialName == "stoneslab") return MaterialStrength::kStoneslab;
    if (materialName == "stone") return MaterialStrength::kStone;
    return MaterialStrength::kWood;  // Default
}

// Helper function to get damage multiplier
inline float getDamageMultiplier(const std::string& materialName) {
    if (materialName == "glass") return DamageMultiplier::kGlass;
    if (materialName == "wood") return DamageMultiplier::kWood;
    if (materialName == "woodboard") return DamageMultiplier::kWoodboard;
    if (materialName == "stoneslab") return DamageMultiplier::kStoneslab;
    if (materialName == "stone") return DamageMultiplier::kStone;
    return DamageMultiplier::kWood;  // Default
}

// Helper function to get speed-based damage multiplier
inline float getSpeedDamageMultiplier(float speed) {
    if (speed < SpeedThreshold::kLowSpeed) {
        return SpeedDamageMultiplier::kLow;
    } else if (speed < SpeedThreshold::kMediumSpeed) {
        return SpeedDamageMultiplier::kMedium;
    } else if (speed < SpeedThreshold::kHighSpeed) {
        return SpeedDamageMultiplier::kHigh;
    } else {
        return SpeedDamageMultiplier::kVeryHigh;
    }
}


