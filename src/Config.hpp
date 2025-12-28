// Global configuration constants for the Angry Birds style game.
#pragma once

#include <string>

namespace config {

// ======= 物理世界基础参数 =======
// 固定物理步长（秒）。值越小，物理越精确，CPU 占用越高。
constexpr float kFixedDelta = 1.0f / 60.0f;
// 像素与 Box2D 米的换算比例（1 米 = 30 像素）。
constexpr float kPixelsPerMeter = 30.0f;
// 重力加速度，单位：像素/秒²（9.8 m/s² * 像素/米）。
constexpr float kGravity = 9.8f * kPixelsPerMeter;
// 所有非小鸟刚体的最大速度上限（像素/秒）。
constexpr float kMaxBodySpeed = 800.0f;
// 小鸟和猪猪的空气阻力加速度（m/s²，负值表示减速）。
constexpr float kAirResistanceAccel = -0.25f;  // m/s^2

// ======= 小鸟速度与技能相关参数 =======
namespace bird_speed {
    // 红鸟：初始发射最大速度 & 绝对速度上限
    constexpr float kRedInitialMax = 600.0f;
    constexpr float kRedMaxSpeed = 800.0f;
    
    // 黄鸟：初始发射最大速度 & 技能后可达到的更高速度上限
    constexpr float kYellowInitialMax = 500.0f;
    constexpr float kYellowMaxSpeed = 1500.0f;   // 可以在技能加速后达到 2 倍以上速度
    
    // 黑鸟（炸弹鸟）：初始发射最大速度 & 绝对速度上限
    constexpr float kBombInitialMax = 550.0f;
    constexpr float kBombMaxSpeed = 800.0f;
}

// ======= 关卡与资源路径配置 =======
// 关卡文件所在目录，相对于可执行文件工作目录（示例：build/bin）。
constexpr const char* kLevelDirectory = "levels";
// 字体搜索顺序（Windows）：优先中文字体，避免中文乱码。
constexpr const char* kFontPathPrimary = "C:/Windows/Fonts/msyh.ttc";     // 微软雅黑 (常见中文字体)
constexpr const char* kFontPathSecondary = "C:/Windows/Fonts/simhei.ttf"; // 黑体
constexpr const char* kFontPathFallback = "C:/Windows/Fonts/arial.ttf";   // 仅作最后兜底

// ======= 窗口与显示配置 =======
// 窗口宽高（像素），增大可以显示更多关卡内容。
constexpr unsigned int kWindowWidth = 1920;  // Increased to show full game world
constexpr unsigned int kWindowHeight = 1080;  // Increased for better aspect ratio
constexpr char kWindowTitle[] = "Angry Birds SFML 3 Demo";

// ======= 弹弓相关配置 =======
// 最大拉伸距离（像素）：控制玩家能拉多远。
constexpr float kMaxPullDistance = 220.0f;
// 弹弓"弹性系数"，越大，小鸟初始速度越高。
constexpr float kSlingshotStiffness = 10.0f;
// AI轨迹计算上限次数：限制收集的候选轨迹数量，避免计算过多
constexpr int kMaxTrajectoryCandidates = 10;
// 弹弓锚点位置（像素坐标，屏幕左上为原点）。
constexpr float kSlingshotX = 200.0f;
constexpr float kSlingshotY = 500.0f;

// ======= 碰撞伤害与血量平衡参数 =======
// 建筑物（所有方块）HP 计算系数：maxHp = material.strength * kBlockHpFactor
// 默认 0.5f -> 提升 50% 到 0.75f，让建筑更耐打。
constexpr float kBlockHpFactor = 0.75f;  // 方块血量系数（全局）

// 猪猪基础血量（会整体乘以系数提升）
// 这里的数值是“原始设计值”，真实血量 = 原始值 * kPigHpFactor
constexpr int kPigHpSmallBase   = 10;  // 小猪基础 HP
constexpr int kPigHpMediumBase  = 30;  // 中猪基础 HP
constexpr int kPigHpLargeBase   = 50;  // 大猪基础 HP
// 猪猪血量系数：1.3 表示在基础值上整体提升 30%
constexpr float kPigHpFactor = 1.3f;   // 猪猪血量提升系数

// ======= 碰撞伤害系统（DamageConfig 原内容迁移）=======
// 说明：所有单位仍然是“像素 / 秒”、“像素”为主，方便与游戏世界坐标对应。

// 材质强度层级（数值越大越硬，仅用于比较与推导血量）
namespace damage_material_strength {
    constexpr float kGlass    = 1.0f;  // 玻璃：最脆
    constexpr float kWood     = 2.0f;  // 木头：中等
    constexpr float kWoodboard= 2.5f;  // 木板：略硬
    constexpr float kStoneslab= 3.5f;  // 石板：较硬
    constexpr float kStone    = 4.0f;  // 石头：最硬
    constexpr float kPig      = 2.0f;  // 猪猪：比玻璃硬，但比大部分建筑软
}

// 不同材质在碰撞中造成的基础伤害倍率（越重越高）
namespace damage_multiplier {
    constexpr float kGlass    = 0.3f;  // 玻璃：轻，碰撞伤害低
    constexpr float kWood     = 0.6f;  // 木头：中等重量
    constexpr float kWoodboard= 0.7f;  // 木板：中偏重
    constexpr float kStoneslab= 1.2f;  // 石板：重，伤害高
    constexpr float kStone    = 1.5f;  // 石头：很重，伤害最高
    constexpr float kPig      = 0.5f;  // 猪猪：中等伤害
}

// 速度阈值（像素/秒），用于分档控制伤害强度
namespace damage_speed_threshold {
    constexpr float kMinDamageSpeed = 10.0f;   // 低于此速度完全不造成伤害
    constexpr float kLowSpeed       = 30.0f;   // 低速档
    constexpr float kMediumSpeed    = 60.0f;   // 中速档
    constexpr float kHighSpeed      = 100.0f;  // 高速档
    constexpr float kVeryHighSpeed  = 150.0f;  // 极高速档
}

// 基础伤害数值（已经在原配置中整体降低过一档）
namespace damage_base {
    constexpr float kBlockToBlock = 9.0f;   // 方块互撞基础伤害
    constexpr float kBlockToPig   = 9.0f;  // 方块撞猪基础伤害
    constexpr float kPigToPig     = 3.0f;   // 猪猪互撞基础伤害
}

// 速度分档对应的伤害倍率
namespace damage_speed_multiplier {
    constexpr float kLow      = 0.3f;  // 低速：30% 伤害
    constexpr float kMedium   = 0.5f;  // 中速：50% 伤害
    constexpr float kHigh     = 1.0f;  // 高速：100% 伤害
    constexpr float kVeryHigh = 1.5f;  // 极高速：150% 伤害
}

// 工具函数：根据材质名称获取强度（用于比较谁更硬）
inline float getMaterialStrength(const std::string& materialName) {
    using namespace damage_material_strength;
    if (materialName == "glass")     return kGlass;
    if (materialName == "wood")      return kWood;
    if (materialName == "woodboard") return kWoodboard;
    if (materialName == "stoneslab") return kStoneslab;
    if (materialName == "stone")     return kStone;
    return kWood;  // 默认当作木头
}

// 工具函数：根据材质名称获取伤害倍率
inline float getDamageMultiplier(const std::string& materialName) {
    using namespace damage_multiplier;
    if (materialName == "glass")     return kGlass;
    if (materialName == "wood")      return kWood;
    if (materialName == "woodboard") return kWoodboard;
    if (materialName == "stoneslab") return kStoneslab;
    if (materialName == "stone")     return kStone;
    return kWood;  // 默认当作木头
}

// 工具函数：根据碰撞速度获取速度伤害倍率
inline float getSpeedDamageMultiplier(float speed) {
    using namespace damage_speed_threshold;
    using namespace damage_speed_multiplier;
    if (speed < kLowSpeed)        return kLow;
    else if (speed < kMediumSpeed)return kMedium;
    else if (speed < kHighSpeed)  return kHigh;
    else                          return kVeryHigh;
}

inline std::string levelPath(int index) {
    return std::string(kLevelDirectory) + "/level" + std::to_string(index) + ".json";
}

}  // namespace config


