// Entity definitions for birds, pigs, blocks and UI helpers.
#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Material.hpp"
#include "Physics.hpp"

enum class BirdType { Red, Yellow, Bomb };
enum class PigType { Small, Medium, Large };

struct ScoreEvent {
    sf::Vector2f position;
    int points{0};
    float lifetime{1.0f};
};

class Entity {
public:
    virtual ~Entity() = default;
    virtual void update(float dt) = 0;
    virtual void draw(sf::RenderWindow& window) = 0;
    bool isDestroyed() const { return destroyed_; }

protected:
    bool destroyed_{false};
};

class Block : public Entity {
public:
    Block(const Material& material, const sf::Vector2f& pos, const sf::Vector2f& size, PhysicsWorld& world);
    void update(float dt) override;
    void draw(sf::RenderWindow& window) override;
    PhysicsBody* body() { return &body_; }
    const PhysicsBody* body() const { return &body_; }
    float strength() const { return material_.strength; }
    sf::Vector2f position() const { return body_.position(); }
    const Material& material() const { return material_; }
    int health() const { return hp_; }
    int maxHealth() const { return maxHp_; }
    void takeDamage(float damage);  // Apply damage to block

private:
    void loadTexture();  // Load texture based on material type
    void updateTextureRect();  // Update texture rect for tiling/cropping
    
    Material material_;
    PhysicsBody body_;
    sf::Vector2f size_;  // Store block size for texture calculations
    sf::RectangleShape shape_;  // Fallback shape (if texture fails)
    std::optional<sf::Sprite> sprite_;  // Texture sprite
    sf::Texture texture_;  // Block texture
    float age_{0.0f};
    int hp_{100};  // Health points
    int maxHp_{100};  // Maximum health points
    float damageFlash_{0.0f};  // Visual feedback timer
};

class Pig : public Entity {
public:
    Pig(PigType type, const sf::Vector2f& pos, PhysicsWorld& world);
    void update(float dt) override;
    void draw(sf::RenderWindow& window) override;
    PhysicsBody* body() { return &body_; }
    const PhysicsBody* body() const { return &body_; }
    int health() const { return hp_; }
    int maxHealth() const { return maxHp_; }
    PigType type() const { return type_; }
    sf::Vector2f position() const { return body_.position(); }
    void takeDamage(float damage);  // Apply damage to pig

private:
    void updateVisuals();  // Update visual appearance based on health and damage flash
    void loadTextures();  // Load pig textures based on health level
    
    PigType type_;
    PhysicsBody body_;
    int hp_{10};
    int maxHp_{10};
    float radius_{16.0f};
    std::optional<sf::Sprite> sprite_;  // Optional because SFML 3.0 requires texture for sprite construction
    std::vector<sf::Texture> textures_;  // Textures for different health levels
    int currentTextureIndex_{0};  // Current texture index based on health
    float age_{0.0f};
    float damageFlash_{0.0f};  // Visual feedback timer
    float currentRotation_{0.0f};  // Store rotation angle for texture switching
};

class Bird : public Entity {
public:
    Bird(BirdType type, const sf::Vector2f& pos, PhysicsWorld& world);
    void update(float dt) override;
    void draw(sf::RenderWindow& window) override;

    BirdType type() const { return type_; }
    PhysicsBody* body() { return &body_; }
    const PhysicsBody* body() const { return &body_; }
    bool isLaunched() const { return launched_; }
    void launch(const sf::Vector2f& impulse);
    void activateSkill();

private:
    void loadTexture();  // Load bird texture based on type
    
    BirdType type_;
    PhysicsBody body_;
    PhysicsWorld* world_{nullptr};
    bool launched_{false};
    bool skillUsed_{false};
    float explosionTimer_{0.0f};  // 0 = not activated, -1 = skill activated, >0 = countdown
    bool exploded_{false};
    float explosionVisualTime_{0.0f};
    float radius_{14.0f};
    std::optional<sf::Sprite> sprite_;  // Optional because SFML 3.0 requires texture for sprite construction
    sf::Texture texture_;
    float restTimer_{0.0f};
    float maxSpeed_{800.0f};  // Per-bird max speed limit (initialized in constructor)
};

// Simple popup manager for score animations.
class ScorePopups {
public:
    explicit ScorePopups(const sf::Font& font);
    void spawn(const sf::Vector2f& pos, int points);
    void update(float dt);
    void draw(sf::RenderWindow& window);

private:
    struct Popup {
        sf::Text text;
        float life{1.0f};
    };
    const sf::Font& font_;
    std::vector<Popup> popups_;
};


