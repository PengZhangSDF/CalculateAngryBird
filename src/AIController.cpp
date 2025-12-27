// AI智能化操控系统实现
#include "AIController.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>

#include "Config.hpp"
#include "Game.hpp"
#include "Logger.hpp"

AIController::AIController() {
    // 初始化视觉反馈
    targetMarker_.setRadius(15.0f);
    targetMarker_.setFillColor(sf::Color(255, 0, 0, 150));
    targetMarker_.setOutlineColor(sf::Color::Red);
    targetMarker_.setOutlineThickness(2.0f);
    targetMarker_.setOrigin(sf::Vector2f(15.0f, 15.0f));
}

void AIController::setGame(Game* game) {
    game_ = game;
}

void AIController::setEnabled(bool enabled) {
    if (enabled == enabled_) return;
    
    enabled_ = enabled;
    if (enabled_) {
        state_ = AIState::WaitingForBird;
        aiming_ = false;
        shouldLaunch_ = false;
        shouldActivateSkill_ = false;
        waitTimer_ = 0.0f;
        aimTimer_ = 0.0f;
        aimProgress_ = 0.0f;
        currentTarget_.reset();
        Logger::getInstance().info("AI Controller enabled");
    } else {
        aiming_ = false;
        shouldLaunch_ = false;
        shouldActivateSkill_ = false;
        currentTarget_.reset();
        Logger::getInstance().info("AI Controller disabled");
    }
}

void AIController::update(float dt, const std::vector<std::unique_ptr<Block>>& blocks,
                         const std::vector<std::unique_ptr<Pig>>& pigs,
                         const std::deque<std::unique_ptr<Bird>>& birds,
                         const sf::Vector2f& slingshotPos) {
    if (!enabled_) return;
    
    updateAI(dt, blocks, pigs, birds, slingshotPos);
}

void AIController::updateAI(float dt, const std::vector<std::unique_ptr<Block>>& blocks,
                            const std::vector<std::unique_ptr<Pig>>& pigs,
                            const std::deque<std::unique_ptr<Bird>>& birds,
                            const sf::Vector2f& slingshotPos) {
    switch (state_) {
        case AIState::WaitingForBird: {
            // 清除之前的轨迹线和目标
            aimTrajectory_.clear();
            currentTarget_.reset();
            currentAim_.isValid = false;
            
            // 严格等待第一只鸟完全消失（isDestroyed）后才能开始下一只
            // 不能使用超时逻辑，必须等待上一只鸟完全从birds_中移除
    if (birds.empty()) {
                // 没有鸟了，保持等待状态
                break;
            }
            
            Bird* firstBird = birds.front().get();
            if (!firstBird) {
                // 第一只鸟不存在，可以开始分析
                state_ = AIState::Analyzing;
                break;
            }
            
            // 如果第一只鸟还未发射，可以开始分析（这意味着没有上一只鸟在等待）
            if (!firstBird->isLaunched()) {
                state_ = AIState::Analyzing;
                break;
            }
            
            // 如果第一只鸟已发射，必须等待它完全消失（isDestroyed）
            if (firstBird->isLaunched() && !firstBird->isDestroyed()) {
                // 上一只鸟还在，必须等待它消失
                break;  // 继续等待
            }
            
            // 第一只鸟已发射且已destroyed，可以开始分析下一只
            state_ = AIState::Analyzing;
            break;
        }
        
        case AIState::Analyzing: {
            // 分析目标并选择最佳目标
            if (birds.empty()) {
                state_ = AIState::WaitingForBird;
                break;
            }
            
            // 查找第一个未发射的鸟
            Bird* currentBird = nullptr;
            for (auto& bird : birds) {
                if (bird && !bird->isLaunched()) {
                    currentBird = bird.get();
                    break;
                }
            }
            
            if (!currentBird) {
                // 所有鸟都已发射，等待
                state_ = AIState::WaitingForBird;
                break;
            }
            
            // 分析所有目标
            std::vector<AITarget> targets = analyzeTargets(pigs, blocks, slingshotPos);
            if (targets.empty()) {
                Logger::getInstance().warning("AI未找到任何目标");
                state_ = AIState::WaitingForBird;
                break;
            }
            
            // 选择最佳目标
            std::optional<AITarget> bestTarget = selectBestTarget(currentBird->type(), targets, slingshotPos);
            if (!bestTarget.has_value()) {
                std::ostringstream msg;
                msg << "AI无法选择目标 - 目标数量: " << targets.size();
                Logger::getInstance().warning(msg.str());
                
                // 如果确实有目标但选择失败，使用第一个目标作为保底
                if (!targets.empty()) {
                    for (const auto& target : targets) {
                        if (target.pig && !target.pig->isDestroyed()) {
                            bestTarget = target;
                            Logger::getInstance().warning("使用保底策略选择目标");
                            break;
                        }
                    }
            }
            
            if (!bestTarget.has_value()) {
                state_ = AIState::WaitingForBird;
                break;
                }
            }
            
            currentTarget_ = bestTarget;
            
            std::ostringstream msg;
            msg << "AI选择目标: 位置(" << bestTarget->position.x << ", " << bestTarget->position.y 
                << "), 距离: " << bestTarget->distance << ", 优先级: " << bestTarget->priority;
            Logger::getInstance().info(msg.str());
            
            // 计算瞄准结果
            currentAim_ = calculateTrajectory(slingshotPos, bestTarget->position, currentBird->type());
            
            // 保存瞄准结果（确保在ReadyToLaunch状态时仍然有效）
            // 注意：generateTrajectoryLine可能会修改currentAim_.isValid，所以先保存
            AIAimResult savedAim = currentAim_;
            
            if (!currentAim_.isValid) {
                std::ostringstream msg;
                msg << "AI轨迹计算失败 - 目标位置(" << bestTarget->position.x << ", " << bestTarget->position.y 
                    << "), 距离: " << bestTarget->distance << "，重试中...";
                Logger::getInstance().warning(msg.str());
                
                // 添加重试计数，避免无限循环
                static int retryCount = 0;
                retryCount++;
                if (retryCount > 30) {
                    Logger::getInstance().warning("轨迹计算重试次数过多，跳过此目标");
                    retryCount = 0;
                    // 尝试选择下一个目标，或者等待
                    state_ = AIState::WaitingForBird;
                }
                // 保持在Analyzing状态，下一帧重试
                break;
            }
            
            // 成功计算轨迹，重置重试计数
            static int retryCount = 0;
            retryCount = 0;
            
            // 保存当前鸟类类型
            currentBirdType_ = currentBird->type();
            
            // 生成瞄准轨迹线（可能会修改currentAim_.isValid，所以先恢复）
            generateTrajectoryLine(slingshotPos);
            
            // 恢复保存的瞄准结果（确保isValid保持为true）
            currentAim_ = savedAim;
            
            // 进入瞄准状态
            state_ = AIState::Aiming;
            aiming_ = true;
            aimProgress_ = 0.0f;
            aimTimer_ = 0.0f;
            
            break;
        }
        
        case AIState::Aiming: {
            // 模拟拉弓过程
            aimTimer_ += dt;
            aimProgress_ = std::min(1.0f, aimTimer_ / aimDuration_);
            
            if (aimProgress_ >= 1.0f) {
                state_ = AIState::ReadyToLaunch;
                shouldLaunch_ = true;
                Logger::getInstance().info("AI瞄准完成，准备发射");
                
                // 设置技能激活标志（黄鸟）
                if (currentBirdType_ == BirdType::Yellow) {
                    shouldActivateSkill_ = true;
                    skillActivationTime_ = 0.0f;  // 立即激活
                }
            }
            break;
        }
        
        case AIState::ReadyToLaunch: {
            // 等待Game.cpp处理发射
            break;
        }
    }
}

bool AIController::isCurrentBirdReady(const std::deque<std::unique_ptr<Bird>>& birds) const {
    if (birds.empty()) {
        return true;  // 没有鸟了，可以准备（虽然可能没有鸟可发射）
    }
    
    Bird* currentBird = birds.front().get();
    if (!currentBird) {
        return true;  // 当前鸟不存在，可以准备
    }
    
    // 如果当前鸟还未发射，说明可以发射（不在等待状态）
    if (!currentBird->isLaunched()) {
        return true;
    }
    
    // 如果鸟已发射，必须等待它完全消失（isDestroyed）才能开始下一只
    // 不能只检查active，因为鸟可能已经失活但还没被移除
    if (currentBird->isLaunched()) {
        // 只有当鸟被标记为destroyed时，才认为可以准备下一只
        // 这确保上一只鸟完全从birds_中移除后，才开始下一只
        if (currentBird->isDestroyed()) {
            return true;  // 鸟已完全消失，可以准备下一只
        }
        // 鸟已发射但还未消失，必须等待
        return false;
    }
    
    return false;
}

// 分析所有目标，计算优先级和遮挡信息
std::vector<AITarget> AIController::analyzeTargets(const std::vector<std::unique_ptr<Pig>>& pigs,
                                                   const std::vector<std::unique_ptr<Block>>& blocks,
                                                   const sf::Vector2f& slingshotPos) {
    std::vector<AITarget> targets;
    
    for (const auto& pig : pigs) {
        if (!pig || pig->isDestroyed()) continue;
        
        AITarget target;
        target.position = pig->position();
        target.pig = pig.get();
        
        // 计算距离
        sf::Vector2f delta = target.position - slingshotPos;
        target.distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        
        // 计算遮挡程度（多重遮挡检测）
        target.protectionLevel = calculateOcclusionLevel(slingshotPos, target.position, blocks);
        target.isExposed = (target.protectionLevel == 0);
        
        // 计算基础优先级（距离越近优先级越高）
        target.priority = 1000.0f / (target.distance + 1.0f);
        
        targets.push_back(target);
    }
    
    return targets;
}

// 计算遮挡等级（多重遮挡检测算法）
int AIController::calculateProtectionLevel(const sf::Vector2f& targetPos,
                                          const std::vector<std::unique_ptr<Block>>& blocks) const {
    // 检查目标周围的方块（保护等级）
    int protection = 0;
    for (const auto& block : blocks) {
        if (!block || block->isDestroyed()) continue;
            
            sf::Vector2f blockPos = block->position();
        sf::Vector2f delta = blockPos - targetPos;
            float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            
            if (dist < 100.0f) {
            protection++;
        }
    }
    return protection;
}

// 多重遮挡检测算法：精确计算目标与发射位置之间的障碍物
int AIController::calculateOcclusionLevel(const sf::Vector2f& fromPos,
                                         const sf::Vector2f& toPos,
                                          const std::vector<std::unique_ptr<Block>>& blocks) const {
    int occlusionCount = 0;
    sf::Vector2f direction = toPos - fromPos;
    float totalDistance = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    
    if (totalDistance < 1.0f) return 0;
    
    // 归一化方向
    direction.x /= totalDistance;
    direction.y /= totalDistance;
    
    // 沿着路径采样点，检测是否有方块遮挡
    const int samplePoints = static_cast<int>(totalDistance / 20.0f);  // 每20像素采样一次
    
    for (int i = 1; i < samplePoints; ++i) {
        float t = static_cast<float>(i) / samplePoints;
        sf::Vector2f samplePos = fromPos + direction * (totalDistance * t);
        
        // 检查这个采样点是否被任何方块遮挡
        for (const auto& block : blocks) {
            if (!block || block->isDestroyed()) continue;
            
            sf::Vector2f blockPos = block->position();
            // 获取方块大小（需要从body获取，这里使用近似值）
            // 假设方块大小约为40x40像素（可以根据实际情况调整）
            float blockSize = 40.0f;
            
            // 计算采样点到方块的距离
            sf::Vector2f delta = samplePos - blockPos;
            float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            
            // 如果采样点在方块范围内（考虑方块大小）
            if (dist < blockSize * 0.7f) {
                occlusionCount++;
                break;  // 每个采样点只计算一次遮挡
            }
        }
    }
    
    // 同时检查目标周围的方块（保护等级）
    for (const auto& block : blocks) {
        if (!block || block->isDestroyed()) continue;
        
        sf::Vector2f blockPos = block->position();
        sf::Vector2f delta = blockPos - toPos;
        float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        
        if (dist < 100.0f) {
            occlusionCount++;  // 附近的方块也算作保护
        }
    }
    
    return occlusionCount;
}

// 选择最佳目标（根据鸟类类型）
std::optional<AITarget> AIController::selectBestTarget(BirdType birdType,
                                                      const std::vector<AITarget>& targets,
                                                      const sf::Vector2f& slingshotPos) {
    switch (birdType) {
        case BirdType::Red:
            return selectRedBirdTarget(targets, slingshotPos);
        case BirdType::Yellow:
            return selectYellowBirdTarget(targets, slingshotPos);
        case BirdType::Bomb:
            return selectBombBirdTarget(targets, {}, slingshotPos);
        default:
            return selectRedBirdTarget(targets, slingshotPos);
    }
}

// 红鸟策略：首先选择最近的无遮挡目标，其次选择中距离的无遮挡目标
std::optional<AITarget> AIController::selectRedBirdTarget(const std::vector<AITarget>& targets,
                                                          const sf::Vector2f& slingshotPos) {
    if (targets.empty()) return std::nullopt;
    
    std::optional<AITarget> bestTarget;
    float bestScore = std::numeric_limits<float>::max();
    
    // 首先尝试选择无遮挡目标
    for (const auto& target : targets) {
        if (target.pig && target.pig->isDestroyed()) continue;
        
        // 优先无遮挡目标
        if (!target.isExposed) continue;
        
        float score = target.distance;
        
        // 优先最近距离的目标，其次中距离（300-600像素）
        if (target.distance > 600.0f) {
            score += 500.0f;  // 远距离目标优先级降低
        }
        
        if (score < bestScore) {
            bestScore = score;
            bestTarget = target;
        }
    }
    
    // 如果没有无遮挡目标，则选择有遮挡的目标（但优先级较低）
    if (!bestTarget.has_value()) {
        for (const auto& target : targets) {
            if (target.pig && target.pig->isDestroyed()) continue;
            
            float score = target.distance + 1000.0f;  // 有遮挡目标额外惩罚
            
            if (score < bestScore) {
                bestScore = score;
                bestTarget = target;
            }
        }
    }
    
    return bestTarget;
}

// 黄鸟策略：首先选择远距离无遮挡目标，其次选择中远距离有遮挡目标
std::optional<AITarget> AIController::selectYellowBirdTarget(const std::vector<AITarget>& targets,
                                                             const sf::Vector2f& slingshotPos) {
    if (targets.empty()) return std::nullopt;
    
    std::optional<AITarget> bestTarget;
    float bestScore = -1.0f;
    
    // 首先尝试远距离无遮挡目标
    for (const auto& target : targets) {
        if (target.pig && target.pig->isDestroyed()) continue;
        
        float score = 0.0f;
        
        // 优先远距离无遮挡目标（距离>600且无遮挡）
        if (target.isExposed && target.distance > 600.0f) {
            score = target.distance;  // 距离越远分数越高
        }
        // 其次中远距离有遮挡目标（400-700且有遮挡）
        else if (!target.isExposed && target.distance >= 400.0f && target.distance <= 700.0f) {
            score = target.distance * 0.8f;  // 有遮挡的目标分数稍低
        }
        // 其他目标不考虑
        else {
            continue;
        }
        
        if (score > bestScore) {
            bestScore = score;
            bestTarget = target;
        }
    }
    
    // 如果没有符合条件的，选择任意远距离目标（>400像素）
    if (!bestTarget.has_value()) {
        for (const auto& target : targets) {
            if (target.pig && target.pig->isDestroyed()) continue;
            
            if (target.distance > 400.0f) {
                float score = target.distance * (target.isExposed ? 1.0f : 0.7f);
                if (score > bestScore) {
                    bestScore = score;
                    bestTarget = target;
                }
            }
        }
    }
    
    // 如果还是没有，选择任意目标（保底策略）
    if (!bestTarget.has_value()) {
        for (const auto& target : targets) {
            if (target.pig && target.pig->isDestroyed()) continue;
            
            float score = target.distance;
            if (score > bestScore) {
                bestScore = score;
                bestTarget = target;
            }
        }
    }
    
    return bestTarget;
}

// 炸弹鸟策略：优先锁定具有层层保护的目标
std::optional<AITarget> AIController::selectBombBirdTarget(const std::vector<AITarget>& targets,
                                                           const std::vector<std::unique_ptr<Block>>& blocks,
                                                           const sf::Vector2f& slingshotPos) {
    if (targets.empty()) return std::nullopt;
    
    std::optional<AITarget> bestTarget;
    int bestProtection = -1;
    float bestDistance = std::numeric_limits<float>::max();
    
    for (const auto& target : targets) {
        if (target.pig && target.pig->isDestroyed()) continue;
        
        // 优先选择保护等级最高的目标（层层保护）
        if (target.protectionLevel > bestProtection) {
            bestProtection = target.protectionLevel;
            bestDistance = target.distance;
            bestTarget = target;
        }
        // 如果保护等级相同，选择距离更近的
        else if (target.protectionLevel == bestProtection) {
            if (target.distance < bestDistance) {
                bestDistance = target.distance;
                bestTarget = target;
            }
        }
    }
    
    // 如果没有有保护的目标，选择任意目标（保底策略）
    if (!bestTarget.has_value()) {
        for (const auto& target : targets) {
            if (target.pig && target.pig->isDestroyed()) continue;
            
            if (target.distance < bestDistance) {
                bestDistance = target.distance;
            bestTarget = target;
            }
        }
    }
    
    return bestTarget;
}

// 检查轨迹是否经过目标上方或下方，并返回最近距离（用于二分查找）
// 返回值：-1=轨迹无效，0=经过目标下方，1=经过目标上方
int AIController::checkTrajectoryPass(const sf::Vector2f& slingshotPos,
                                      const sf::Vector2f& targetPos,
                                      const sf::Vector2f& initialVelocity,
                                      float& closestDist) const {
    sf::Vector2f pos = slingshotPos;
    sf::Vector2f vel = initialVelocity;
    const int maxSteps = 1500;
    const float stepDt = 0.01f;
    
    closestDist = std::numeric_limits<float>::max();
    bool passedTargetX = false;
    float yAtTargetX = 0.0f;
    
    // 计算目标距离
    float dx = targetPos.x - slingshotPos.x;
    float dy = targetPos.y - slingshotPos.y;
    
    // 严格限制x坐标范围
    float minX = std::min(slingshotPos.x, targetPos.x);
    float maxX = std::max(slingshotPos.x, targetPos.x);
    
    // 检查初始速度x方向
    if (initialVelocity.x <= 0.0f) {
        return -1;  // 轨迹无效
    }
    
    sf::Vector2f prevVel = initialVelocity;
    
    for (int step = 0; step < maxSteps; ++step) {
        // 应用重力和空气阻力
        vel.y += config::kGravity * stepDt;
        
        float currentSpeed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        if (currentSpeed > 0.001f) {
            // kAirResistanceAccel已经是负值（-0.25 m/s²），表示阻力
            // 直接乘以速度方向即可得到与速度方向相反的阻力加速度
            const float airResistanceAccelPixels = config::kAirResistanceAccel * config::kPixelsPerMeter;
            sf::Vector2f resistanceDir(vel.x / currentSpeed, vel.y / currentSpeed);
            sf::Vector2f resistanceAccel = resistanceDir * airResistanceAccelPixels;
            vel += resistanceAccel * stepDt;
        }
        
        // 检测速度突变（放宽条件，因为二分查找只需要判断方向，不需要严格验证）
        // 只有在极端情况下才判定为无效
        float prevSpeed = std::sqrt(prevVel.x * prevVel.x + prevVel.y * prevVel.y);
        if (prevSpeed > 0.001f && currentSpeed > 0.001f) {
            float speedChangeRatio = currentSpeed / prevSpeed;
            // 只有在速度突然减小超过50%且方向变化超过45度时才判定为无效
            if (speedChangeRatio < 0.5f) {
                sf::Vector2f prevDir = prevVel / prevSpeed;
                sf::Vector2f currDir = vel / currentSpeed;
                float dirDot = prevDir.x * currDir.x + prevDir.y * currDir.y;
                if (dirDot < 0.707f) {  // 约45度
                    return -1;  // 轨迹无效（速度突变）
                }
            }
        }
        
        // 更新位置
        pos = pos + vel * stepDt;
        prevVel = vel;
        
        // 计算到目标的距离
        float deltaX = pos.x - targetPos.x;
        float deltaY = pos.y - targetPos.y;
        float dist = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        
        if (dist < closestDist) {
            closestDist = dist;
        }
        
        // 检查是否经过目标X坐标（放宽条件，允许一定误差）
        if (!passedTargetX) {
            // 检查是否已经越过目标的X坐标（允许一定误差范围）
            float xTolerance = 10.0f;  // 10像素容差
            if ((dx > 0 && pos.x >= targetPos.x - xTolerance) || (dx < 0 && pos.x <= targetPos.x + xTolerance)) {
                passedTargetX = true;
                // 使用当前位置或线性插值估算在目标X坐标处的Y值
                if (std::abs(pos.x - targetPos.x) < xTolerance) {
                    yAtTargetX = pos.y;
                } else {
                    // 如果位置不在目标X坐标，使用线性插值
                    sf::Vector2f prevPos = pos - vel * stepDt;
                    if (std::abs(pos.x - prevPos.x) > 0.001f) {
                        float t = (targetPos.x - prevPos.x) / (pos.x - prevPos.x);
                        if (t > 0.0f && t <= 1.0f) {
                            yAtTargetX = prevPos.y + t * (pos.y - prevPos.y);
                        } else {
                            yAtTargetX = pos.y;
                        }
                    } else {
                        yAtTargetX = pos.y;
                    }
                }
            }
        }
        
        // 如果已经经过目标X坐标，可以提前结束（已获得判断所需信息）
        if (passedTargetX) {
            // 继续计算一小段距离以确保准确性，但不要过早终止
            if (dist > closestDist * 2.0f && step > 50) {
                break;
            }
        }
        
        // 检查X坐标范围（放宽，允许一定超出）
        float xTolerance = 50.0f;
        if (pos.x < minX - xTolerance || pos.x > maxX + xTolerance) {
            // 如果已经经过目标，可以判断；否则返回无效
            if (passedTargetX) {
                break;
            } else {
                return -1;  // 超出X坐标范围且未经过目标
            }
        }
        
        // 终止条件
        if (pos.y > static_cast<float>(config::kWindowHeight) + 200.0f) {
            break;
        }
        
        if (step > 100 && pos.x > targetPos.x && dist > closestDist * 2.0f) {
            break;
        }
    }
    
    // 如果没有经过目标X坐标，无法判断
    if (!passedTargetX) {
        return -1;
    }
    
    // 判断是经过上方还是下方
    if (yAtTargetX < targetPos.y) {
        return 0;  // 经过目标下方
    } else {
        return 1;  // 经过目标上方
    }
}

// 计算轨迹：使用二分查找策略
// 从最大力度开始，使用二分查找在角度范围内找到经过目标30像素误差内的轨迹
AIAimResult AIController::calculateTrajectory(const sf::Vector2f& slingshotPos,
                                             const sf::Vector2f& targetPos,
                                             BirdType birdType) {
    AIAimResult result;
    result.dragStart = slingshotPos;
    result.isValid = false;

    // 获取鸟类特性
    float baseMaxInitialSpeed = config::bird_speed::kRedInitialMax;

    switch (birdType) {
        case BirdType::Red:
            baseMaxInitialSpeed = config::bird_speed::kRedInitialMax;
            break;
        case BirdType::Yellow:
            baseMaxInitialSpeed = config::bird_speed::kYellowInitialMax * 2.0f;
            if (baseMaxInitialSpeed > config::bird_speed::kYellowMaxSpeed) {
                baseMaxInitialSpeed = config::bird_speed::kYellowMaxSpeed;
            }
            break;
        case BirdType::Bomb:
            baseMaxInitialSpeed = config::bird_speed::kBombInitialMax;
            break;
    }

    // 计算目标方向
    float dx = targetPos.x - slingshotPos.x;
    float dy = targetPos.y - slingshotPos.y;
    float targetDistance = std::sqrt(dx * dx + dy * dy);
    
    if (targetDistance < 10.0f) {
        Logger::getInstance().warning("AI轨迹计算失败 - 目标太近");
        return result;
    }
    
    // 对于黄鸟，允许更大的拉弓距离以实现2倍速度
    float maxPull = config::kMaxPullDistance;
    if (birdType == BirdType::Yellow) {
        maxPull = config::kMaxPullDistance * 2.0f;
    }
    
    // 角度范围：
    // 从最小发射角度(鸟到目标所成直线的角度)到最大发射角度(竖直向上发射)
    // 竖直向上是-90度（-π/2），这是最小的角度值
    // targetAngle是目标方向的角度
    float targetAngle = std::atan2(dy, dx);
    
    // 竖直向上是-90度（-π/2），这是角度值的最小值
    // 目标角度targetAngle可能是任意值
    // 用户要求：从targetAngle到-90度进行搜索
    // 在二分查找中，我们需要minAngle < maxAngle
    float minAngle = -1.57f;  // -90度（竖直向上），这是角度值的最小值
    float maxAngle = targetAngle;  // 目标角度
    
    // 如果targetAngle < -90度（不太可能），需要调整
    if (maxAngle < minAngle) {
        maxAngle = minAngle + 0.1f;  // 给一个小范围
    }
    
    // 限制最大角度不超过45度（避免过于水平）
    maxAngle = std::min(maxAngle, 0.785f);  // 45度
    
    // 添加调试日志
    std::ostringstream angleMsg;
    angleMsg << "角度范围设置 - targetAngle: " << targetAngle 
             << " (" << (targetAngle * 180.0f / 3.14159f) << "度)"
             << ", minAngle: " << minAngle 
             << " (" << (minAngle * 180.0f / 3.14159f) << "度)"
             << ", maxAngle: " << maxAngle
             << " (" << (maxAngle * 180.0f / 3.14159f) << "度)";
    Logger::getInstance().info(angleMsg.str());
    
    // 速度递减策略：从最大速度开始，逐步降低
    const int speedSteps = 5;
    const float speedStepRatio = 0.85f;
    
    for (int speedLevel = 0; speedLevel < speedSteps; ++speedLevel) {
        // 计算当前速度级别
        float currentMaxSpeed = baseMaxInitialSpeed * std::pow(speedStepRatio, speedLevel);
        float minSpeed = currentMaxSpeed * 0.3f;
        
        // 使用最大拉弓距离（最大力度）
        float r = maxPull;
        
        // 二分查找角度
        // lowAngle对应较小的角度值（更向上），highAngle对应较大的角度值（更向下，指向目标）
        // 初始：lowAngle = -90度（竖直向上），highAngle = targetAngle（指向目标）
        float lowAngle = minAngle;  // -90度（竖直向上）
        float highAngle = maxAngle; // targetAngle（指向目标）
        const int maxBinarySearchIterations = 10;
        const float tolerance = 30.0f;  // 30像素误差
        
        sf::Vector2f bestPull(0.f, 0.f);
        float bestDistance = std::numeric_limits<float>::max();
        bool foundValid = false;
        
        for (int iter = 0; iter < maxBinarySearchIterations; ++iter) {
            float midAngle = (lowAngle + highAngle) * 0.5f;
            
            // 计算拉弓向量和初速度
            sf::Vector2f pull(std::cos(midAngle) * r, std::sin(midAngle) * r);
            sf::Vector2f v0 = pull * config::kSlingshotStiffness;
            
            // 限制速度
            float speed = std::sqrt(v0.x * v0.x + v0.y * v0.y);
            if (speed < minSpeed) {
                // 速度太低，需要更向下的角度（更大的角度值）
                lowAngle = midAngle;
                continue;
            }
            if (speed > currentMaxSpeed) {
                v0 = v0 * (currentMaxSpeed / speed);
                pull = v0 / config::kSlingshotStiffness;
            }
            
            // 检查轨迹经过目标上方还是下方
            float closestDist;
            int passResult = checkTrajectoryPass(slingshotPos, targetPos, v0, closestDist);
            
            // 添加调试日志
            if (iter < 3 || iter == maxBinarySearchIterations - 1) {
                std::ostringstream dbg;
                dbg << "二分查找迭代 " << iter << " - 角度: " << midAngle 
                    << " (" << (midAngle * 180.0f / 3.14159f) << "度)"
                    << ", 速度级别: " << speedLevel 
                    << ", passResult: " << passResult 
                    << ", closestDist: " << closestDist
                    << ", 角度范围: [" << lowAngle << "(" << (lowAngle * 180.0f / 3.14159f) << "度)"
                    << ", " << highAngle << "(" << (highAngle * 180.0f / 3.14159f) << "度)]";
                Logger::getInstance().info(dbg.str());
            }
            
            // 记录最佳距离（即使不在容差范围内，也记录下来）
            if (passResult != -1 && closestDist < bestDistance) {
                bestPull = pull;
                bestDistance = closestDist;
            }
            
            // 检查是否在误差范围内
            if (passResult != -1 && closestDist <= tolerance) {
                // 距离已经足够接近，直接使用（checkTrajectoryPass已经验证了基本有效性）
                bestPull = pull;
                bestDistance = closestDist;
                foundValid = true;
                
                std::ostringstream msg;
                msg << "找到有效轨迹 - 迭代: " << iter << ", 角度: " << midAngle 
                    << ", 距离: " << closestDist;
                Logger::getInstance().info(msg.str());
                break;
            }
            
            if (passResult == -1) {
                // 轨迹无效，尝试更向下的角度（更大的角度值）
                lowAngle = midAngle;
                continue;
            }
            
            // 根据轨迹经过目标上方还是下方调整角度范围
            // lowAngle对应更向上的角度（更小的角度值），highAngle对应更向下的角度（更大的角度值）
            if (passResult == 0) {
                // 经过目标下方，需要更向上的角度（更小的角度值），所以更新highAngle
                highAngle = midAngle;
            } else {
                // 经过目标上方，需要更向下的角度（更大的角度值），所以更新lowAngle
                lowAngle = midAngle;
            }
            
            // 如果角度范围已经很小，停止搜索
            if (std::abs(highAngle - lowAngle) < 0.01f) {
                // 尝试最后的角度
                float finalAngle = (lowAngle + highAngle) * 0.5f;
                sf::Vector2f finalPull(std::cos(finalAngle) * r, std::sin(finalAngle) * r);
                sf::Vector2f finalV0 = finalPull * config::kSlingshotStiffness;
                float finalSpeed = std::sqrt(finalV0.x * finalV0.x + finalV0.y * finalV0.y);
                if (finalSpeed > currentMaxSpeed) {
                    finalV0 = finalV0 * (currentMaxSpeed / finalSpeed);
                    finalPull = finalV0 / config::kSlingshotStiffness;
                }
                
                float finalDist;
                float finalTime;
                if (validateTrajectoryWithTime(slingshotPos, targetPos, finalV0, finalDist, finalTime)) {
                    if (finalDist <= tolerance) {
                        bestPull = finalPull;
                        bestDistance = finalDist;
                        foundValid = true;
                        
                        std::ostringstream msg;
                        msg << "找到有效轨迹（最后尝试） - 角度: " << finalAngle 
                            << ", 距离: " << finalDist;
                        Logger::getInstance().info(msg.str());
                    }
                }
                break;
            }
        }
        
        // 如果没找到有效轨迹，但找到了接近的轨迹，记录日志
        if (!foundValid && bestDistance < std::numeric_limits<float>::max()) {
            std::ostringstream msg;
            msg << "未找到有效轨迹，但最佳距离: " << bestDistance << " (容差: " << tolerance << ")";
            Logger::getInstance().info(msg.str());
        }
        
        // 如果找到有效轨迹，使用它
        if (foundValid) {
            result.pull = bestPull;
            result.dragEnd = slingshotPos - bestPull;
            result.isValid = true;
            
            // 黄鸟技能立即激活
            if (birdType == BirdType::Yellow) {
                result.skillActivationTime = 0.0f;
            }
            
            std::ostringstream msg;
            msg << "轨迹计算成功 - 速度级别: " << speedLevel 
                << ", 最近距离: " << bestDistance
                << ", pull方向: (" << bestPull.x << ", " << bestPull.y << ")";
            Logger::getInstance().info(msg.str());
            
            return result;
        }
    }
    
    // 所有速度级别都无解
    std::ostringstream msg;
    msg << "轨迹搜索失败 - 目标距离: " << targetDistance 
        << ", 已尝试 " << speedSteps << " 个速度级别";
    Logger::getInstance().warning(msg.str());
    
    return result;
}

// 验证轨迹是否有效：平滑抛物线、无折线、x坐标范围合法（严格验证）
bool AIController::validateTrajectory(const sf::Vector2f& slingshotPos,
                                     const sf::Vector2f& targetPos,
                                     const sf::Vector2f& initialVelocity,
                                     float& closestDist) const {
    float timeToTarget;
    return validateTrajectoryWithTime(slingshotPos, targetPos, initialVelocity, closestDist, timeToTarget);
}

// 验证轨迹并计算到达目标的时间（严格验证：无折线，x坐标严格在范围内）
bool AIController::validateTrajectoryWithTime(const sf::Vector2f& slingshotPos,
                                             const sf::Vector2f& targetPos,
                                             const sf::Vector2f& initialVelocity,
                                             float& closestDist,
                                             float& timeToTarget) const {
    sf::Vector2f pos = slingshotPos;
    sf::Vector2f vel = initialVelocity;
    const int maxSteps = 1500;  // 增加步数以补偿更小的时间步长
    const float stepDt = 0.01f;  // 减小步长以提高模拟精度，避免轨迹突变
    
    closestDist = std::numeric_limits<float>::max();
    timeToTarget = std::numeric_limits<float>::max();
    
    // 计算目标距离
    float dx = targetPos.x - slingshotPos.x;
    float dy = targetPos.y - slingshotPos.y;
    float targetDist = std::sqrt(dx * dx + dy * dy);
    
    // 严格限制x坐标范围：轨迹上所有点的x坐标必须位于鸟与目标的x坐标之间
    float minX = std::min(slingshotPos.x, targetPos.x);
    float maxX = std::max(slingshotPos.x, targetPos.x);
    
    std::vector<sf::Vector2f> trajectoryPoints;
    trajectoryPoints.push_back(slingshotPos);
    
    // 检查初始速度x方向：vx应该大于0（向右），否则轨迹不合理
    if (initialVelocity.x <= 0.0f) {
        return false;  // 初始速度向左或垂直，轨迹不合理
    }
    
    // 允许向下发射（如果目标在下方），但需要确保轨迹是合理的抛物线
    // 如果初始速度向下（vy > 0），需要确保速度足够大，能够形成合理的轨迹
    if (initialVelocity.y > 0.0f) {
        // 向下发射时，需要足够大的速度才能形成合理的轨迹
        float speed = std::sqrt(initialVelocity.x * initialVelocity.x + initialVelocity.y * initialVelocity.y);
        if (speed < 200.0f) {
            return false;  // 速度太小，向下发射无法形成合理轨迹
        }
    }
    
    // 用于检测速度突变（速度强制缩放导致的折线）
    sf::Vector2f prevVel = initialVelocity;
    
    for (int step = 0; step < maxSteps; ++step) {
        float currentTime = step * stepDt;
        
        // 应用重力和空气阻力
        vel.y += config::kGravity * stepDt;
        
        float currentSpeed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        if (currentSpeed > 0.001f) {
            // kAirResistanceAccel已经是负值（-0.25 m/s²），表示阻力
            // 直接乘以速度方向即可得到与速度方向相反的阻力加速度
            const float airResistanceAccelPixels = config::kAirResistanceAccel * config::kPixelsPerMeter;
            sf::Vector2f resistanceDir(vel.x / currentSpeed, vel.y / currentSpeed);
            sf::Vector2f resistanceAccel = resistanceDir * airResistanceAccelPixels;
            vel += resistanceAccel * stepDt;
        }
        
        // 检测速度突变：如果速度向量的大小或方向发生突然变化（可能是速度限制导致的）
        // 正常的物理过程（重力和阻力）应该导致速度平滑变化
        float prevSpeed = std::sqrt(prevVel.x * prevVel.x + prevVel.y * prevVel.y);
        float speedChangeRatio = (prevSpeed > 0.001f) ? (currentSpeed / prevSpeed) : 1.0f;
        // 如果速度突然减小超过15%（可能是速度限制导致的），检查方向变化
        if (speedChangeRatio < 0.85f && prevSpeed > 0.001f) {
            // 计算速度方向的变化
            sf::Vector2f prevDir = prevVel / prevSpeed;
            sf::Vector2f currDir = vel / currentSpeed;
            float dirDot = prevDir.x * currDir.x + prevDir.y * currDir.y;
            // 如果速度方向和大小都发生了显著变化，可能是速度限制导致的折线
            if (dirDot < 0.98f) {  // 方向变化超过约12度，且速度突然减小
                // 这是速度强制缩放导致的折线，标记为无效
                return false;
            }
        }
        
        // 在更新位置之前，预测下一个位置是否会在X坐标范围内
        // 如果超出范围，不更新位置，直接返回false（避免产生截断导致的折线）
        sf::Vector2f nextPos = pos + vel * stepDt;
        if (nextPos.x < minX || nextPos.x > maxX) {
            // 超出X坐标范围，轨迹不合法
            // 不在超出范围的点处截断，直接返回false
            return false;
        }
        
        // 更新位置
        pos = nextPos;
        
        // 更新prevVel用于下一帧检测
        prevVel = vel;
        
        // 检查折线（尖锐拐点检测）- 使用改进的检测逻辑
        // 对于正常的抛物线，相邻线段的方向向量点积应该始终为正（方向变化小于90度）
        if (trajectoryPoints.size() >= 2) {
            sf::Vector2f p0 = trajectoryPoints[trajectoryPoints.size() - 2];
            sf::Vector2f p1 = trajectoryPoints.back();
            sf::Vector2f p2 = pos;
            
            sf::Vector2f v1 = p1 - p0;
            sf::Vector2f v2 = p2 - p1;
            
            float len1 = std::sqrt(v1.x * v1.x + v1.y * v1.y);
            float len2 = std::sqrt(v2.x * v2.x + v2.y * v2.y);
            
            if (len1 > 0.1f && len2 > 0.1f) {
                v1.x /= len1;
                v1.y /= len1;
                v2.x /= len2;
                v2.y /= len2;
                
                float dot = v1.x * v2.x + v1.y * v2.y;
                dot = std::max(-1.0f, std::min(1.0f, dot));
                
                // 检测折线（不可导点）：如果方向变化超过60度（dot < 0.5），认为是折线
                // 但在顶点区域需要更严格的检测，避免真正的折线被当作正常顶点
                // 检查是否在顶点区域（vel.y接近0或从负变正）
                bool isNearVertex = std::abs(vel.y) < 50.0f || (prevVel.y < 0.0f && vel.y > 0.0f);
                
                if (dot < 0.5f) {
                    // 如果不在顶点区域，直接判定为折线
                    if (!isNearVertex) {
                        return false;  // 检测到折线（不可导点，方向变化超过60度）
                    }
                    // 在顶点区域，使用更严格的阈值（30度）来检测真正的折线
                    if (dot < 0.866f) {  // cos(30°) ≈ 0.866
                        return false;  // 即使在顶点区域，方向变化超过30度也是折线
                    }
                }
            }
        }
        
        trajectoryPoints.push_back(pos);
        
        // 计算到目标的距离
        float deltaX = pos.x - targetPos.x;
        float deltaY = pos.y - targetPos.y;
        float dist = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        
        if (dist < closestDist) {
            closestDist = dist;
            timeToTarget = currentTime;
        }
        
        // 不要提前结束验证，确保检查完整轨迹以避免检测与渲染不一致
        // 只在飞出屏幕时停止（与generateTrajectoryLine保持一致）
        if (pos.y > static_cast<float>(config::kWindowHeight) + 200.0f) {
            break;
        }
        
        // 如果轨迹已经飞过目标且继续远离，可以停止
        // 但需要确保已经检查了足够长的轨迹
        if (step > 100 && pos.x > targetPos.x && dist > closestDist * 2.0f) {
            break;
        }
    }
    
    // 严格验证：最近距离必须足够接近目标才算有效
    // 目标大小约30像素，所以有效判定距离应该更小（30-50像素）
    // 使用更严格的标准：最近距离应该小于目标距离的1.1倍，且绝对距离不超过50像素
    float maxValidDist = std::min(targetDist * 1.0f, 10.0f);
    bool isValid = closestDist < maxValidDist;
    
    // 额外检查：如果最近距离仍然很大（超过60像素），说明轨迹根本没有接近目标，应该拒绝
    if (closestDist > 60.0f) {
        isValid = false;
    }
    
    return isValid;
}

// 生成轨迹线（用于可视化）
void AIController::generateTrajectoryLine(const sf::Vector2f& slingshotPos) {
    aimTrajectory_.clear();
    
    if (!currentAim_.isValid || !currentTarget_.has_value()) return;
    
    // 计算初始速度（使用与calculateTrajectory相同的逻辑）
    // currentAim_.pull 已经在 calculateTrajectory 中考虑了速度限制和2倍速度（对于黄鸟）
    // 为了保持一致，这里直接使用 pull 计算速度，不再重复限制
    sf::Vector2f v0 = currentAim_.pull * config::kSlingshotStiffness;
    
    // 模拟轨迹
    sf::Vector2f pos = slingshotPos;
    sf::Vector2f vel = v0;
    const int steps = 1500;  // 增加步数以补偿更小的时间步长
    const float stepDt = 0.01f;  // 减小步长以提高模拟精度，避免轨迹突变
    
    sf::Vector2f targetPos = currentTarget_->position;
    float minX = std::min(slingshotPos.x, targetPos.x);
    float maxX = std::max(slingshotPos.x, targetPos.x);
    
    bool trajectoryLineValid = true;
    
    // 用于检测速度突变（速度强制缩放导致的折线）
    sf::Vector2f prevVel = v0;
    
    for (int i = 0; i < steps; ++i) {
        // 应用重力和空气阻力
        vel.y += config::kGravity * stepDt;
        
        float currentSpeed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        if (currentSpeed > 0.001f) {
            // kAirResistanceAccel已经是负值（-0.25 m/s²），表示阻力
            // 直接乘以速度方向即可得到与速度方向相反的阻力加速度
            const float airResistanceAccelPixels = config::kAirResistanceAccel * config::kPixelsPerMeter;
            sf::Vector2f resistanceDir(vel.x / currentSpeed, vel.y / currentSpeed);
            sf::Vector2f resistanceAccel = resistanceDir * airResistanceAccelPixels;
            vel += resistanceAccel * stepDt;
        }
        
        // 重新计算currentSpeed（可能在应用阻力后发生变化）
        currentSpeed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        
        // 检测速度突变：如果速度向量的大小或方向发生突然变化（可能是速度限制导致的）
        // 正常的物理过程（重力和阻力）应该导致速度平滑变化
        float prevSpeed = std::sqrt(prevVel.x * prevVel.x + prevVel.y * prevVel.y);
        float speedChangeRatio = (prevSpeed > 0.001f) ? (currentSpeed / prevSpeed) : 1.0f;
        // 如果速度突然减小超过15%（可能是速度限制导致的），检查方向变化
        if (speedChangeRatio < 0.85f && prevSpeed > 0.001f && currentSpeed > 0.001f) {
            // 计算速度方向的变化
            sf::Vector2f prevDir = prevVel / prevSpeed;
            sf::Vector2f currDir = vel / currentSpeed;
            float dirDot = prevDir.x * currDir.x + prevDir.y * currDir.y;
            // 如果速度方向和大小都发生了显著变化，可能是速度限制导致的折线
            if (dirDot < 0.98f) {  // 方向变化超过约12度，且速度突然减小
                // 这是速度强制缩放导致的折线，标记为无效
                trajectoryLineValid = false;
                break;
            }
        }
        
        // 在更新位置之前，预测下一个位置是否会在X坐标范围内
        // 如果超出范围，不更新位置，直接标记为无效并break（避免产生截断导致的折线）
        sf::Vector2f nextPos = pos + vel * stepDt;
        if (nextPos.x < minX || nextPos.x > maxX) {
            // 超出X坐标范围，轨迹不合法
            // 不在超出范围的点处截断，直接标记为无效
            trajectoryLineValid = false;
            break;
        }
        
        // 更新位置
        pos = nextPos;
        
        // 更新prevVel用于下一帧检测
        prevVel = vel;
        
        // 检查折线（不可导点检测）- 检测轨迹点的尖锐拐点
        // 折线是指轨迹在某一点处不可导，即相邻线段的方向发生突然变化
        if (aimTrajectory_.size() >= 2) {
            sf::Vector2f p0 = aimTrajectory_[aimTrajectory_.size() - 2].position;
            sf::Vector2f p1 = aimTrajectory_.back().position;
            sf::Vector2f p2 = pos;
            
            // 计算两个相邻线段的方向向量
            sf::Vector2f v1 = p1 - p0;  // 前一段的方向
            sf::Vector2f v2 = p2 - p1;  // 后一段的方向
            
            float len1 = std::sqrt(v1.x * v1.x + v1.y * v1.y);
            float len2 = std::sqrt(v2.x * v2.x + v2.y * v2.y);
            
            if (len1 > 0.1f && len2 > 0.1f) {
                // 归一化方向向量
                v1.x /= len1;
                v1.y /= len1;
                v2.x /= len2;
                v2.y /= len2;
                
                // 计算两个方向向量的点积（cos(夹角)）
                float dot = v1.x * v2.x + v1.y * v2.y;
                dot = std::max(-1.0f, std::min(1.0f, dot));
                
                // 检测折线（不可导点）：如果方向变化超过60度（dot < 0.5），认为是折线
                // 但在顶点区域需要更严格的检测，避免真正的折线被当作正常顶点
                // 检查是否在顶点区域（vel.y接近0或从负变正）
                bool isNearVertex = std::abs(vel.y) < 50.0f || (prevVel.y < 0.0f && vel.y > 0.0f);
                
                if (dot < 0.5f) {
                    // 如果不在顶点区域，直接判定为折线
                    if (!isNearVertex) {
                        trajectoryLineValid = false;
                        break;  // 检测到折线（不可导点，方向变化超过60度）
                    }
                    // 在顶点区域，使用更严格的阈值（30度）来检测真正的折线
                    if (dot < 0.866f) {  // cos(30°) ≈ 0.866
                        trajectoryLineValid = false;
                        break;  // 即使在顶点区域，方向变化超过30度也是折线
                    }
                }
            }
        }
        
        // 添加到轨迹线
        float alpha = 200.0f * (1.0f - static_cast<float>(i) / steps);
        aimTrajectory_.emplace_back(pos, sf::Color(255, 255, 0, static_cast<unsigned char>(alpha)));
        
        // 与validateTrajectoryWithTime保持一致的终止条件
        // 如果轨迹已经飞过目标且继续远离，可以停止
        float deltaX = pos.x - targetPos.x;
        float deltaY = pos.y - targetPos.y;
        float distToTarget = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        if (i > 100 && pos.x > targetPos.x && distToTarget > 500.0f) {
            break;
        }
        
        if (pos.y > static_cast<float>(config::kWindowHeight) + 200.0f) {
            break;
        }
    }
    
    // 如果轨迹线无效，清除并标记
    if (!trajectoryLineValid) {
        aimTrajectory_.clear();
        currentAim_.isValid = false;
        Logger::getInstance().warning("轨迹线生成失败 - 检测到折线或超出范围");
    } else if (!aimTrajectory_.empty()) {
        // 添加目标点标记
        aimTrajectory_.emplace_back(targetPos, sf::Color(255, 255, 0, 100));
    }
}

// 其他辅助函数（保持兼容性）
bool AIController::simulateTrajectory(const sf::Vector2f& startPos,
                                     const sf::Vector2f& initialVelocity,
                                     const sf::Vector2f& targetPos,
                                     float tolerance) const {
    float closestDist;
    bool isValid = validateTrajectory(startPos, targetPos, initialVelocity, closestDist);
    return isValid && closestDist < tolerance;
}

sf::Vector2f AIController::calculateInitialVelocity(const sf::Vector2f& startPos,
                                                   const sf::Vector2f& targetPos,
                                                   BirdType birdType) const {
    float dx = targetPos.x - startPos.x;
    float dy = targetPos.y - startPos.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    
    float maxSpeed = config::bird_speed::kRedInitialMax;
    switch (birdType) {
        case BirdType::Red:
            maxSpeed = config::bird_speed::kRedInitialMax;
            break;
        case BirdType::Yellow:
            maxSpeed = config::bird_speed::kYellowInitialMax;
            break;
        case BirdType::Bomb:
            maxSpeed = config::bird_speed::kBombInitialMax;
            break;
    }
    
        float estimatedTime = distance / (maxSpeed * 0.7f);
        float v0x = dx / estimatedTime;
    float v0y = (dy + 0.5f * config::kGravity * estimatedTime * estimatedTime) / estimatedTime;
        
        float speed = std::sqrt(v0x * v0x + v0y * v0y);
        if (speed > maxSpeed) {
            float scale = maxSpeed / speed;
            v0x *= scale;
            v0y *= scale;
        }
        
    return sf::Vector2f(v0x, v0y);
}

float AIController::calculateYellowBirdSkillTime(const sf::Vector2f& slingshotPos,
                                                const sf::Vector2f& targetPos,
                                                const sf::Vector2f& initialVelocity) const {
    return 0.0f;  // 立即激活
}

sf::Vector2f AIController::calculateBombExplosionPosition(const sf::Vector2f& targetPos,
                                                          const std::vector<std::unique_ptr<Block>>& blocks) const {
    return targetPos;  // 在目标位置爆炸
}

void AIController::render(sf::RenderWindow& window) const {
    if (!enabled_) return;
    
    // 渲染瞄准轨迹线
    if (!aimTrajectory_.empty()) {
        window.draw(aimTrajectory_.data(), aimTrajectory_.size(), sf::PrimitiveType::LineStrip);
    }
    
    // 渲染目标标记
    if (currentTarget_.has_value()) {
        // 创建临时副本以设置位置（因为const方法）
        sf::CircleShape marker = targetMarker_;
        marker.setPosition(currentTarget_->position);
        window.draw(marker);
    }
}

