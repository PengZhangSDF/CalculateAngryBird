// AI控制器实现：包含所有子系统的完整实现
#include "AIController.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <chrono>
#include <tuple>
#include <SFML/Graphics.hpp>

#include "Game.hpp"
#include "Material.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ========== 构造函数和析构函数 ==========

AIController::AIController() {
    Logger::getInstance().info("AI控制器初始化");
    stats_ = PerformanceStats();
}

AIController::~AIController() {
    Logger::getInstance().info("AI控制器销毁");
}

// ========== 主更新函数 ==========

void AIController::update(float dt,
                         const std::vector<std::unique_ptr<Block>>& blocks,
                         const std::vector<std::unique_ptr<Pig>>& pigs,
                         const std::deque<std::unique_ptr<Bird>>& birds,
                         const sf::Vector2f& slingshotPos) {
    if (!enabled_) return;
    
    slingshotPos_ = slingshotPos;
    
    // 更新冷却时间
    if (launchCooldown_ > 0.0f) {
        launchCooldown_ -= dt;
        return;  // 冷却期间不执行任何逻辑
    }
    
    // 更新轨迹预览计时器
    if (trajectoryPreviewTimer_ > 0.0f && !trajectoryPreviewReady_) {
        trajectoryPreviewTimer_ += dt;
        if (trajectoryPreviewTimer_ >= kTrajectoryPreviewDuration) {
            trajectoryPreviewReady_ = true;
            trajectoryPreviewTimer_ = 0.0f;
            Logger::getInstance().info("轨迹预览完成，准备发射");
            // 立即设置发射标志，避免重新计算
            if (currentAim_.isValid) {
                shouldLaunch_ = true;
                // 判断鸟类型并设置技能激活标志
                BirdType nextBirdType = BirdType::Red;
                for (const auto& bird : birds) {
                    if (bird && !bird->isLaunched()) {
                        nextBirdType = bird->type();
                        break;
                    }
                }
                if (nextBirdType == BirdType::Yellow) {
                    shouldActivateSkill_ = true;
                }
                Logger::getInstance().info("AI准备发射: 角度=" + std::to_string(currentAim_.angle) + 
                                          "°, 力度=" + std::to_string(currentAim_.power) + 
                                          "%, 误差=" + std::to_string(currentAim_.trajectoryError) + "%");
                launchCooldown_ = kLaunchCooldownTime;
            }
        }
    }
    
    // 定期分析关卡布局
    timeSinceLastAnalysis_ += dt;
    if (timeSinceLastAnalysis_ >= kAnalysisInterval) {
        analyzeLevelLayout(blocks, pigs);
        timeSinceLastAnalysis_ = 0.0f;
    }
    
    // 检查是否还有已发射但未消失的鸟（需要等待它们消失）
    bool hasActiveLaunchedBird = false;
    int activeBirdsCount = 0;
    for (const auto& bird : birds) {
        if (bird && bird->isLaunched() && !bird->isDestroyed()) {
            auto* body = bird->body();
            if (body && body->active()) {
                hasActiveLaunchedBird = true;
                activeBirdsCount++;
            }
        }
    }
    
    // 如果有已发射的鸟还在飞行，等待它们消失
    if (hasActiveLaunchedBird) {
        lastBirdWasActive_ = true;
        birdDisappearWaitTimer_ = 0.0f;  // 重置等待计时器
        shouldLaunch_ = false;  // 取消待发射指令（如果有）
        // 清除轨迹预览，避免显示过时的轨迹
        if (!trajectoryPreview_.empty()) {
            trajectoryPreview_.clear();
        }
        // 不记录日志（避免日志过多），只在第一次检测到时记录
        if (!waitingForBirdsLogged_ && activeBirdsCount > 0) {
            Logger::getInstance().info("AI等待: 还有 " + std::to_string(activeBirdsCount) + " 只鸟在空中飞行");
            waitingForBirdsLogged_ = true;
        }
        return;
    } else {
        // 当所有鸟都消失后，检查是否需要等待1秒
        if (lastBirdWasActive_) {
            // 刚刚所有鸟都消失了，开始计时
            lastBirdWasActive_ = false;
            birdDisappearWaitTimer_ = 0.0f;
            Logger::getInstance().info("AI等待: 所有鸟已消失，等待1秒后准备发射下一只");
        }
        
        // 如果正在等待中，继续等待
        if (birdDisappearWaitTimer_ < kBirdDisappearWaitTime) {
            birdDisappearWaitTimer_ += dt;
            shouldLaunch_ = false;  // 取消待发射指令（如果有）
            // 清除轨迹预览，避免显示过时的轨迹
            if (!trajectoryPreview_.empty()) {
                trajectoryPreview_.clear();
            }
            return;  // 等待时间未到，不执行后续逻辑
        }
        
        // 等待时间已到，可以准备发射下一只
        if (waitingForBirdsLogged_) {
            Logger::getInstance().info("AI等待结束: 等待时间已过，准备发射下一只");
            waitingForBirdsLogged_ = false;
        }
    }
    
    // 如果没有未发射的鸟，不执行AI逻辑
    bool hasUnlaunchedBird = false;
    BirdType nextBirdType = BirdType::Red;
    for (const auto& bird : birds) {
        if (bird && !bird->isLaunched()) {
            hasUnlaunchedBird = true;
            nextBirdType = bird->type();
            break;
        }
    }
    
    if (!hasUnlaunchedBird) {
        return;
    }
    
    // 如果轨迹预览正在计时中，持续更新预览显示，但不重新计算瞄准
    if (trajectoryPreviewTimer_ > 0.0f && !trajectoryPreviewReady_) {
        // 更新轨迹预览显示（持续刷新）
        updateTrajectoryPreview();
        return;  // 等待轨迹预览计时完成
    }
    
    // 如果轨迹预览已完成且已设置发射标志，等待Game处理发射
    if (trajectoryPreviewReady_ && shouldLaunch_) {
        // 重置轨迹预览状态（但保持shouldLaunch_，等待Game处理）
        trajectoryPreviewTimer_ = 0.0f;
        trajectoryPreviewReady_ = false;
        return;  // 已设置发射标志，等待Game处理
    }
    
    // 如果有未发射的鸟且没有待发射指令，计算新的瞄准
    if (!shouldLaunch_ && !pigTargets_.empty()) {
        // 选择目标
        TargetInfo selectedTarget;
        switch (nextBirdType) {
            case BirdType::Bomb:
                selectedTarget = selectTargetForBombBird(pigTargets_, blockTargets_);
                break;
            case BirdType::Yellow:
                selectedTarget = selectTargetForYellowBird(pigTargets_);
                break;
            case BirdType::Red:
            default:
                selectedTarget = selectTargetForRedBird(pigTargets_);
                break;
        }
        
        // 如果找到了有效目标，计算最佳瞄准
        if (selectedTarget.entityPtr != nullptr) {
            Logger::getInstance().info("AI开始计算瞄准: 鸟类型=" + std::to_string(static_cast<int>(nextBirdType)) + 
                                      ", 目标位置=(" + std::to_string(selectedTarget.position.x) + 
                                      ", " + std::to_string(selectedTarget.position.y) + ")");
            
            currentAim_ = calculateOptimalAim(nextBirdType, selectedTarget, slingshotPos_);
            
            // 如果瞄准有效，检查误差
            if (currentAim_.isValid) {
                Logger::getInstance().info("AI瞄准计算完成: 角度=" + std::to_string(currentAim_.angle) + 
                                          "°, 力度=" + std::to_string(currentAim_.power) + 
                                          "%, 误差=" + std::to_string(currentAim_.trajectoryError) + "%");
                
                // 对于不同鸟类，使用不同的误差阈值
                // 黄鸟：轨迹计算复杂，放宽到5%
                // 炸弹鸟：远距离目标，放宽到8%（允许更大的误差）
                // 红鸟：标准3%
                float errorThreshold = 3.0f;
                if (nextBirdType == BirdType::Yellow) {
                    errorThreshold = 5.0f;
                } else if (nextBirdType == BirdType::Bomb) {
                    errorThreshold = 8.0f;  // 炸弹鸟远距离目标，允许更大误差
                }
                
                // 如果瞄准有效且误差在可接受范围内，准备发射
                if (currentAim_.trajectoryError < errorThreshold) {
                    // 如果这是第一次计算（计时器未启动），启动轨迹预览计时器
                    if (trajectoryPreviewTimer_ == 0.0f) {
                        trajectoryPreviewTimer_ = 0.001f;  // 启动计时器（很小的初始值）
                        trajectoryPreviewReady_ = false;
                        Logger::getInstance().info("轨迹预览开始，等待1秒...");
                    }
                    
                    // 更新轨迹预览显示
                    updateTrajectoryPreview();
                    
                    // 轨迹预览中，不发射（等待计时器完成）
                    shouldLaunch_ = false;
                } else {
                    Logger::getInstance().info("AI瞄准误差过大: " + std::to_string(currentAim_.trajectoryError) + 
                                              "% (阈值: " + std::to_string(errorThreshold) + "%)");
                }
            } else {
                Logger::getInstance().info("AI未能找到有效瞄准方案");
            }
        } else {
            Logger::getInstance().info("AI未找到有效目标");
        }
    }
}

// ========== 子系统1: 关卡布局分析 ==========

void AIController::analyzeLevelLayout(const std::vector<std::unique_ptr<Block>>& blocks,
                                     const std::vector<std::unique_ptr<Pig>>& pigs) {
    stats_.targetIdentifications++;
    
    allTargets_.clear();
    pigTargets_.clear();
    blockTargets_.clear();
    
    // 分析所有方块
    for (const auto& block : blocks) {
        if (!block || block->isDestroyed()) continue;
        
        auto* body = block->body();
        if (!body || !body->active()) continue;
        
        TargetInfo info;
        info.type = TargetInfo::Block;
        info.position = body->position();
        info.size = sf::Vector2f(50.0f, 20.0f);  // 默认大小，可以从Entity获取
        info.health = block->health();
        info.maxHealth = block->maxHealth();
        info.materialName = block->material().name;
        info.materialStrength = block->material().strength;
        info.entityPtr = block.get();
        info.threatValue = calculateThreatLevel(info);
        
        allTargets_.push_back(info);
        blockTargets_.push_back(info);
    }
    
    // 分析所有猪
    for (const auto& pig : pigs) {
        if (!pig || pig->isDestroyed()) continue;
        
        auto* body = pig->body();
        if (!body || !body->active()) continue;
        
        TargetInfo info;
        info.type = TargetInfo::Pig;
        info.position = body->position();
        float radius = 15.0f;  // 猪的默认半径
        info.size = sf::Vector2f(radius, radius);
        info.health = pig->health();
        info.maxHealth = pig->maxHealth();
        info.pigType = pig->type();
        info.entityPtr = pig.get();
        info.threatValue = calculateThreatLevel(info);
        info.attackValue = calculateAttackValue(info, slingshotPos_);
        
        allTargets_.push_back(info);
        pigTargets_.push_back(info);
    }
    
    // 计算障碍物层级（用于炸弹鸟）
    calculateObstacleLayers(allTargets_, blockTargets_);
}

// ========== 子系统2: 差异化目标选择 ==========

TargetInfo AIController::selectTargetForBombBird(const std::vector<TargetInfo>& targets,
                                                 const std::vector<TargetInfo>& blocks) {
    if (targets.empty()) {
        TargetInfo invalid;
        return invalid;
    }
    
    TargetInfo bestTarget;
    float bestValue = -1.0f;
    
    // 炸弹鸟只选择猪（targets参数应该只包含猪），不选择方块
    // 优先选择被多层障碍物保护的猪
    // 如果所有目标都没有保护，则选择最远或威胁值最高的目标
    bool hasProtectedTarget = false;
    for (const auto& target : targets) {
        // 确保只处理猪目标（type == Pig）
        if (target.type != TargetInfo::Pig) {
            continue;  // 跳过非猪目标
        }
        if (target.obstacleLayerCount > 0) {
            hasProtectedTarget = true;
            break;
        }
    }
    
    for (const auto& target : targets) {
        // 炸弹鸟只选择猪，不选择方块
        if (target.type != TargetInfo::Pig) {
            continue;  // 跳过方块目标
        }
        
        float value = 0.0f;
        
        if (hasProtectedTarget) {
            // 如果有被保护的目标，优先选择保护层数多的
            value = evaluateTargetValueForBomb(target, blocks);
        } else {
            // 如果没有被保护的目标，选择最远或威胁值最高的
            float dist = distance(target.position, slingshotPos_);
            float threatValue = target.threatValue;
            
            // 价值 = 距离因子 + 威胁值因子
            float distanceFactor = dist * 0.1f;  // 距离越远越好
            float threatFactor = threatValue * 10.0f;
            value = distanceFactor + threatFactor;
        }
        
        if (value > bestValue) {
            bestValue = value;
            bestTarget = target;
        }
    }
    
    return bestTarget;
}

TargetInfo AIController::selectTargetForRedBird(const std::vector<TargetInfo>& targets) {
    if (targets.empty()) {
        TargetInfo invalid;
        return invalid;
    }
    
    TargetInfo bestTarget;
    float bestValue = -1.0f;
    
    // 红鸟策略：优先选择较近距离的裸露/较少保护的猪猪
    for (const auto& target : targets) {
        float dist = distance(target.position, slingshotPos_);
        
        // 障碍物层数（越少越好，裸露的目标优先）
        int obstacleLayers = target.obstacleLayerCount;
        
        // 计算价值：距离越近越好，障碍物层数越少越好
        // 距离因子：1 / (1 + dist/100)，距离越近值越大
        float distanceFactor = 100.0f / (1.0f + dist * 0.01f);
        
        // 障碍物因子：障碍物越少越好（0层 = 100分，每增加1层减20分）
        float obstacleFactor = std::max(0.0f, 100.0f - static_cast<float>(obstacleLayers) * 20.0f);
        
        // 综合价值 = 距离因子 * 障碍物因子
        float value = distanceFactor * obstacleFactor;
        
        // 额外奖励：完全裸露的目标（0层障碍物）且距离适中
        if (obstacleLayers == 0 && dist < 800.0f) {
            value *= 1.5f;  // 额外50%奖励
        }
        
        if (value > bestValue) {
            bestValue = value;
            bestTarget = target;
        }
    }
    
    return bestTarget;
}

TargetInfo AIController::selectTargetForYellowBird(const std::vector<TargetInfo>& targets) {
    // 黄鸟策略：优先选择远距离的裸露目标，或者中远距离有少量保护的目标
    if (targets.empty()) {
        TargetInfo invalid;
        return invalid;
    }
    
    TargetInfo bestTarget;
    float bestValue = -1.0f;
    
    for (const auto& target : targets) {
        float dist = distance(target.position, slingshotPos_);
        int obstacleLayers = target.obstacleLayerCount;
        
        // 价值计算：优先远距离、裸露或少量保护的目标
        float distanceScore = 0.0f;
        float obstacleScore = 0.0f;
        
        // 距离评分：远距离目标得分更高
        // 中远距离（600-1200像素）得分最高
        if (dist >= 600.0f && dist <= 1200.0f) {
            distanceScore = 100.0f;  // 中远距离满分
        } else if (dist > 1200.0f) {
            distanceScore = 80.0f + (dist - 1200.0f) * 0.05f;  // 超远距离也有较高得分
        } else if (dist >= 400.0f && dist < 600.0f) {
            distanceScore = 60.0f;  // 中距离
        } else {
            distanceScore = 40.0f;  // 近距离
        }
        
        // 障碍物评分：裸露或少量保护的目标得分高
        // 0层（裸露）= 100分，1层 = 70分，2层 = 40分，3层以上 = 20分
        if (obstacleLayers == 0) {
            obstacleScore = 100.0f;  // 完全裸露，最高分
        } else if (obstacleLayers == 1) {
            obstacleScore = 70.0f;   // 少量保护，仍然很好
        } else if (obstacleLayers == 2) {
            obstacleScore = 40.0f;   // 中等保护
        } else {
            obstacleScore = 20.0f;   // 多层保护，不优先
        }
        
        // 综合价值：距离得分 * 障碍物得分（归一化到0-1范围）
        float value = (distanceScore / 100.0f) * (obstacleScore / 100.0f) * 100.0f;
        
        // 额外奖励：远距离且裸露的目标
        if (obstacleLayers == 0 && dist >= 800.0f) {
            value *= 1.5f;  // 额外50%奖励
        }
        
        if (value > bestValue) {
            bestValue = value;
            bestTarget = target;
        }
    }
    
    return bestTarget;
}

void AIController::calculateObstacleLayers(const std::vector<TargetInfo>& allTargets,
                                           const std::vector<TargetInfo>& blocks) {
    // 为每个目标计算障碍物层级数
    for (auto& target : const_cast<std::vector<TargetInfo>&>(allTargets)) {
        if (target.type == TargetInfo::Pig) {
            target.obstacleLayerCount = countObstacleLayers(target, blocks, slingshotPos_);
        }
    }
    
    // 同步到pigTargets_
    for (auto& pig : pigTargets_) {
        for (const auto& target : allTargets) {
            if (target.entityPtr == pig.entityPtr) {
                pig.obstacleLayerCount = target.obstacleLayerCount;
                pig.blockingBlocks = target.blockingBlocks;
                break;
            }
        }
    }
}

int AIController::countObstacleLayers(const TargetInfo& target,
                                     const std::vector<TargetInfo>& blocks,
                                     const sf::Vector2f& slingshotPos) {
    // 使用射线检测计算从弹弓到目标的障碍物层数
    sf::Vector2f direction = normalize(target.position - slingshotPos);
    float dist = distance(target.position, slingshotPos);
    
    std::vector<std::pair<float, size_t>> intersections;  // (distance, blockIndex)
    
    // 检查每个方块是否与射线相交
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto& block = blocks[i];
        sf::Vector2f blockCenter = block.position;
        sf::Vector2f blockSize = block.size;
        
        // 简单的AABB射线相交检测
        sf::Vector2f min = blockCenter - blockSize * 0.5f;
        sf::Vector2f max = blockCenter + blockSize * 0.5f;
        
        // 简化的射线-AABB相交（这里使用中心点距离作为近似）
        sf::Vector2f toBlock = blockCenter - slingshotPos;
        float projDist = std::abs(toBlock.x * direction.x + toBlock.y * direction.y);
        
        if (projDist < dist && length(toBlock) < dist) {
            // 检查是否在方块的包围盒内（简化检测）
            sf::Vector2f projPoint = slingshotPos + direction * projDist;
            if (projPoint.x >= min.x && projPoint.x <= max.x &&
                projPoint.y >= min.y && projPoint.y <= max.y) {
                intersections.push_back({projDist, i});
            }
        }
    }
    
    // 按距离排序
    std::sort(intersections.begin(), intersections.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    
    // 返回层数（最多计算到目标的距离）
    int layerCount = 0;
    float currentDist = 0.0f;
    for (const auto& intersection : intersections) {
        if (intersection.first > currentDist && intersection.first < dist) {
            layerCount++;
            currentDist = intersection.first;
        }
    }
    
    return layerCount;
}

float AIController::evaluateTargetValueForBomb(const TargetInfo& target,
                                               const std::vector<TargetInfo>& blocks) {
    // 炸弹鸟目标价值 = 障碍物层数 * 系数 + 目标价值
    float layerBonus = static_cast<float>(target.obstacleLayerCount) * 50.0f;
    float healthBonus = static_cast<float>(target.health) * 0.1f;
    float distancePenalty = distance(target.position, slingshotPos_) * 0.01f;
    
    return layerBonus + healthBonus - distancePenalty;
}

float AIController::calculateThreatLevel(const TargetInfo& target) {
    // 威胁值基于血量、类型和位置
    float baseThreat = static_cast<float>(target.health);
    
    if (target.type == TargetInfo::Pig) {
        switch (target.pigType) {
            case PigType::Small: baseThreat *= 1.0f; break;
            case PigType::Medium: baseThreat *= 1.5f; break;
            case PigType::Large: baseThreat *= 2.0f; break;
        }
    } else {
        // 方块威胁值基于材质强度
        baseThreat = target.materialStrength * 0.1f;
    }
    
    return baseThreat;
}

float AIController::calculateAttackValue(const TargetInfo& target,
                                        const sf::Vector2f& fromPos) {
    float dist = distance(target.position, fromPos);
    float healthValue = static_cast<float>(target.health);
    
    // 距离越近，攻击价值越高；血量越高，价值越高
    return healthValue / (1.0f + dist * 0.01f);
}

// ========== 子系统3: 轨迹计算引擎 ==========

TrajectoryResult AIController::calculateTrajectory(const sf::Vector2f& startPos,
                                                   const sf::Vector2f& velocity,
                                                   BirdType birdType,
                                                   bool useSkill,
                                                   const TargetInfo& target,
                                                   float maxTime) {
    auto startTime = std::chrono::high_resolution_clock::now();
    stats_.trajectoryCalculations++;
    
    TrajectoryResult result;
    
    // 根据鸟类型选择计算方法
    if (birdType == BirdType::Yellow && useSkill) {
        // 黄鸟使用特殊轨迹计算
        float skillTime = 0.2f;  // 默认技能激活时间
        result = calculateYellowBirdTrajectory(startPos, velocity, skillTime, target, maxTime);
    } else {
        // 标准轨迹计算
        sf::Vector2f pos = startPos;
        sf::Vector2f vel = velocity;
        
        // 获取鸟的最大速度
        float maxSpeed = config::kMaxBodySpeed;
        switch (birdType) {
            case BirdType::Red:
                maxSpeed = config::bird_speed::kRedMaxSpeed;
                break;
            case BirdType::Yellow:
                maxSpeed = useSkill ? config::bird_speed::kYellowMaxSpeed : config::bird_speed::kYellowInitialMax;
                break;
            case BirdType::Bomb:
                maxSpeed = config::bird_speed::kBombMaxSpeed;
                break;
        }
        
        const float dt = 0.02f;  // 20ms步长
        const int maxSteps = static_cast<int>(maxTime / dt);
        float closestDist = std::numeric_limits<float>::max();
        sf::Vector2f closestPoint;
        
        for (int i = 0; i < maxSteps; ++i) {
            result.points.push_back(pos);
            
            // 检查是否击中目标
            float distToTarget = distance(pos, target.position);
            if (distToTarget < closestDist) {
                closestDist = distToTarget;
                closestPoint = pos;
            }
            
            // 简化碰撞检测：检查是否在目标范围内
            float targetRadius = target.size.x;  // 对于方块，使用宽度的一半
            if (target.type == TargetInfo::Pig) {
                targetRadius = target.size.x;  // 猪的半径
            } else {
                targetRadius = std::max(target.size.x, target.size.y) * 0.5f;
            }
            
            // 对于炸弹鸟，扩大碰撞检测范围（因为爆炸范围大）
            float collisionRadius = targetRadius + 10.0f;
            if (birdType == BirdType::Bomb) {
                collisionRadius = targetRadius + 30.0f;  // 炸弹鸟有更大的爆炸范围
            }
            
            if (distToTarget < collisionRadius) {
                result.hitTarget = true;
                result.hitTime = i * dt;
                result.hitPoint = pos;
                result.minDistanceToTarget = distToTarget;
                break;
            }
            
            // 应用物理（返回更新后的位置和速度）
            std::tie(pos, vel) = applyPhysicsStep(pos, vel, dt, maxSpeed);
            
            // 边界检查
            if (pos.y > static_cast<float>(config::kWindowHeight) + 100.0f || 
                pos.x < -100.0f || pos.x > static_cast<float>(config::kWindowWidth) + 100.0f) {
                break;
            }
        }
        
        if (!result.hitTarget) {
            result.minDistanceToTarget = closestDist;
            result.hitPoint = closestPoint;
        }
        
        result.finalVelocity = length(vel);
    }
    
    // 性能统计
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    float timeMs = duration.count() / 1000.0f;
    
    stats_.avgTrajectoryTimeMs = (stats_.avgTrajectoryTimeMs * (stats_.trajectoryCalculations - 1) + timeMs) / stats_.trajectoryCalculations;
    if (timeMs > stats_.maxTrajectoryTimeMs) {
        stats_.maxTrajectoryTimeMs = timeMs;
    }
    
    return result;
}

TrajectoryResult AIController::calculateYellowBirdTrajectory(const sf::Vector2f& startPos,
                                                            const sf::Vector2f& initialVelocity,
                                                            float skillActivationTime,
                                                            const TargetInfo& target,
                                                            float maxTime) {
    TrajectoryResult result;
    
    sf::Vector2f pos = startPos;
    sf::Vector2f vel = initialVelocity;
    
    const float dt = 0.02f;
    const int maxSteps = static_cast<int>(maxTime / dt);
    float currentTime = 0.0f;
    
    // 黄鸟技能立即激活（AI模式下立即激活）
    // 注意：AI计算时假设初始速度可达1000（kYellowInitialMax * 2.0f），但实际发射时
    // Bird::launch()会将速度限制为500，然后activateSkill()立即翻倍到1000
    // 技能激活后，速度翻倍但受限于kYellowMaxSpeed = 1500
    bool skillActivated = true;  // 立即激活
    float maxSpeed = config::bird_speed::kYellowMaxSpeed;  // 技能后最大速度：1500
    
    // 技能激活：速度翻倍（但受限于maxSpeed = 1500）
    // 初始速度已经是1000（2倍初始速度），翻倍后是2000，但受限于1500
    float currentSpeed = length(vel);
    if (currentSpeed > 0.001f) {
        float newSpeed = std::min(currentSpeed * 2.0f, maxSpeed);  // 翻倍但不超过1500
        vel = normalize(vel) * newSpeed;
        Logger::getInstance().info("黄鸟轨迹计算: 初始速度=" + std::to_string(currentSpeed) + 
                                  ", 技能后速度=" + std::to_string(newSpeed));
    }
    
    float closestDist = std::numeric_limits<float>::max();
    sf::Vector2f closestPoint;
    
    for (int i = 0; i < maxSteps; ++i) {
        currentTime += dt;
        
        // 技能已经在开始时激活，不需要再次检查
        
        result.points.push_back(pos);
        
        // 检查碰撞
        float distToTarget = distance(pos, target.position);
        if (distToTarget < closestDist) {
            closestDist = distToTarget;
            closestPoint = pos;
        }
        
        float targetRadius = target.size.x;
        if (target.type == TargetInfo::Block) {
            targetRadius = std::max(target.size.x, target.size.y) * 0.5f;
        }
        
        if (distToTarget < targetRadius + 10.0f) {
            result.hitTarget = true;
            result.hitTime = currentTime;
            result.hitPoint = pos;
            result.minDistanceToTarget = distToTarget;
            break;
        }
        
        // 应用物理（返回更新后的位置和速度）
        std::tie(pos, vel) = applyPhysicsStep(pos, vel, dt, maxSpeed);
        
        // 边界检查
        if (pos.y > static_cast<float>(config::kWindowHeight) + 100.0f || 
            pos.x < -100.0f || pos.x > static_cast<float>(config::kWindowWidth) + 100.0f) {
            break;
        }
    }
    
    if (!result.hitTarget) {
        result.minDistanceToTarget = closestDist;
        result.hitPoint = closestPoint;
    }
    
    result.finalVelocity = length(vel);
    return result;
}

std::pair<sf::Vector2f, sf::Vector2f> AIController::applyPhysicsStep(sf::Vector2f pos, sf::Vector2f vel, float dt, float maxSpeed) {
    // 应用重力
    vel.y += config::kGravity * dt;
    
    // 应用空气阻力
    float speed = length(vel);
    if (speed > 0.001f) {
        const float airResistanceAccelPixels = config::kAirResistanceAccel * config::kPixelsPerMeter;
        sf::Vector2f resistanceDir = normalize(vel);
        sf::Vector2f resistanceAccel = -resistanceDir * airResistanceAccelPixels;
        vel += resistanceAccel * dt;
        
        // 重新计算速度（空气阻力可能改变了速度大小）
        speed = length(vel);
    }
    
    // 限制速度（必须在应用重力和空气阻力之后检查）
    if (speed > maxSpeed) {
        vel = normalize(vel) * maxSpeed;
    }
    
    // 更新位置
    pos += vel * dt;
    
    return std::make_pair(pos, vel);
}

float AIController::calculateAirResistance(float speed) {
    return config::kAirResistanceAccel * config::kPixelsPerMeter;
}

// ========== 轨迹预览更新 ==========

void AIController::updateTrajectoryPreview() {
    // 生成轨迹预览（只计算1秒的轨迹）
    trajectoryPreview_.clear();
    if (currentAim_.isValid && !currentAim_.trajectoryPoints.empty()) {
        // 计算1秒内的轨迹点（假设60fps，1秒约60个点）
        const float previewTime = 1.0f;
        const float dt = 0.0167f;  // 约60fps的时间步长
        const int maxPreviewSteps = static_cast<int>(previewTime / dt);
        
        // 使用已有的轨迹点，但只取前1秒的
        int pointsToShow = std::min(static_cast<int>(currentAim_.trajectoryPoints.size()), maxPreviewSteps);
        
        for (int i = 0; i < pointsToShow; ++i) {
            const auto& pt = currentAim_.trajectoryPoints[i];
            float alpha = 255.0f * (1.0f - static_cast<float>(i) / pointsToShow);
            unsigned char alphaByte = static_cast<unsigned char>(alpha);
            trajectoryPreview_.emplace_back(pt, sf::Color(255u, 255u, 0u, alphaByte));
        }
    }
}

// ========== 子系统4: 发射参数计算 ==========

AimingInfo AIController::calculateOptimalAim(BirdType birdType,
                                             const TargetInfo& target,
                                             const sf::Vector2f& slingshotPos) {
    AimingInfo aim;
    aim.target = target;
    aim.dragStart = slingshotPos;
    
    // 使用迭代优化找到最佳角度和力度
    aim = optimizeLaunchParameters(birdType, target, slingshotPos);
    
    return aim;
}

AimingInfo AIController::optimizeLaunchParameters(BirdType birdType,
                                                  const TargetInfo& target,
                                                  const sf::Vector2f& slingshotPos) {
    AimingInfo bestAim;
    bestAim.isValid = false;
    float bestError = std::numeric_limits<float>::max();
    
    // 获取初始最大速度限制
    float baseMaxSpeed = config::bird_speed::kRedInitialMax;
    float maxSpeed = config::bird_speed::kRedMaxSpeed;
    bool useSkill = false;
    
    switch (birdType) {
        case BirdType::Red:
            baseMaxSpeed = config::bird_speed::kRedInitialMax;
            maxSpeed = config::bird_speed::kRedMaxSpeed;
            break;
        case BirdType::Yellow:
            // 黄鸟在AI模式下允许2倍拉弓距离，但实际发射速度仍限制为kYellowInitialMax (500)
            // 然后立即激活技能，速度翻倍到1000（但受限于kYellowMaxSpeed = 1500）
            // AI计算时使用2倍速度假设，以模拟技能激活后的效果
            baseMaxSpeed = config::bird_speed::kYellowInitialMax * 2.0f;  // AI计算用：假设初始速度可达1000
            maxSpeed = config::bird_speed::kYellowMaxSpeed;                // 技能后速度：1500
            useSkill = true;  // 黄鸟默认使用技能
            break;
        case BirdType::Bomb:
            baseMaxSpeed = config::bird_speed::kBombInitialMax;
            maxSpeed = config::bird_speed::kBombMaxSpeed;
            break;
    }
    
    // 注意：baseMaxSpeed是初始发射速度限制，轨迹计算时会考虑技能加速
    
    // 迭代搜索最佳角度和力度
    // 角度范围：5-85度，步长2度（扩大范围以找到更多可能的角度）
    // 对于炸弹鸟，角度范围可以更大（包括更小的角度）
    // 力度范围：20-100%，步长5%（降低最小力度，允许更小的力度）
    // 如果找到误差<3%的解决方案，提前终止
    float angleStep = (birdType == BirdType::Bomb) ? 1.5f : 2.0f;  // 炸弹鸟使用更小的步长
    for (float angle = 5.0f; angle <= 85.0f; angle += angleStep) {
        // 如果已经找到很好的解，可以进行精细搜索
        if (bestError < 3.0f && angle > 5.0f) {
            // 在最佳角度附近进行精细搜索
            float fineAngleStart = std::max(5.0f, angle - 4.0f);
            float fineAngleEnd = std::min(85.0f, angle + 4.0f);
            for (float fineAngle = fineAngleStart; fineAngle <= fineAngleEnd; fineAngle += 1.0f) {
                if (fineAngle == angle) continue;  // 跳过已经计算过的角度
                
                for (float power = 20.0f; power <= 100.0f; power += 5.0f) {
                    sf::Vector2f velocity = velocityFromAngleAndPower(fineAngle, power, birdType);
                    
                    float speed = length(velocity);
                    if (speed > baseMaxSpeed) {
                        velocity = normalize(velocity) * baseMaxSpeed;
                    }
                    
                    // 对于炸弹鸟，使用更长的计算时间以覆盖远距离目标
                    float maxTrajectoryTime = (birdType == BirdType::Bomb) ? 8.0f : 5.0f;
                    TrajectoryResult traj = calculateTrajectory(
                        slingshotPos, velocity, birdType, useSkill, target, maxTrajectoryTime);
                    
                    float error = traj.minDistanceToTarget;
                    if (traj.hitTarget) error = 0.0f;
                    
                    float targetSize = std::max(target.size.x, target.size.y);
                    float errorPercent = (error / targetSize) * 100.0f;
                    
                    if (errorPercent < bestError) {
                        bestError = errorPercent;
                        bestAim.isValid = true;
                        bestAim.angle = fineAngle;
                        bestAim.power = power;
                        bestAim.trajectoryError = errorPercent;
                        bestAim.trajectoryPoints = traj.points;
                        bestAim.predictedHitPoint = traj.hitPoint;
                        
                        sf::Vector2f pull = -velocity / config::kSlingshotStiffness;
                        float pullDist = length(pull);
                        float maxPull = config::kMaxPullDistance;
                        if (birdType == BirdType::Yellow) {
                            maxPull = config::kMaxPullDistance * 2.0f;
                        }
                        
                        if (pullDist > maxPull) {
                            pull = normalize(pull) * maxPull;
                            // 重新计算速度，确保与限制后的pull一致
                            velocity = -pull * config::kSlingshotStiffness;
                            // 限制速度到baseMaxSpeed
                            float speed = length(velocity);
                            if (speed > baseMaxSpeed) {
                                velocity = normalize(velocity) * baseMaxSpeed;
                            }
                        }
                        
                        bestAim.dragEnd = slingshotPos + pull;
                        
                        if (birdType == BirdType::Yellow && useSkill) {
                            bestAim.skillActivationTime = 0.0f;  // 立即激活
                        }
                    }
                }
            }
        }
        
        for (float power = 20.0f; power <= 100.0f; power += 5.0f) {
            sf::Vector2f velocity = velocityFromAngleAndPower(angle, power, birdType);
            
            // 限制初始速度
            float speed = length(velocity);
            if (speed > baseMaxSpeed) {
                velocity = normalize(velocity) * baseMaxSpeed;
            }
            
            // 计算轨迹（对于炸弹鸟，使用更长的计算时间以覆盖远距离目标）
            float maxTrajectoryTime = (birdType == BirdType::Bomb) ? 8.0f : 5.0f;
            TrajectoryResult traj = calculateTrajectory(
                slingshotPos, velocity, birdType, useSkill, target, maxTrajectoryTime);
            
            // 评估误差
            float error = traj.minDistanceToTarget;
            
            if (traj.hitTarget) {
                error = 0.0f;  // 击中目标，误差为0
            }
            
            // 转换为百分比误差（相对于目标大小）
            float targetSize = std::max(target.size.x, target.size.y);
            float errorPercent = (error / targetSize) * 100.0f;
            
            if (errorPercent < bestError) {
                bestError = errorPercent;
                bestAim.isValid = true;
                bestAim.angle = angle;
                bestAim.power = power;
                bestAim.trajectoryError = errorPercent;
                bestAim.trajectoryPoints = traj.points;
                bestAim.predictedHitPoint = traj.hitPoint;
                
                // 计算拖拽终点
                // 拖拽向量 = -velocity / stiffness（反向）
                sf::Vector2f pull = -velocity / config::kSlingshotStiffness;
                float pullDist = length(pull);
                
                // 限制拖拽距离
                float maxPull = config::kMaxPullDistance;
                if (birdType == BirdType::Yellow) {
                    maxPull = config::kMaxPullDistance * 2.0f;  // 黄鸟允许2倍距离
                }
                
                // 如果拉弓距离超过限制，重新计算速度和pull以保持一致
                if (pullDist > maxPull) {
                    pull = normalize(pull) * maxPull;
                    // 重新计算速度，确保与限制后的pull一致
                    velocity = -pull * config::kSlingshotStiffness;
                    // 限制速度到baseMaxSpeed
                    float speed = length(velocity);
                    if (speed > baseMaxSpeed) {
                        velocity = normalize(velocity) * baseMaxSpeed;
                        // 再次更新pull以保持一致
                        pull = -velocity / config::kSlingshotStiffness;
                    }
                }
                
                bestAim.dragEnd = slingshotPos + pull;
                
                // 黄鸟技能激活时间（立即激活）
                if (birdType == BirdType::Yellow && useSkill) {
                    bestAim.skillActivationTime = 0.0f;  // 发射时立即激活
                }
            }
        }
        
        // 性能优化：如果已找到误差<1%的完美解，提前终止
        if (bestError < 1.0f) {
            break;
        }
    }
    
    return bestAim;
}

sf::Vector2f AIController::velocityFromAngleAndPower(float angle, float power, BirdType birdType) {
    // 角度转换为弧度（0度=向右，90度=向上）
    float angleRad = angle * M_PI / 180.0f;
    
    // 力度转换为速度（0-100% -> 0-maxSpeed）
    // 注意：这里使用初始最大速度，因为这是发射时的实际速度限制
    float maxSpeed = config::bird_speed::kRedInitialMax;
    switch (birdType) {
        case BirdType::Red:
            maxSpeed = config::bird_speed::kRedInitialMax;
            break;
        case BirdType::Yellow:
            // 黄鸟在AI模式下允许2倍拉弓距离，但实际发射速度仍限制为kYellowInitialMax (500)
            // 然后立即激活技能，速度翻倍。AI计算时使用2倍速度假设，以模拟技能激活后的效果
            maxSpeed = config::bird_speed::kYellowInitialMax * 2.0f;  // AI计算用：假设初始速度可达1000
            break;
        case BirdType::Bomb:
            maxSpeed = config::bird_speed::kBombInitialMax;
            break;
    }
    
    float speed = (power / 100.0f) * maxSpeed;
    
    // 计算速度向量（角度：0度=右，90度=上，即向上为负Y）
    sf::Vector2f velocity;
    velocity.x = std::cos(angleRad) * speed;
    velocity.y = -std::sin(angleRad) * speed;  // 向上为负Y
    
    return velocity;
}

// ========== 辅助函数 ==========

bool AIController::isPointInBounds(const sf::Vector2f& point) const {
    return point.x >= 0 && point.x <= static_cast<float>(config::kWindowWidth) &&
           point.y >= 0 && point.y <= static_cast<float>(config::kWindowHeight);
}

float AIController::distance(const sf::Vector2f& a, const sf::Vector2f& b) const {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float AIController::length(const sf::Vector2f& v) const {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

sf::Vector2f AIController::normalize(const sf::Vector2f& v) const {
    float len = length(v);
    if (len < 0.0001f) return sf::Vector2f(0.0f, 0.0f);
    return sf::Vector2f(v.x / len, v.y / len);
}

bool AIController::raycastToTarget(const sf::Vector2f& start, const sf::Vector2f& end,
                                   const std::vector<TargetInfo>& obstacles,
                                   sf::Vector2f& hitPoint) const {
    // 简化的射线检测实现
    sf::Vector2f dir = normalize(end - start);
    float dist = distance(start, end);
    
    for (const auto& obstacle : obstacles) {
        sf::Vector2f toObstacle = obstacle.position - start;
        float projDist = toObstacle.x * dir.x + toObstacle.y * dir.y;
        
        if (projDist > 0 && projDist < dist) {
            sf::Vector2f projPoint = start + dir * projDist;
            float distToObstacle = distance(projPoint, obstacle.position);
            
            float obstacleRadius = std::max(obstacle.size.x, obstacle.size.y) * 0.5f;
            if (distToObstacle < obstacleRadius) {
                hitPoint = projPoint;
                return true;
            }
        }
    }
    
    return false;
}

std::vector<BirdType> AIController::determineLaunchOrder(const std::deque<std::unique_ptr<Bird>>& birds) {
    std::vector<BirdType> order;
    for (const auto& bird : birds) {
        if (bird && !bird->isLaunched()) {
            order.push_back(bird->type());
        }
    }
    return order;
}
