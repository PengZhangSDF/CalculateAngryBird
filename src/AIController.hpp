// AI自动操控发射鸟以完成关卡的功能模块
// 包含差异化目标选择、轨迹精确计算、可视化等子系统
#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <memory>
#include <deque>
#include <chrono>
#include <cmath>
#include <algorithm>

#include "Entity.hpp"
#include "Physics.hpp"
#include "Config.hpp"
#include "Logger.hpp"

// 前向声明
class Game;

// ========== 数据结构定义 ==========

// 目标信息结构
struct TargetInfo {
    enum Type { Pig, Block };
    Type type;
    sf::Vector2f position;
    sf::Vector2f size;  // 对于圆形目标（猪），size.x 为半径
    int health{0};
    int maxHealth{0};
    float threatValue{0.0f};  // 威胁值（用于优先级排序）
    float attackValue{0.0f};  // 攻击价值（用于目标选择）
    
    // 障碍物层级信息（用于炸弹鸟）
    int obstacleLayerCount{0};  // 覆盖此目标的障碍物层数
    std::vector<size_t> blockingBlocks;  // 阻挡此目标的方块索引
    
    // 材质信息（用于方块）
    std::string materialName{"wood"};
    float materialStrength{240.0f};
    
    // 猪类型（用于猪）
    PigType pigType{PigType::Small};
    
    // 物理体指针（用于碰撞检测）
    void* entityPtr{nullptr};
};

// 瞄准信息结构
struct AimingInfo {
    bool isValid{false};
    sf::Vector2f dragStart;      // 拖拽起点（弹弓位置）
    sf::Vector2f dragEnd;        // 拖拽终点（AI计算的拖拽位置）
    float angle{0.0f};           // 发射角度（0-90度，精度0.1度）
    float power{0.0f};           // 发射力度（0-100%，精度1%）
    float skillActivationTime{0.0f};  // 技能释放时机（时间戳，精度10ms）
    TargetInfo target;           // 目标信息
    float trajectoryError{0.0f}; // 轨迹预测误差（%）
    
    // 轨迹点（用于可视化）
    std::vector<sf::Vector2f> trajectoryPoints;
    sf::Vector2f predictedHitPoint;  // 预测碰撞点
};

// 轨迹计算结果
struct TrajectoryResult {
    std::vector<sf::Vector2f> points;  // 轨迹点序列
    bool hitTarget{false};              // 是否击中目标
    float hitTime{0.0f};                // 击中时间
    sf::Vector2f hitPoint;              // 击中点
    float minDistanceToTarget{999999.0f};  // 与目标的最小距离
    float finalVelocity{0.0f};          // 击中时的速度
};

// ========== AI控制器主类 ==========

class AIController {
public:
    AIController();
    ~AIController();
    
    // 基础接口
    void setGame(Game* game) { game_ = game; }
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
    // 更新接口
    void update(float dt, 
                const std::vector<std::unique_ptr<Block>>& blocks,
                const std::vector<std::unique_ptr<Pig>>& pigs,
                const std::deque<std::unique_ptr<Bird>>& birds,
                const sf::Vector2f& slingshotPos);
    
    // 发射控制接口
    bool shouldLaunch() const { return shouldLaunch_; }
    void resetLaunchFlag() { shouldLaunch_ = false; }
    const AimingInfo& getCurrentAim() const { return currentAim_; }
    
    // 技能激活接口
    bool shouldActivateSkill() const { return shouldActivateSkill_; }
    void resetSkillFlag() { shouldActivateSkill_ = false; }
    
    // 轨迹可视化接口
    void clearTrajectory() { 
        trajectoryPreview_.clear();
        trajectoryPreviewTimer_ = 0.0f;
        trajectoryPreviewReady_ = false;
    }
    const std::vector<sf::Vertex>& getTrajectoryPreview() const { return trajectoryPreview_; }
    void updateTrajectoryPreview();  // 更新轨迹预览
    
    // 性能统计
    struct PerformanceStats {
        int trajectoryCalculations{0};
        int targetIdentifications{0};
        float avgTrajectoryTimeMs{0.0f};
        float maxTrajectoryTimeMs{0.0f};
        int successfulHits{0};
        int totalShots{0};
        float successRate{0.0f};
    };
    const PerformanceStats& getStats() const { return stats_; }

private:
    // ========== 子系统1: 关卡布局分析 ==========
    void analyzeLevelLayout(const std::vector<std::unique_ptr<Block>>& blocks,
                           const std::vector<std::unique_ptr<Pig>>& pigs);
    
    // ========== 子系统2: 差异化目标选择 ==========
    TargetInfo selectTargetForBombBird(const std::vector<TargetInfo>& targets,
                                       const std::vector<TargetInfo>& blocks);
    TargetInfo selectTargetForRedBird(const std::vector<TargetInfo>& targets);
    TargetInfo selectTargetForYellowBird(const std::vector<TargetInfo>& targets);
    
    // 炸弹鸟专用：障碍物层级分析
    void calculateObstacleLayers(const std::vector<TargetInfo>& allTargets,
                                 const std::vector<TargetInfo>& blocks);
    int countObstacleLayers(const TargetInfo& target, 
                           const std::vector<TargetInfo>& blocks,
                           const sf::Vector2f& slingshotPos);
    float evaluateTargetValueForBomb(const TargetInfo& target, 
                                     const std::vector<TargetInfo>& blocks);
    
    // 红鸟专用：目标优先级计算
    float calculateThreatLevel(const TargetInfo& target);
    float calculateAttackValue(const TargetInfo& target, 
                              const sf::Vector2f& fromPos);
    
    // ========== 子系统3: 轨迹计算引擎 ==========
    TrajectoryResult calculateTrajectory(const sf::Vector2f& startPos,
                                        const sf::Vector2f& velocity,
                                        BirdType birdType,
                                        bool useSkill,
                                        const TargetInfo& target,
                                        float maxTime = 5.0f);
    
    TrajectoryResult calculateYellowBirdTrajectory(const sf::Vector2f& startPos,
                                                   const sf::Vector2f& initialVelocity,
                                                   float skillActivationTime,
                                                   const TargetInfo& target,
                                                   float maxTime = 5.0f);
    
    // 物理计算辅助函数
    std::pair<sf::Vector2f, sf::Vector2f> applyPhysicsStep(sf::Vector2f pos, sf::Vector2f vel,
                                                          float dt, float maxSpeed);
    float calculateAirResistance(float speed);
    
    // ========== 子系统4: 发射参数计算 ==========
    AimingInfo calculateOptimalAim(BirdType birdType,
                                   const TargetInfo& target,
                                   const sf::Vector2f& slingshotPos);
    
    // 发射参数优化（迭代优化角度和力度）
    AimingInfo optimizeLaunchParameters(BirdType birdType,
                                       const TargetInfo& target,
                                       const sf::Vector2f& slingshotPos);
    
    // 从角度和力度计算速度向量
    sf::Vector2f velocityFromAngleAndPower(float angle, float power, BirdType birdType);
    
    // ========== 子系统5: 发射顺序决策 ==========
    std::vector<BirdType> determineLaunchOrder(const std::deque<std::unique_ptr<Bird>>& birds);
    
    // ========== 辅助函数 ==========
    bool isPointInBounds(const sf::Vector2f& point) const;
    float distance(const sf::Vector2f& a, const sf::Vector2f& b) const;
    float length(const sf::Vector2f& v) const;
    sf::Vector2f normalize(const sf::Vector2f& v) const;
    
    // 碰撞检测辅助
    bool raycastToTarget(const sf::Vector2f& start, const sf::Vector2f& end,
                        const std::vector<TargetInfo>& obstacles,
                        sf::Vector2f& hitPoint) const;
    
    // ========== 成员变量 ==========
    Game* game_{nullptr};
    bool enabled_{false};
    
    // 当前状态
    bool shouldLaunch_{false};
    bool shouldActivateSkill_{false};
    AimingInfo currentAim_;
    
    // 关卡分析结果
    std::vector<TargetInfo> allTargets_;     // 所有目标（猪+方块）
    std::vector<TargetInfo> pigTargets_;     // 仅猪目标
    std::vector<TargetInfo> blockTargets_;   // 仅方块
    sf::Vector2f slingshotPos_;
    
    // 轨迹可视化
    std::vector<sf::Vertex> trajectoryPreview_;
    
    // 性能统计
    PerformanceStats stats_;
    std::chrono::high_resolution_clock::time_point trajectoryCalcStart_;
    
    // 状态管理
    float timeSinceLastAnalysis_{0.0f};
    static constexpr float kAnalysisInterval = 0.1f;  // 每0.1秒分析一次关卡布局
    float launchCooldown_{0.0f};
    static constexpr float kLaunchCooldownTime = 0.5f;  // 发射后冷却时间
    bool waitingForBirdsLogged_{false};  // 记录是否已记录等待日志
    
    // 鸟消失后的等待计时器
    float birdDisappearWaitTimer_{0.0f};  // 鸟消失后等待计时器
    static constexpr float kBirdDisappearWaitTime = 1.0f;  // 鸟消失后等待1秒
    bool lastBirdWasActive_{false};  // 上一帧是否有活动的鸟
    
    // 轨迹预览延迟
    float trajectoryPreviewTimer_{0.0f};  // 轨迹预览计时器
    static constexpr float kTrajectoryPreviewDuration = 1.0f;  // 发射前预览1秒
    bool trajectoryPreviewReady_{false};  // 轨迹预览是否准备好
};

