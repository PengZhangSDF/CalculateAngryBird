// Box2D 2.4.1 physics implementation with GLM 1.0.2 math.
#include "Physics.hpp"

#include <algorithm>
#include <cmath>

#include "Config.hpp"
#include "DamageConfig.hpp"
#include "Entity.hpp"
#include "DamageConfig.hpp"
#include "Entity.hpp"

// ========== PhysicsBody implementation ==========

sf::Vector2f PhysicsBody::position() const {
    if (!body_) return {};
    return PhysicsWorld::meterToPixel(body_->GetPosition());
}

sf::Vector2f PhysicsBody::velocity() const {
    if (!body_) return {};
    return PhysicsWorld::meterToPixel(body_->GetLinearVelocity());
}

float PhysicsBody::angle() const {
    return body_ ? body_->GetAngle() : 0.0f;
}

bool PhysicsBody::active() const {
    return body_ && body_->IsEnabled();
}

bool PhysicsBody::dynamic() const {
    return body_ && body_->GetType() == b2_dynamicBody;
}

float PhysicsBody::hitStrength() const {
    return userData_ ? userData_->hitStrength : 0.0f;
}

bool PhysicsBody::isBird() const {
    return userData_ && userData_->isBird;
}

bool PhysicsBody::environment() const {
    return userData_ && userData_->environment;
}

void PhysicsBody::setPosition(const sf::Vector2f& pos) {
    if (body_) {
        body_->SetTransform(PhysicsWorld::pixelToMeter(pos), body_->GetAngle());
    }
}

void PhysicsBody::setVelocity(const sf::Vector2f& vel) {
    if (body_) {
        body_->SetLinearVelocity(PhysicsWorld::pixelToMeter(vel));
    }
}

void PhysicsBody::setMass(float mass, float density) {
    if (!body_) return;
    b2MassData massData;
    body_->GetMassData(&massData);
    massData.mass = mass * density;
    body_->SetMassData(&massData);
}

void PhysicsBody::applyForce(const sf::Vector2f& force) {
    if (body_) {
        body_->ApplyForce(PhysicsWorld::pixelToMeter(force), body_->GetWorldCenter(), true);
    }
}

void PhysicsBody::applyImpulse(const sf::Vector2f& impulse) {
    if (body_) {
        body_->ApplyLinearImpulse(PhysicsWorld::pixelToMeter(impulse), body_->GetWorldCenter(), true);
    }
}

void PhysicsBody::setActive(bool active) {
    if (body_) {
        body_->SetEnabled(active);
    }
}

void PhysicsBody::setDynamic(bool dynamic) {
    if (!body_) return;
    body_->SetType(dynamic ? b2_dynamicBody : b2_staticBody);
}

// ========== DamageContactListener implementation ==========

void DamageContactListener::BeginContact(b2Contact* contact) {
    // Damage calculation happens in PreSolve for more accurate impact speed
}

void DamageContactListener::PreSolve(b2Contact* contact, const b2Manifold* oldManifold) {
    b2Fixture* fixtureA = contact->GetFixtureA();
    b2Fixture* fixtureB = contact->GetFixtureB();

    FixtureUserData* dataA = reinterpret_cast<FixtureUserData*>(fixtureA->GetUserData().pointer);
    FixtureUserData* dataB = reinterpret_cast<FixtureUserData*>(fixtureB->GetUserData().pointer);

    if (!dataA || !dataB) return;

    b2Body* bodyA = fixtureA->GetBody();
    b2Body* bodyB = fixtureB->GetBody();

    // Check if one body is on the ground (environment)
    bool bodyAOnGround = dataA->environment || dataB->environment;
    bool bodyBOnGround = dataB->environment || dataA->environment;
    
    // Apply ground friction for objects rolling on ground - stronger friction for all speeds
    if (bodyAOnGround && bodyA->GetType() == b2_dynamicBody) {
        b2Vec2 vel = bodyA->GetLinearVelocity();
        float speed = vel.Length();
        if (speed > 0.0f) {
            // Apply stronger friction based on speed
            // Faster objects get less friction, slower objects get more friction
            float frictionFactor = 0.92f;  // Base friction (8% reduction per frame)
            if (speed < PhysicsWorld::pixelToMeter(50.0f)) {
                frictionFactor = 0.85f;  // Stronger friction for slow objects (15% reduction)
            } else if (speed < PhysicsWorld::pixelToMeter(100.0f)) {
                frictionFactor = 0.90f;  // Medium friction (10% reduction)
            }
            bodyA->SetLinearVelocity(frictionFactor * vel);  // Box2D requires float * b2Vec2
        }
    }
    if (bodyBOnGround && bodyB->GetType() == b2_dynamicBody) {
        b2Vec2 vel = bodyB->GetLinearVelocity();
        float speed = vel.Length();
        if (speed > 0.0f) {
            float frictionFactor = 0.92f;  // Base friction
            if (speed < PhysicsWorld::pixelToMeter(50.0f)) {
                frictionFactor = 0.85f;  // Stronger friction for slow objects
            } else if (speed < PhysicsWorld::pixelToMeter(100.0f)) {
                frictionFactor = 0.90f;  // Medium friction
            }
            bodyB->SetLinearVelocity(frictionFactor * vel);  // Box2D requires float * b2Vec2
        }
    }

    // Skip if either is environment or bird (bird damage is handled separately)
    if (dataA->environment || dataB->environment) return;
    // Skip damage if either entity is in editor mode
    if (dataA->isEditorEntity || dataB->isEditorEntity) return;
    if (dataA->isBird || dataB->isBird) {
        // Handle bird collisions (legacy system)
        b2Vec2 velA = bodyA->GetLinearVelocity();
        b2Vec2 velB = bodyB->GetLinearVelocity();
        b2Vec2 relativeVel = velB - velA;
        b2WorldManifold worldManifold;
        contact->GetWorldManifold(&worldManifold);
        b2Vec2 normal = worldManifold.normal;
        float velAlongNormal = b2Dot(relativeVel, normal);
        if (velAlongNormal > 0.0f) return;
        float impactSpeed = -velAlongNormal * config::kPixelsPerMeter;
        float damageSpeed = std::max(0.0f, impactSpeed - 6.0f);
        float impact = damageSpeed * 4.0f;
        if (impact > 0.0f) {
            if (dataA->isBird && !dataB->isBird) {
                dataB->hitStrength = std::max(dataB->hitStrength, impact);
                float slowFactor = 1.0f - std::min(0.8f, impact / 400.0f);
                bodyA->SetLinearVelocity(slowFactor * velA);
            } else if (dataB->isBird && !dataA->isBird) {
                dataA->hitStrength = std::max(dataA->hitStrength, impact);
                float slowFactor = 1.0f - std::min(0.8f, impact / 400.0f);
                bodyB->SetLinearVelocity(slowFactor * velB);
            }
        }
        return;
    }

    // Both are non-bird, non-environment objects (blocks or pigs)
    // Calculate collision damage
    b2Vec2 velA = bodyA->GetLinearVelocity();
    b2Vec2 velB = bodyB->GetLinearVelocity();
    b2Vec2 relativeVel = velB - velA;

    // Get contact normal
    b2WorldManifold worldManifold;
    contact->GetWorldManifold(&worldManifold);
    b2Vec2 normal = worldManifold.normal;

    // Project relative velocity onto normal
    float velAlongNormal = b2Dot(relativeVel, normal);

    // Only process collisions where objects are closing
    if (velAlongNormal > 0.0f) return;

    // Convert to pixel space for damage calculation
    float impactSpeed = -velAlongNormal * config::kPixelsPerMeter;  // m/s -> pixels/s
    
    // Minimum speed threshold for damage
    if (impactSpeed < SpeedThreshold::kMinDamageSpeed) return;

    // Get entity pointers
    Block* blockA = nullptr;
    Block* blockB = nullptr;
    Pig* pigA = nullptr;
    Pig* pigB = nullptr;
    
    if (dataA->entityPtr) {
        blockA = dynamic_cast<Block*>(static_cast<Entity*>(dataA->entityPtr));
        if (!blockA) pigA = dynamic_cast<Pig*>(static_cast<Entity*>(dataA->entityPtr));
    }
    if (dataB->entityPtr) {
        blockB = dynamic_cast<Block*>(static_cast<Entity*>(dataB->entityPtr));
        if (!blockB) pigB = dynamic_cast<Pig*>(static_cast<Entity*>(dataB->entityPtr));
    }

    // Determine collision type and calculate damage
    if (blockA && blockB) {
        // Block-to-block collision
        std::string materialA = blockA->material().name;
        std::string materialB = blockB->material().name;
        
        float strengthA = getMaterialStrength(materialA);
        float strengthB = getMaterialStrength(materialB);
        float multiplierA = getDamageMultiplier(materialA);
        float multiplierB = getDamageMultiplier(materialB);
        float speedMultiplier = getSpeedDamageMultiplier(impactSpeed);
        
        float baseDamage = BaseDamage::kBlockToBlock * speedMultiplier;
        
        if (materialA == materialB) {
            // Same material: both take damage
            float damageA = baseDamage * multiplierB;
            float damageB = baseDamage * multiplierA;
            blockA->takeDamage(damageA);
            blockB->takeDamage(damageB);
        } else {
            // Different materials: only weaker one takes damage
            if (strengthA < strengthB) {
                float damage = baseDamage * multiplierB;
                blockA->takeDamage(damage);
            } else if (strengthB < strengthA) {
                float damage = baseDamage * multiplierA;
                blockB->takeDamage(damage);
            }
        }
    } else if (blockA && pigB) {
        // Block-to-pig collision
        std::string materialA = blockA->material().name;
        float multiplierA = getDamageMultiplier(materialA);
        float speedMultiplier = getSpeedDamageMultiplier(impactSpeed);
        
        // Special case: glass never kills pigs
        if (materialA == "glass") {
            float damage = BaseDamage::kBlockToPig * speedMultiplier * multiplierA * 0.3f;  // Reduced damage
            pigB->takeDamage(damage);
        } else {
            float damage = BaseDamage::kBlockToPig * speedMultiplier * multiplierA;
            pigB->takeDamage(damage);
            // Block also takes damage from pig
            float blockDamage = BaseDamage::kBlockToPig * speedMultiplier * DamageMultiplier::kPig * 0.5f;
            blockA->takeDamage(blockDamage);
        }
    } else if (blockB && pigA) {
        // Pig-to-block collision (same as block-to-pig)
        std::string materialB = blockB->material().name;
        float multiplierB = getDamageMultiplier(materialB);
        float speedMultiplier = getSpeedDamageMultiplier(impactSpeed);
        
        // Special case: glass never kills pigs
        if (materialB == "glass") {
            float damage = BaseDamage::kBlockToPig * speedMultiplier * multiplierB * 0.3f;  // Reduced damage
            pigA->takeDamage(damage);
        } else {
            float damage = BaseDamage::kBlockToPig * speedMultiplier * multiplierB;
            pigA->takeDamage(damage);
            // Block also takes damage from pig
            float blockDamage = BaseDamage::kBlockToPig * speedMultiplier * DamageMultiplier::kPig * 0.5f;
            blockB->takeDamage(blockDamage);
        }
    } else if (pigA && pigB) {
        // Pig-to-pig collision
        float speedMultiplier = getSpeedDamageMultiplier(impactSpeed);
        float damage = BaseDamage::kPigToPig * speedMultiplier;
        pigA->takeDamage(damage);
        pigB->takeDamage(damage);
    }
}

// ========== PhysicsWorld implementation ==========

PhysicsWorld::PhysicsWorld(const sf::Vector2f& gravity)
    : world_(std::make_unique<b2World>(pixelToMeter(gravity))),
      contactListener_(std::make_unique<DamageContactListener>()) {
    world_->SetContactListener(contactListener_.get());
    // Box2D default settings are good, but we can tune if needed
    world_->SetContinuousPhysics(true);  // Better collision detection for fast objects
}

PhysicsWorld::~PhysicsWorld() = default;

PhysicsBody PhysicsWorld::createBoxBody(const sf::Vector2f& pos, const sf::Vector2f& size,
                                         float density, float friction, float restitution,
                                         bool isDynamic, bool isBird, bool isEnvironment,
                                         void* entityPtr, bool isEditorEntity) {
    // Create body definition
    b2BodyDef bodyDef;
    bodyDef.type = isDynamic ? b2_dynamicBody : b2_staticBody;
    bodyDef.position = pixelToMeter(pos);
    bodyDef.angle = 0.0f;
    // Minimal damping - ground friction is handled in contact listener
    if (isDynamic) {
        bodyDef.linearDamping = 0.0f;  // No global damping - let physics work naturally
        bodyDef.angularDamping = 0.0f;  // No global angular damping
    }
    b2Body* body = world_->CreateBody(&bodyDef);

    // Create box shape (half extents in meters)
    // Ensure collision box matches visual shape exactly
    // CRITICAL FIX: For stoneslab (long thin rectangles), ensure precise collision boundaries
    b2PolygonShape boxShape;
    b2Vec2 halfSize = pixelToMeter(size * 0.5f);
    
    // For very thin or long objects (like stoneslab), use explicit vertex definition
    // to ensure collision boundaries match visual boundaries exactly
    // This prevents penetration at the ends of long rectangular blocks
    // Box2D requires minimum area (about 1.19e-7 m^2), which is about 1 meter (30 pixels) minimum
    const float minHalfSize = pixelToMeter(15.0f);  // Minimum 30 pixels total = 15 pixels half-size
    halfSize.x = std::max(minHalfSize, halfSize.x);
    halfSize.y = std::max(minHalfSize, halfSize.y);
    
    if (halfSize.x > 0.0f && halfSize.y > 0.0f) {
        // Use explicit vertices to ensure precise collision boundaries
        // This is especially important for long thin blocks like stoneslab
        b2Vec2 vertices[4];
        vertices[0].Set(-halfSize.x, -halfSize.y);  // Bottom-left
        vertices[1].Set(halfSize.x, -halfSize.y);   // Bottom-right
        vertices[2].Set(halfSize.x, halfSize.y);     // Top-right
        vertices[3].Set(-halfSize.x, halfSize.y);   // Top-left
        boxShape.Set(vertices, 4);
    } else {
        // Fallback to SetAsBox for safety
        boxShape.SetAsBox(halfSize.x, halfSize.y, b2Vec2_zero, 0.0f);
    }

    // Create fixture with improved collision detection
    b2FixtureDef fixtureDef;
    fixtureDef.shape = &boxShape;
    fixtureDef.density = density;
    fixtureDef.friction = friction;
    fixtureDef.restitution = restitution;
    // Enable better collision detection for thin/long objects
    fixtureDef.isSensor = false;
    b2Fixture* fixture = body->CreateFixture(&fixtureDef);

    // Create and attach user data
    auto userData = std::make_unique<FixtureUserData>();
    userData->isBird = isBird;
    userData->environment = isEnvironment;
    userData->isEditorEntity = isEditorEntity;
    userData->entityPtr = entityPtr;
    fixture->GetUserData().pointer = reinterpret_cast<uintptr_t>(userData.get());
    userDataStorage_.push_back(std::move(userData));

    PhysicsBody result;
    result.body_ = body;
    result.userData_ = userDataStorage_.back().get();
    return result;
}

PhysicsBody PhysicsWorld::createCircleBody(const sf::Vector2f& pos, float radius,
                                            float density, float friction, float restitution,
                                            bool isDynamic, bool isBird, bool isEnvironment,
                                            void* entityPtr, bool isEditorEntity) {
    // Create body definition
    b2BodyDef bodyDef;
    bodyDef.type = isDynamic ? b2_dynamicBody : b2_staticBody;
    bodyDef.position = pixelToMeter(pos);
    bodyDef.angle = 0.0f;
    // Minimal damping - ground friction is handled in contact listener
    if (isDynamic) {
        bodyDef.linearDamping = 0.0f;  // No global damping - let physics work naturally
        bodyDef.angularDamping = 0.0f;  // No global angular damping
    }
    b2Body* body = world_->CreateBody(&bodyDef);

    // Create circle shape
    b2CircleShape circleShape;
    circleShape.m_radius = pixelToMeter(radius);

    // Create fixture
    b2FixtureDef fixtureDef;
    fixtureDef.shape = &circleShape;
    fixtureDef.density = density;
    fixtureDef.friction = friction;
    fixtureDef.restitution = restitution;
    b2Fixture* fixture = body->CreateFixture(&fixtureDef);

    // Create and attach user data
    auto userData = std::make_unique<FixtureUserData>();
    userData->isBird = isBird;
    userData->environment = isEnvironment;
    userData->isEditorEntity = isEditorEntity;
    userData->entityPtr = entityPtr;
    fixture->GetUserData().pointer = reinterpret_cast<uintptr_t>(userData.get());
    userDataStorage_.push_back(std::move(userData));

    PhysicsBody result;
    result.body_ = body;
    result.userData_ = userDataStorage_.back().get();
    return result;
}

void PhysicsWorld::step(float dt) {
    // Clear hit strength each frame
    for (auto& data : userDataStorage_) {
        data->hitStrength = 0.0f;
    }

    // Clamp velocities to max speed - but this is now handled per-bird in Bird class
    // We still need global clamp for non-bird objects
    for (b2Body* body = world_->GetBodyList(); body; body = body->GetNext()) {
        if (body->GetType() != b2_dynamicBody) continue;
        
        // Check if this is a bird (has user data with isBird flag)
        bool isBird = false;
        b2Fixture* fixture = body->GetFixtureList();
        while (fixture) {
            FixtureUserData* data = reinterpret_cast<FixtureUserData*>(
                fixture->GetUserData().pointer);
            if (data && data->isBird) {
                isBird = true;
                break;
            }
            fixture = fixture->GetNext();
        }
        
        // For birds, max speed is handled in Bird class, skip global clamp
        // For non-birds, use global max speed
        if (!isBird) {
            b2Vec2 vel = body->GetLinearVelocity();
            float speedSq = vel.x * vel.x + vel.y * vel.y;
            float maxSpeedMeters = pixelToMeter(config::kMaxBodySpeed);
            if (speedSq > maxSpeedMeters * maxSpeedMeters) {
                float speed = std::sqrt(speedSq);
                body->SetLinearVelocity((maxSpeedMeters / speed) * vel);
            }
        }
    }

    // Step the physics simulation with more position iterations for better collision resolution
    // Increased iterations to prevent overlaps, especially for stone/stoneslab blocks
    int32 velocityIterations = 10;   // Increased from 8
    int32 positionIterations = 40;  // Increased from 30 for better overlap resolution, especially for stoneslab ends
    world_->Step(dt, velocityIterations, positionIterations);
}

void PhysicsWorld::clearInactive() {
    // Box2D handles body destruction automatically when we delete them
    // We just need to clean up our user data storage
    userDataStorage_.erase(
        std::remove_if(userDataStorage_.begin(), userDataStorage_.end(),
                       [](const std::unique_ptr<FixtureUserData>& data) {
                           // Check if the body still exists
                           // This is a simplified check - in practice you'd track this better
                           return false;  // Keep all for now, proper cleanup would require body tracking
                       }),
        userDataStorage_.end());
}

// ========== Conversion functions ==========

b2Vec2 PhysicsWorld::pixelToMeter(const sf::Vector2f& pixel) {
    return b2Vec2(pixel.x / config::kPixelsPerMeter, pixel.y / config::kPixelsPerMeter);
}

sf::Vector2f PhysicsWorld::meterToPixel(const b2Vec2& meter) {
    return sf::Vector2f(meter.x * config::kPixelsPerMeter, meter.y * config::kPixelsPerMeter);
}

float PhysicsWorld::pixelToMeter(float pixel) {
    return pixel / config::kPixelsPerMeter;
}

float PhysicsWorld::meterToPixel(float meter) {
    return meter * config::kPixelsPerMeter;
}

// ========== GLM math utilities ==========

namespace PhysicsMath {
    float length(const vec2& v) {
        return glm::length(v);
    }

    vec2 normalize(const vec2& v) {
        float len = length(v);
        if (len < 1e-5f) return vec2(0.0f);
        return v / len;
    }

    float dot(const vec2& a, const vec2& b) {
        return glm::dot(a, b);
    }

    float distance(const vec2& a, const vec2& b) {
        return glm::distance(a, b);
    }
}
