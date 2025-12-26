// Box2D 2.4.1 physics wrapper with GLM 1.0.2 for math operations.
#pragma once

#include <SFML/Graphics.hpp>
#include <box2d/box2d.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

// User data attached to Box2D fixtures for damage tracking
struct FixtureUserData {
    float hitStrength{0.0f};
    bool isBird{false};
    bool environment{false};
    bool isEditorEntity{false};  // True if entity is in level editor (no damage)
    void* entityPtr{nullptr};  // Pointer back to Entity for damage queries
};

// Wrapper around Box2D body that maintains compatibility with existing code
class PhysicsBody {
public:
    b2Body* body_{nullptr};
    FixtureUserData* userData_{nullptr};

    // Compatibility interface
    sf::Vector2f position() const;
    sf::Vector2f velocity() const;
    float angle() const;
    bool active() const;
    bool dynamic() const;
    float hitStrength() const;
    bool isBird() const;
    bool environment() const;

    void setPosition(const sf::Vector2f& pos);
    void setVelocity(const sf::Vector2f& vel);
    void setMass(float mass, float density = 1.0f);
    void applyForce(const sf::Vector2f& force);
    void applyImpulse(const sf::Vector2f& impulse);
    void setActive(bool active);
    void setDynamic(bool dynamic);
};

// Box2D contact listener for damage calculation
class DamageContactListener : public b2ContactListener {
public:
    void BeginContact(b2Contact* contact) override;
    void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) override;
};

// Box2D physics world wrapper
class PhysicsWorld {
public:
    explicit PhysicsWorld(const sf::Vector2f& gravity);
    ~PhysicsWorld();
    
    // Move constructor and assignment for Game::loadLevel
    PhysicsWorld(PhysicsWorld&&) noexcept = default;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept = default;
    
    // Delete copy constructor/assignment (unique_ptr is not copyable)
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Create body with Box2D
    PhysicsBody createBoxBody(const sf::Vector2f& pos, const sf::Vector2f& size,
                               float density, float friction, float restitution,
                               bool isDynamic, bool isBird = false, bool isEnvironment = false,
                               void* entityPtr = nullptr, bool isEditorEntity = false);
    PhysicsBody createCircleBody(const sf::Vector2f& pos, float radius,
                                  float density, float friction, float restitution,
                                  bool isDynamic, bool isBird = false, bool isEnvironment = false,
                                  void* entityPtr = nullptr, bool isEditorEntity = false);

    void step(float dt);
    void clearInactive();

    b2World* world() { return world_.get(); }
    const b2World* world() const { return world_.get(); }

    // Convert between pixel space (SFML) and physics space (Box2D meters)
    static b2Vec2 pixelToMeter(const sf::Vector2f& pixel);
    static sf::Vector2f meterToPixel(const b2Vec2& meter);
    static float pixelToMeter(float pixel);
    static float meterToPixel(float meter);

private:
    std::unique_ptr<b2World> world_;
    std::unique_ptr<DamageContactListener> contactListener_;
    std::vector<std::unique_ptr<FixtureUserData>> userDataStorage_;
};

// GLM utility functions for math operations
namespace PhysicsMath {
    using glm::vec2;
    using glm::vec3;
    using glm::vec4;

    inline vec2 toGlm(const sf::Vector2f& v) { return vec2(v.x, v.y); }
    inline sf::Vector2f fromGlm(const vec2& v) { return sf::Vector2f(v.x, v.y); }
    inline vec2 toGlm(const b2Vec2& v) { return vec2(v.x, v.y); }
    inline b2Vec2 toBox2D(const vec2& v) { return b2Vec2(v.x, v.y); }

    float length(const vec2& v);
    vec2 normalize(const vec2& v);
    float dot(const vec2& a, const vec2& b);
    float distance(const vec2& a, const vec2& b);
}
