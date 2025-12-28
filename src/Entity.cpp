// Implementations for game entities using Box2D physics.
#include "Entity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "Config.hpp"

namespace {
constexpr float kSpawnInvincibleTime = 2.5f;  // seconds
}

Block::Block(const Material& material, const sf::Vector2f& pos, const sf::Vector2f& size, PhysicsWorld& world)
    : material_(material) {
    body_ = world.createBoxBody(pos, size, material_.density, material_.friction, material_.restitution,
                                 true, false, false, this);

    // Set HP based on material strength (higher strength = more HP)
    // 使用全局系数 config::kBlockHpFactor 统一控制所有建筑物血量。
    // 示例：0.5f -> 原始血量；0.75f -> 在原始基础上整体 +50%。
    maxHp_ = static_cast<int>(material_.strength * config::kBlockHpFactor);  // Convert strength to HP
    hp_ = maxHp_;

    shape_.setSize(size);
    shape_.setOrigin(size * 0.5f);  // Center origin matches Box2D body center
    auto fill = material_.color;
    fill.a = static_cast<std::uint8_t>(material_.opacity * 255);
    shape_.setFillColor(fill);
    // CRITICAL FIX: Use body position instead of initial pos to ensure visual matches collision box
    shape_.setPosition(body_.position());
    shape_.setRotation(sf::radians(body_.angle()));  // SFML 3.0 uses sf::radians()
    shape_.setOutlineColor(sf::Color::Black);
    shape_.setOutlineThickness(1.0f);
}

void Block::update(float dt) {
    age_ += dt;
    if (!body_.active()) {
        destroyed_ = true;
        return;
    }
    // CRITICAL FIX: Always sync visual position and rotation with physics body
    shape_.setPosition(body_.position());
    shape_.setRotation(sf::radians(body_.angle()));  // SFML 3.0 uses sf::radians()
    
    // Update damage flash effect
    if (damageFlash_ > 0.0f) {
        damageFlash_ -= dt;
        // Update visual color during flash
        float healthRatio = static_cast<float>(hp_) / static_cast<float>(maxHp_);
        auto baseColor = material_.color;
        float flashIntensity = damageFlash_ / 0.2f;
        shape_.setFillColor(sf::Color(
            static_cast<std::uint8_t>(baseColor.r + (255 - baseColor.r) * flashIntensity * 0.5f),
            static_cast<std::uint8_t>(baseColor.g * (1.0f - flashIntensity * 0.3f)),
            static_cast<std::uint8_t>(baseColor.b * (1.0f - flashIntensity * 0.3f)),
            static_cast<std::uint8_t>(material_.opacity * 255)
        ));
    } else if (hp_ < maxHp_) {
        // Update visual color based on health (when not flashing)
        float healthRatio = static_cast<float>(hp_) / static_cast<float>(maxHp_);
        auto baseColor = material_.color;
        shape_.setFillColor(sf::Color(
            static_cast<std::uint8_t>(baseColor.r * (0.5f + 0.5f * healthRatio)),
            static_cast<std::uint8_t>(baseColor.g * (0.5f + 0.5f * healthRatio)),
            static_cast<std::uint8_t>(baseColor.b * (0.5f + 0.5f * healthRatio)),
            static_cast<std::uint8_t>(material_.opacity * 255)
        ));
    }
    
    // Apply damage from hitStrength (legacy system for bird impacts)
    if (age_ >= kSpawnInvincibleTime && body_.hitStrength() > material_.strength) {
        body_.setActive(false);
        destroyed_ = true;
    }
    
    // Check if block is destroyed due to health
    if (hp_ <= 0) {
        body_.setActive(false);
        destroyed_ = true;
    }
}

void Block::takeDamage(float damage) {
    if (age_ < kSpawnInvincibleTime) return;  // Invincible during spawn
    
    hp_ -= static_cast<int>(damage);
    hp_ = std::max(0, hp_);
    damageFlash_ = 0.2f;  // Flash for 0.2 seconds
    // Visual update will happen in update() method
}

void Block::draw(sf::RenderWindow& window) {
    if (!destroyed_) {
        window.draw(shape_);
    }
}

Pig::Pig(PigType type, const sf::Vector2f& pos, PhysicsWorld& world) : type_(type) {
    radius_ = (type == PigType::Large) ? 26.f : (type == PigType::Medium ? 20.f : 16.f);
    body_ = world.createCircleBody(pos, radius_, 1.5f, 0.8f, 0.2f, true, false, false, this);

    // 基础血量来自 Config，并通过系数统一提升。
    int baseHp = (type == PigType::Large)
                     ? config::kPigHpLargeBase
                     : (type == PigType::Medium ? config::kPigHpMediumBase : config::kPigHpSmallBase);
    maxHp_ = static_cast<int>(baseHp * config::kPigHpFactor);  // 整体提升 30%
    hp_ = maxHp_;
    
    // 加载贴图
    loadTextures();
    updateVisuals();
    
    // CRITICAL FIX: Use body position instead of initial pos to ensure visual matches collision box
    if (sprite_.has_value()) {
        sprite_->setPosition(body_.position());
        currentRotation_ = body_.angle();
        sprite_->setRotation(sf::radians(currentRotation_));  // SFML 3.0 uses sf::radians()
    }
}

void Pig::update(float dt) {
    age_ += dt;
    if (!body_.active()) {
        destroyed_ = true;
        return;
    }
    
    // Apply air resistance: -0.25 m/s^2 acceleration in opposite direction of velocity
    if (body_.active() && body_.body_) {
        b2Vec2 vel = body_.body_->GetLinearVelocity();
        float speed = vel.Length();
        if (speed > 0.001f) {  // Only apply if pig is moving
            // Air resistance acceleration: -0.25 m/s^2 in opposite direction of velocity
            const float airResistanceAccel = config::kAirResistanceAccel;  // m/s^2
            b2Vec2 resistanceDir = (1.0f / speed) * vel;  // Normalized direction
            b2Vec2 resistanceAccel = airResistanceAccel * resistanceDir;
            // Apply acceleration as impulse: F = ma, impulse = F * dt = m * a * dt
            // Box2D requires float * b2Vec2, not b2Vec2 * float
            float mass = body_.body_->GetMass();
            b2Vec2 resistanceImpulse = (mass * dt) * resistanceAccel;
            body_.body_->ApplyLinearImpulse(resistanceImpulse, body_.body_->GetWorldCenter(), true);
        }
    }
    
    // CRITICAL FIX: Always sync visual position and rotation with physics body
    if (sprite_.has_value()) {
        sprite_->setPosition(body_.position());
        currentRotation_ = body_.angle();  // 保存旋转角度
        sprite_->setRotation(sf::radians(currentRotation_));  // SFML 3.0 uses sf::radians()
    }
    
    // Update damage flash effect and texture if health changed
    bool healthChanged = false;
    if (damageFlash_ > 0.0f) {
        damageFlash_ -= dt;
        updateVisuals();
    }
    
    // Legacy damage system for bird impacts (keep for compatibility)
    float damage = body_.hitStrength() * 0.1f;
    if (age_ >= kSpawnInvincibleTime && damage > 1.0f) {
        int oldHp = hp_;
        hp_ -= static_cast<int>(damage);
        hp_ = std::max(0, hp_);
        healthChanged = (oldHp != hp_);
        // hitStrength is cleared by PhysicsWorld each frame, so we don't need to reset it
        updateVisuals();
        if (hp_ <= 0) {
            body_.setActive(false);
            destroyed_ = true;
        }
    }
    
    // Check if pig is destroyed due to health
    if (hp_ <= 0) {
        body_.setActive(false);
        destroyed_ = true;
    }
    
    // Update texture if health level changed
    if (healthChanged) {
        updateVisuals();
    }
}

void Pig::takeDamage(float damage) {
    if (age_ < kSpawnInvincibleTime) return;  // Invincible during spawn
    
    hp_ -= static_cast<int>(damage);
    hp_ = std::max(0, hp_);
    damageFlash_ = 0.2f;  // Flash for 0.2 seconds
    updateVisuals();
}

void Pig::loadTextures() {
    textures_.clear();
    textures_.resize(4);  // 4个健康等级：100%, 75%, 50%, 25%
    
    // 加载4个健康等级的贴图
    std::vector<std::string> texturePaths = {
        "image/pig_nor_100.png",
        "image/pig_nor_75.png",
        "image/pig_nor_50.png",
        "image/pig_nor_25.png"
    };
    
    bool textureLoaded = false;
    for (size_t i = 0; i < textures_.size(); ++i) {
        if (!textures_[i].loadFromFile(texturePaths[i])) {
            std::cerr << "警告: 无法加载猪贴图: " << texturePaths[i] << "\n";
            // 如果加载失败，创建一个简单的占位符（可选）
        } else if (!textureLoaded && i == 0) {
            textureLoaded = true;
        }
    }
    
    // 如果至少有一个贴图加载成功，设置sprite
    if (textureLoaded && !textures_.empty() && textures_[0].getSize().x > 0) {
        // 初始化sprite_使用第一个贴图（SFML 3.0要求使用纹理构造）
        sprite_ = sf::Sprite(textures_[0]);
        sf::Vector2u textureSize = textures_[0].getSize();
        sprite_->setOrigin(sf::Vector2f(textureSize.x * 0.5f, textureSize.y * 0.5f));
        float targetSize = radius_ * 2.0f;
        float scale = targetSize / static_cast<float>(std::max(textureSize.x, textureSize.y));
        sprite_->setScale(sf::Vector2f(scale, scale));
    }
}

void Pig::updateVisuals() {
    // 根据血量确定贴图索引
    float ratio = std::max(0.0f, static_cast<float>(hp_) / static_cast<float>(maxHp_));
    int newTextureIndex = 0;
    
    if (ratio > 0.75f) {
        newTextureIndex = 0;  // 100%
    } else if (ratio > 0.5f) {
        newTextureIndex = 1;  // 75%
    } else if (ratio > 0.25f) {
        newTextureIndex = 2;  // 50%
    } else {
        newTextureIndex = 3;  // 25%
    }
    
    // 如果贴图索引改变，更新贴图
    if (newTextureIndex != currentTextureIndex_ && newTextureIndex < static_cast<int>(textures_.size()) && 
        textures_[newTextureIndex].getSize().x > 0) {
        currentTextureIndex_ = newTextureIndex;
        // 创建新的sprite（SFML 3.0要求使用纹理构造）
        sprite_ = sf::Sprite(textures_[currentTextureIndex_]);
        
        // 设置贴图原点为中心
        sf::Vector2u textureSize = textures_[currentTextureIndex_].getSize();
        sprite_->setOrigin(sf::Vector2f(textureSize.x * 0.5f, textureSize.y * 0.5f));
        
        // 根据猪的类型缩放贴图，确保和当前显示大小差不多
        // 当前半径：Small=16, Medium=20, Large=26
        // 假设贴图原始大小约为32x32，需要缩放到radius_*2
        float targetSize = radius_ * 2.0f;
        float scale = targetSize / static_cast<float>(std::max(textureSize.x, textureSize.y));
        sprite_->setScale(sf::Vector2f(scale, scale));
        
        // 恢复旋转角度（因为更换贴图时会重置）
        sprite_->setRotation(sf::radians(currentRotation_));
    }
    
    // 受伤闪烁效果（可选，通过颜色调制实现）
    if (sprite_.has_value() && damageFlash_ > 0.0f) {
        float flashIntensity = damageFlash_ / 0.2f;
        // 在受伤时稍微变红
        sprite_->setColor(sf::Color(
            255,
            static_cast<std::uint8_t>(255 * (1.0f - flashIntensity * 0.3f)),
            static_cast<std::uint8_t>(255 * (1.0f - flashIntensity * 0.3f))
        ));
    } else if (sprite_.has_value()) {
        sprite_->setColor(sf::Color::White);
    }
}

void Pig::draw(sf::RenderWindow& window) {
    if (!destroyed_ && sprite_.has_value()) {
        window.draw(*sprite_);
    }
}

Bird::Bird(BirdType type, const sf::Vector2f& pos, PhysicsWorld& world) : type_(type), world_(&world) {
    radius_ = 14.f;
    // Birds start as static (not dynamic) until launched
    body_ = world.createCircleBody(pos, radius_, 1.0f, 0.5f, 0.4f, false, true, false, this);

    // Set per-bird max speed limits
    switch (type) {
        case BirdType::Red:
            maxSpeed_ = config::bird_speed::kRedMaxSpeed;
            break;
        case BirdType::Yellow:
            maxSpeed_ = config::bird_speed::kYellowMaxSpeed;  // High speed limit for acceleration
            break;
        case BirdType::Bomb:
            maxSpeed_ = config::bird_speed::kBombMaxSpeed;
            break;
    }

    // 加载贴图（必须在设置位置和旋转之前调用）
    loadTexture();
    
    // CRITICAL FIX: Use body position instead of initial pos to ensure visual matches collision box
    if (sprite_.has_value()) {
        sprite_->setPosition(body_.position());
        sprite_->setRotation(sf::radians(body_.angle()));  // SFML 3.0 uses sf::radians()
    }
}

void Bird::launch(const sf::Vector2f& impulse) {
    body_.setDynamic(true);
    
    // Clamp initial launch speed based on bird type
    sf::Vector2f clampedImpulse = impulse;
    float speedSq = impulse.x * impulse.x + impulse.y * impulse.y;
    float speed = std::sqrt(speedSq);
    
    float initialMaxSpeed = config::kMaxBodySpeed;
    switch (type_) {
        case BirdType::Red:
            initialMaxSpeed = config::bird_speed::kRedInitialMax;
            break;
        case BirdType::Yellow:
            // 黄鸟假设技能立即激活，使用2倍速度限制（与AI轨迹计算一致）
            // 直接使用2倍速度，不截断
            initialMaxSpeed = config::bird_speed::kYellowInitialMax * 2.0f;
            // 如果2倍速度超过上限，则使用上限（但通常不应该发生）
            if (initialMaxSpeed > config::bird_speed::kYellowMaxSpeed) {
                initialMaxSpeed = config::bird_speed::kYellowMaxSpeed;
            }
            break;
        case BirdType::Bomb:
            initialMaxSpeed = config::bird_speed::kBombInitialMax;
            break;
    }
    
    if (speed > initialMaxSpeed) {
        clampedImpulse = impulse * (initialMaxSpeed / speed);
    }
    
    body_.setVelocity(clampedImpulse);
    launched_ = true;
}

void Bird::activateSkill() {
    // Red bird can use skill repeatedly, others only once
    if (type_ != BirdType::Red && skillUsed_) return;
    // For bomb bird, allow skill activation even if not launched yet
    // For yellow bird, allow skill activation if launched (even if body is not active yet)
    if (type_ != BirdType::Bomb && type_ != BirdType::Yellow && !body_.active()) return;
    // For yellow bird, must be launched to use skill
    if (type_ == BirdType::Yellow && !launched_) return;
    
    switch (type_) {
        case BirdType::Red: {
            // Increase mass by 200%
            if (body_.body_) {
                float currentMass = body_.body_->GetMass();
                body_.setMass(currentMass * 2.0f, 1.0f);
            }
            break;
        }
        case BirdType::Yellow: {
            // Yellow bird: double speed when skill is activated
            // Can accelerate to maxSpeed_ (which is high for Yellow bird)
            // Can be used at any speed, immediately after launch
            if (launched_) {
                sf::Vector2f v = body_.velocity();
                float speedSq = v.x * v.x + v.y * v.y;
                float speed = std::sqrt(std::max(0.01f, speedSq));  // Avoid division by zero
                
                // Calculate new speed: double current speed, but cap at maxSpeed_
                float newSpeed = std::min(speed * 2.0f, maxSpeed_);
                
                // Apply new velocity, preserving direction
                if (speed > 0.01f) {
                    // Normal case: bird is moving, accelerate in current direction
                    body_.setVelocity(v * (newSpeed / speed));
                } else {
                    // Edge case: bird is stationary, give it a push in default direction
                    sf::Vector2f defaultDir(1.0f, 0.0f);
                    body_.setVelocity(defaultDir * (maxSpeed_ * 0.3f));
                }
            }
            break;
        }
        case BirdType::Bomb:
            // Set to -1.0f to mark skill as activated, will start countdown when landed
            // If already landed (speed < 2.0), will start countdown immediately in update()
            explosionTimer_ = -1.0f;
            break;
    }
    // Only mark as used for one-time skills (not Red bird)
    if (type_ != BirdType::Red) {
        skillUsed_ = true;
    }
}

void Bird::update(float dt) {
    // Apply air resistance: -0.1 m/s^2 acceleration in opposite direction of velocity
    if (body_.active() && launched_ && body_.body_) {
        b2Vec2 vel = body_.body_->GetLinearVelocity();
        float speed = vel.Length();
        if (speed > 0.001f) {  // Only apply if bird is moving
            // Air resistance acceleration: -0.25 m/s^2 in opposite direction of velocity
            const float airResistanceAccel = config::kAirResistanceAccel;  // m/s^2
            b2Vec2 resistanceDir = (1.0f / speed) * vel;  // Normalized direction
            b2Vec2 resistanceAccel = airResistanceAccel * resistanceDir;
            // Apply acceleration as impulse: F = ma, impulse = F * dt = m * a * dt
            // Box2D requires float * b2Vec2, not b2Vec2 * float
            float mass = body_.body_->GetMass();
            b2Vec2 resistanceImpulse = (mass * dt) * resistanceAccel;
            body_.body_->ApplyLinearImpulse(resistanceImpulse, body_.body_->GetWorldCenter(), true);
        }
    }
    
    // Clamp bird velocity to per-bird max speed
    if (body_.active() && launched_ && body_.body_) {
        b2Vec2 vel = body_.body_->GetLinearVelocity();
        float speedSq = vel.x * vel.x + vel.y * vel.y;
        float maxSpeedMeters = PhysicsWorld::pixelToMeter(maxSpeed_);
        if (speedSq > maxSpeedMeters * maxSpeedMeters) {
            float speed = std::sqrt(speedSq);
            body_.body_->SetLinearVelocity((maxSpeedMeters / speed) * vel);
        }
    }
    
    // Despawn logic when bird has fully come to rest.
    // For bomb birds that haven't exploded yet, we skip this so
    // that they can properly trigger their explosion on landing.
    if (body_.active() && launched_) {
        bool waitingForBombExplosion = (type_ == BirdType::Bomb && !exploded_);
        if (!waitingForBombExplosion) {
            sf::Vector2f vel = body_.velocity();
            float speedSq = vel.x * vel.x + vel.y * vel.y;
            const float restSpeedSq = 20.0f * 20.0f;
            if (speedSq < restSpeedSq) {
                restTimer_ += dt;
            } else {
                restTimer_ = 0.0f;
            }
            if (restTimer_ > 1.0f && !exploded_) {
                body_.setActive(false);
            }
        }
    }

    if (!body_.active()) {
        if (exploded_ && explosionVisualTime_ > 0.0f) {
            explosionVisualTime_ -= dt;
            if (explosionVisualTime_ <= 0.0f) {
                destroyed_ = true;
            }
        } else {
            destroyed_ = true;
        }
        return;
    }
    // CRITICAL FIX: Always sync visual position and rotation with physics body
    if (sprite_.has_value()) {
        sprite_->setPosition(body_.position());
        sprite_->setRotation(sf::radians(body_.angle()));  // SFML 3.0 uses sf::radians()
    }

    // Hard clamp to visual ground so birds never fall through the bottom of the screen
    const float groundTop = static_cast<float>(config::kWindowHeight) - 30.0f;
    sf::Vector2f pos = body_.position();
    if (pos.y > groundTop - radius_) {
        body_.setPosition({pos.x, groundTop - radius_});
        body_.setVelocity({body_.velocity().x, 0.0f});
        if (sprite_.has_value()) {
            sprite_->setPosition(body_.position());
        }
    }

    // Despawn bird when far outside the visible area
    const float margin = 200.0f;
    if (pos.x < -margin ||
        pos.x > static_cast<float>(config::kWindowWidth) + margin ||
        pos.y > static_cast<float>(config::kWindowHeight) + margin) {
        body_.setActive(false);
        destroyed_ = true;
        return;
    }

    // Bomb birds explode after landing (auto) or when skill is activated via right-click (manual)
    // But only if they have been launched first!
    if (type_ == BirdType::Bomb && launched_ && !exploded_ && body_.active()) {
        sf::Vector2f vel = body_.velocity();
        float speedSq = vel.x * vel.x + vel.y * vel.y;
        
        // explosionTimer_ states:
        //   0.0f = not activated, will auto-explode after landing
        //  -1.0f = skill activated (right-click), will explode when landed
        //  >0.0f = countdown in progress
        
        if (explosionTimer_ == -1.0f) {
            // Skill was activated via right-click, start countdown when landed
            if (speedSq < 4.0f) {
                explosionTimer_ = 1.0f;  // landed, start 1 second countdown
            }
        } else if (explosionTimer_ == 0.0f && speedSq < 4.0f) {
            // Auto-explode after landing (if skill wasn't manually activated)
            explosionTimer_ = 1.0f;  // landed, start 1 second countdown
        }
        
        if (explosionTimer_ > 0) {
            explosionTimer_ -= dt;
            if (explosionTimer_ <= 0 && world_) {
                sf::Vector2f bombPos = body_.position();
                b2Body* bombBody = body_.body_;
                for (b2Body* other = world_->world()->GetBodyList(); other; other = other->GetNext()) {
                    if (other == bombBody) continue;
                    sf::Vector2f otherPos = PhysicsWorld::meterToPixel(other->GetPosition());
                    sf::Vector2f delta = otherPos - bombPos;
                    float distSq = delta.x * delta.x + delta.y * delta.y;
                    float radius = 120.0f;
                    if (distSq < radius * radius) {
                        float dist = std::max(4.0f, std::sqrt(distSq));
                        sf::Vector2f dir = delta / dist;
                        // Much stronger explosion power to destroy all materials
                        // Power is in pixels, need to convert to meters for Box2D impulse
                        float powerPixels = 1000000.0f / dist;  // Massive explosion power
                        // Damage calculation:
                        // - For blocks: damage must > material.strength (glass=120, stone=800)
                        // - For pigs: damage * 0.1 must > maxHp (max 50), so damage > 500
                        // Set damage to be much higher than needed to ensure destruction
                        float damage = powerPixels * 2.0f;  // Very high damage multiplier (2M at close range)
                        
                        // Apply impulse to dynamic bodies - convert pixel force to Box2D impulse
                        // Box2D uses kg*m/s for impulse, so we need to scale by pixels-per-meter
                        if (other->GetType() == b2_dynamicBody) {
                            // Convert pixel force to Box2D impulse (force * time = impulse)
                            // powerPixels is in pixels, convert to meters and apply as impulse
                            b2Vec2 impulseDir = PhysicsWorld::pixelToMeter(dir);
                            // Use larger impulse magnitude for stronger explosion effect
                            float impulseMagnitude = (powerPixels / config::kPixelsPerMeter) * 3.0f;  // Triple the impulse
                            b2Vec2 impulse = impulseMagnitude * impulseDir;
                            other->ApplyLinearImpulse(impulse, other->GetWorldCenter(), true);
                        }
                        
                        // Apply damage to ALL bodies (including static ones) through user data
                        // This is critical - damage must be applied to fixtures for blocks/pigs to take damage
                        // Apply damage to ALL fixtures of the body to ensure it's registered
                        b2Fixture* fixture = other->GetFixtureList();
                        while (fixture) {
                            FixtureUserData* data = reinterpret_cast<FixtureUserData*>(
                                fixture->GetUserData().pointer);
                            if (data) {
                                // Apply massive damage - ensure it exceeds material strength and pig HP
                                // For blocks: damage must > material.strength (max ~800 for stone)
                                // For pigs: damage * 0.1 must > maxHp (max 50), so damage > 500
                                // Set damage to be much higher than needed
                                data->hitStrength = std::max(data->hitStrength, damage);
                            }
                            fixture = fixture->GetNext();
                        }
                    }
                }
                exploded_ = true;
                explosionVisualTime_ = 0.5f;
                body_.setActive(false);
            }
        }
    }
}

void Bird::loadTexture() {
    std::string texturePath;
    switch (type_) {
        case BirdType::Red:
            texturePath = "image/bird_red.png";
            break;
        case BirdType::Yellow:
            texturePath = "image/bird_yellow.png";
            break;
        case BirdType::Bomb:
            texturePath = "image/bird_black.png";
            break;
    }
    
    if (!texture_.loadFromFile(texturePath)) {
        std::cerr << "警告: 无法加载鸟贴图: " << texturePath << "\n";
        return;  // 如果加载失败，sprite将无法使用，但不会崩溃
    }
    
    // 设置贴图（SFML 3.0要求sprite必须有一个纹理）
    sprite_ = sf::Sprite(texture_);
    
    // 设置贴图原点为中心
    sf::Vector2u textureSize = texture_.getSize();
    sprite_->setOrigin(sf::Vector2f(textureSize.x * 0.5f, textureSize.y * 0.5f));
    
    // 缩放贴图以适应半径（假设贴图原始大小约为28x28，需要缩放到radius_*2）
    float targetSize = radius_ * 2.0f;
    float scale = targetSize / static_cast<float>(std::max(textureSize.x, textureSize.y));
    sprite_->setScale(sf::Vector2f(scale, scale));
}

void Bird::draw(sf::RenderWindow& window) {
    if (destroyed_) return;
    // Bomb explosion visual
    if (type_ == BirdType::Bomb && exploded_ && explosionVisualTime_ > 0.0f) {
        float t = explosionVisualTime_ / 0.5f;  // 0..1
        float radius = 120.0f * (1.0f - t);
        sf::CircleShape boom(radius);
        boom.setOrigin({radius, radius});
        boom.setPosition(body_.position());
        sf::Color c(255, 200, 0, static_cast<std::uint8_t>(255 * t));
        boom.setFillColor(sf::Color(255, 200, 0, static_cast<std::uint8_t>(120 * t)));
        boom.setOutlineColor(c);
        boom.setOutlineThickness(4.0f);
        window.draw(boom);
    }
    // 只有在sprite已初始化时才绘制
    if (sprite_.has_value()) {
        window.draw(*sprite_);
    }
}

ScorePopups::ScorePopups(const sf::Font& font) : font_(font) {}

void ScorePopups::spawn(const sf::Vector2f& pos, int points) {
    sf::Text text(font_, "+" + std::to_string(points), 18);
    text.setPosition(pos);
    text.setFillColor(sf::Color::Yellow);
    popups_.push_back({text, 1.0f});
}

void ScorePopups::update(float dt) {
    for (auto& p : popups_) {
        p.life -= dt;
        p.text.move({0.f, -30.f * dt});
        sf::Color c = p.text.getFillColor();
        c.a = static_cast<std::uint8_t>(std::max(0.f, p.life) * 255);
        p.text.setFillColor(c);
    }
    popups_.erase(std::remove_if(popups_.begin(), popups_.end(), [](const Popup& p) { return p.life <= 0; }),
                  popups_.end());
}

void ScorePopups::draw(sf::RenderWindow& window) {
    for (auto& p : popups_) window.draw(p.text);
}
