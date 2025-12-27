// Central game manager and scene flow.
#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <deque>
#include <memory>
#include <string>

#include "Button.hpp"
#include "Config.hpp"
#include "Entity.hpp"
#include "Level.hpp"
#include "Physics.hpp"
#include "ScoreSystem.hpp"
#include "Logger.hpp"

// Forward declarations
class LevelEditor;
class AIController;

enum class Scene {
    Splash,
    MainMenu,
    LevelSelect,
    Playing,
    Score,
    GameOver,
    Paused,
    LevelEditor
};

class Game {
public:
    Game();
    ~Game();  // Explicit destructor needed for unique_ptr<LevelEditor>
    void run();

private:
    void processEvents();
    void update(float dt);
    void render();

    // Scene handlers
    void renderMenu();
    void renderLevelSelect();
    void renderHUD();
    void renderScoreScreen();
    void renderPauseMenu();
    void renderDebugCollisionBoxes();  // Debug: draw collision boxes

    void loadLevel(int index);
    void resetCurrent();
    void launchCurrentBird();

    sf::RenderWindow window_;
    sf::Font font_;
    sf::Texture backgroundTexture_;
    std::optional<sf::Sprite> backgroundSprite_;  // Will be set after texture is loaded
    Scene scene_{Scene::Splash};
    float splashTimer_{1.0f};
    float gameTime_{0.0f};  // Total game time for bird launch cooldown

    LevelLoader levelLoader_;
    LevelData currentLevel_;
    int levelIndex_{1};

    PhysicsWorld physics_;
    std::vector<std::unique_ptr<Block>> blocks_;
    std::vector<std::unique_ptr<Pig>> pigs_;
    std::deque<std::unique_ptr<Bird>> birds_;

    ScoreSystem scoreSystem_;
    ScorePopups popups_;

    // Slingshot state - using clear state machine
    enum class LaunchState {
        Ready,      // Can launch next bird (2 seconds passed or first bird)
        Dragging,   // Currently dragging to aim
        Launched,   // Bird just launched, in cooldown
        Cooldown    // Waiting for cooldown to expire
    };
    LaunchState launchState_{LaunchState::Ready};
    sf::Vector2f dragStart_{};
    sf::Vector2f dragCurrent_{};
    float lastBirdLaunchTime_{0.0f};  // Time when last bird was launched
    static constexpr float kLaunchCooldown = 2.0f;  // 2 seconds cooldown
    sf::Vector2f slingshotPos_{config::kSlingshotX, config::kSlingshotY};  // Slingshot launch position
    bool nextBirdMovedToSlingshot_{false};  // Track if next bird has been moved to slingshot
    Bird* draggingBird_{nullptr};  // Track which bird is being dragged (to avoid interference from launched birds)

    // Input state tracking
    bool prevMouseDown_{false};
    bool prevRightDown_{false};
    bool prevSpaceDown_{false};

    // Trajectory preview
    std::vector<sf::Vertex> previewPath_;
    
    // UI Buttons
    std::vector<std::unique_ptr<Button>> menuButtons_;
    std::vector<std::unique_ptr<Button>> gameButtons_;
    std::vector<std::unique_ptr<Button>> pauseButtons_;
    std::vector<std::unique_ptr<Button>> levelSelectButtons_;  // Level selection buttons
    std::vector<std::unique_ptr<Button>> scoreButtons_;  // Score screen buttons
    bool escPressed_{false};
    bool prevEscPressed_{false};
    bool showDebugCollisionBoxes_{false};  // Toggle collision box debug display
    
    // Helper methods for cleaner code
    bool canLaunchBird() const;
    void updateLaunchState(float dt);
    void handleSkillInput();
    void initButtons();
    void updateButtons(float dt);
    
    // Audio management
    void initAudio();
    void updateMusic();
    void playBirdSelectSound(BirdType type);
    void playBirdFlyingSound(BirdType type);
    
    // Music and sounds
    sf::Music titleTheme_;
    sf::Music gameComplete_;
    sf::Music birdsOutro_;
    sf::SoundBuffer birdSelectBuffers_[3];  // Red, Yellow, Bomb
    sf::SoundBuffer birdFlyingBuffers_[3];   // Red, Yellow, Bomb
    std::optional<sf::Sound> birdSelectSound_;  // Will be set after buffers are loaded
    std::optional<sf::Sound> birdFlyingSound_;  // Will be set after buffers are loaded
    Scene previousScene_{Scene::Splash};  // Track scene changes for music
    bool birdSelected_{false};  // Track if bird has been selected
    
    // Level Editor
    std::unique_ptr<LevelEditor> levelEditor_;
    
    // AI Controller
    std::unique_ptr<AIController> aiController_;
    bool aiModeEnabled_{false};  // AI模式开关
    bool prevAPressed_{false};   // 跟踪A键状态（用于切换AI模式）
    
    // AI控制相关
    void updateAI(float dt);
    void handleAIControl(float dt);
};

