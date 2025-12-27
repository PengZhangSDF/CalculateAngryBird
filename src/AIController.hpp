// AI智能化操控系统 - 自动游玩并通关
#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <memory>
#include <optional>

#include "Entity.hpp"
#include "Physics.hpp"
#include <deque>

// 前向声明
class Game;

// AI瞄准目标信息
struct AITarget {
    sf::Vector2f position;           // 目标位置
    float priority{0.0f};            // 优先级（越高越优先）
    float distance{0.0f};            // 距离
    bool isExposed{true};            // 是否裸露（无保护）
    int protectionLevel{0};          // 保护等级（0=无保护，数字越大保护越多）
    Pig* pig{nullptr};               // 关联的猪猪指针（如果目标是猪）
    std::vector<Block*> nearbyBlocks; // 附近的方块（用于计算保护等级）
};

// AI瞄准计算结果
struct AIAimResult {
    sf::Vector2f dragStart;         // 拖拽起始位置（弹弓位置）
    sf::Vector2f dragEnd;            // 拖拽结束位置（瞄准位置）
    sf::Vector2f pull;               // 拉弓向量
    float skillActivationTime{-1.0f}; // 技能激活时间（-1表示不使用技能）
    bool isValid{false};             // 计算结果是否有效
};

class AIController {
public:
    AIController();
    
    // 设置游戏引用（用于访问游戏状态）
    void setGame(class Game* game);
    
    // AI模式开关
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }
    
    // 更新AI逻辑（每帧调用）
    void update(float dt, const std::vector<std::unique_ptr<Block>>& blocks,
                const std::vector<std::unique_ptr<Pig>>& pigs,
                const std::deque<std::unique_ptr<Bird>>& birds,
                const sf::Vector2f& slingshotPos);
    
    // 获取当前瞄准结果（用于视觉反馈）
    const AIAimResult& getCurrentAim() const { return currentAim_; }
    bool isAiming() const { return aiming_; }
    float getAimProgress() const { return aimProgress_; }
    
    // 获取当前目标信息（用于视觉反馈）
    const AITarget* getCurrentTarget() const { return currentTarget_ ? &currentTarget_.value() : nullptr; }
    
    // 检查是否应该发射（AI控制）
    bool shouldLaunch() const { return shouldLaunch_; }
    void resetLaunchFlag() { shouldLaunch_ = false; }
    
    // 检查是否应该激活技能（AI控制）
    bool shouldActivateSkill() const { return shouldActivateSkill_; }
    float getSkillActivationTime() const { return skillActivationTime_; }
    void resetSkillFlag() { shouldActivateSkill_ = false; }
    
    // 清除轨迹线（发射后调用）
    void clearTrajectory() { 
        aimTrajectory_.clear(); 
        currentAim_.isValid = false; 
        currentTarget_.reset();  // 清除目标红点
        state_ = AIState::WaitingForBird;  // 回到等待状态
        aiming_ = false;
    }
    
    // 渲染AI视觉反馈（瞄准线、目标标记等）
    void render(sf::RenderWindow& window) const;

private:
    // 核心AI逻辑
    void updateAI(float dt, const std::vector<std::unique_ptr<Block>>& blocks,
                  const std::vector<std::unique_ptr<Pig>>& pigs,
                  const std::deque<std::unique_ptr<Bird>>& birds,
                  const sf::Vector2f& slingshotPos);
    
    // 等待机制：检查当前鸟是否已经消失或静止
    bool isCurrentBirdReady(const std::deque<std::unique_ptr<Bird>>& birds) const;
    
    // 目标分析：分析所有猪猪，计算优先级
    std::vector<AITarget> analyzeTargets(const std::vector<std::unique_ptr<Pig>>& pigs,
                                        const std::vector<std::unique_ptr<Block>>& blocks,
                                        const sf::Vector2f& slingshotPos);
    
    // 计算保护等级：分析目标周围有多少方块保护
    int calculateProtectionLevel(const sf::Vector2f& targetPos,
                                const std::vector<std::unique_ptr<Block>>& blocks) const;
    
    // 多重遮挡检测算法：精确计算目标与发射位置之间的障碍物
    int calculateOcclusionLevel(const sf::Vector2f& fromPos,
                               const sf::Vector2f& toPos,
                               const std::vector<std::unique_ptr<Block>>& blocks) const;
    
    // 验证轨迹是否有效：平滑抛物线、无折线、x坐标范围合法
    bool validateTrajectory(const sf::Vector2f& slingshotPos,
                           const sf::Vector2f& targetPos,
                           const sf::Vector2f& initialVelocity,
                           float& closestDist) const;
    
    // 验证轨迹并计算到达目标的时间
    bool validateTrajectoryWithTime(const sf::Vector2f& slingshotPos,
                                   const sf::Vector2f& targetPos,
                                   const sf::Vector2f& initialVelocity,
                                   float& closestDist,
                                   float& timeToTarget) const;
    
    // 检查轨迹是否经过目标上方或下方，并返回最近距离（用于二分查找）
    // 返回值：-1=轨迹无效，0=经过目标下方，1=经过目标上方
    int checkTrajectoryPass(const sf::Vector2f& slingshotPos,
                           const sf::Vector2f& targetPos,
                           const sf::Vector2f& initialVelocity,
                           float& closestDist) const;
    
    // 生成轨迹线（用于可视化）
    void generateTrajectoryLine(const sf::Vector2f& slingshotPos);
    
    // 选择最佳目标（根据鸟类类型）
    std::optional<AITarget> selectBestTarget(BirdType birdType,
                                             const std::vector<AITarget>& targets,
                                             const sf::Vector2f& slingshotPos);
    
    // 红鸟策略：优先最近的裸露猪猪
    std::optional<AITarget> selectRedBirdTarget(const std::vector<AITarget>& targets,
                                                const sf::Vector2f& slingshotPos);
    
    // 黄鸟策略：优先远距离的裸露猪猪，或中等距离有保护的
    std::optional<AITarget> selectYellowBirdTarget(const std::vector<AITarget>& targets,
                                                   const sf::Vector2f& slingshotPos);
    
    // 炸弹鸟策略：优先建筑内部的多重保护猪猪群体
    std::optional<AITarget> selectBombBirdTarget(const std::vector<AITarget>& targets,
                                                const std::vector<std::unique_ptr<Block>>& blocks,
                                                const sf::Vector2f& slingshotPos);
    
    // 轨迹计算：计算从弹弓到目标的抛物线轨迹
    AIAimResult calculateTrajectory(const sf::Vector2f& slingshotPos,
                                   const sf::Vector2f& targetPos,
                                   BirdType birdType);
    
    // 物理模拟：模拟鸟的飞行轨迹
    bool simulateTrajectory(const sf::Vector2f& startPos,
                           const sf::Vector2f& initialVelocity,
                           const sf::Vector2f& targetPos,
                           float tolerance = 50.0f) const;
    
    // 计算初始速度（给定目标位置）
    sf::Vector2f calculateInitialVelocity(const sf::Vector2f& startPos,
                                         const sf::Vector2f& targetPos,
                                         BirdType birdType) const;
    
    // 黄鸟加速时机计算
    float calculateYellowBirdSkillTime(const sf::Vector2f& slingshotPos,
                                      const sf::Vector2f& targetPos,
                                      const sf::Vector2f& initialVelocity) const;
    
    // 炸弹鸟最佳爆炸位置计算
    sf::Vector2f calculateBombExplosionPosition(const sf::Vector2f& targetPos,
                                               const std::vector<std::unique_ptr<Block>>& blocks) const;
    
    // 状态变量
    bool enabled_{false};
    class Game* game_{nullptr};  // 使用前向声明
    
    // AI状态机
    enum class AIState {
        WaitingForBird,      // 等待当前鸟消失
        Analyzing,           // 分析目标
        Aiming,              // 瞄准中（模拟拉弓）
        ReadyToLaunch        // 准备发射
    };
    AIState state_{AIState::WaitingForBird};
    
    // 瞄准相关
    bool aiming_{false};
    float aimProgress_{0.0f};        // 瞄准进度（0-1）
    float aimDuration_{1.5f};        // 瞄准持续时间（秒）
    float aimTimer_{0.0f};
    AIAimResult currentAim_;
    std::optional<AITarget> currentTarget_;
    BirdType currentBirdType_{BirdType::Red};  // 保存当前鸟类类型
    
    // 发射控制
    bool shouldLaunch_{false};
    bool shouldActivateSkill_{false};
    float skillActivationTime_{-1.0f};
    
    // 等待计时器（在非const方法中使用）
    mutable float waitTimer_{0.0f};
    static constexpr float kWaitAfterLaunch = 0.5f;  // 发射后等待时间
    
    // 视觉反馈
    mutable std::vector<sf::Vertex> aimTrajectory_;  // 瞄准轨迹线（mutable用于const方法）
    sf::CircleShape targetMarker_;            // 目标标记
};

