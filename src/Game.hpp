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

// 开屏动画中的简易小鸟数据（只做纯视觉用，不参与物理）
struct SplashBirdVisual {
    sf::Sprite sprite;
    sf::Vector2f velocity;  // 像素/秒

    // SFML 3.0 的 Sprite 需要纹理才能构造，提供一个显式构造函数
    explicit SplashBirdVisual(const sf::Texture& texture)
        : sprite(texture), velocity(0.0f, 0.0f) {}
};

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

    // 主界面动画逻辑（无限滚动地面和草）
    void updateMenuAnimation(float dt);
    void renderMenuAnimation();

    void loadLevel(int index);
    void resetCurrent();
    void launchCurrentBird();

    sf::RenderWindow window_;
    sf::View gameView_;  // 游戏视图（用于缩放和平移）
    sf::Font font_;
    sf::Texture backgroundTexture_;
    std::optional<sf::Sprite> backgroundSprite_;  // Will be set after texture is loaded
    sf::Texture choiceBackgroundTexture_;  // Level select background texture
    std::optional<sf::Sprite> choiceBackgroundSprite_;  // Level select background sprite
    sf::Texture winBackgroundTexture_;  // Win screen background texture
    std::optional<sf::Sprite> winBackgroundSprite_;  // Win screen background sprite
    sf::Texture slingshotTexture_;  // Slingshot texture
    std::optional<sf::Sprite> slingshotSprite_;  // Slingshot sprite (optional because SFML 3.0 requires texture for construction)
    
    // Splash 场景用的小鸟贴图（只用于视觉，不参与物理/声音）
    sf::Texture splashBirdRedTexture_;
    sf::Texture splashBirdYellowTexture_;
    sf::Texture splashBirdBlackTexture_;
    
    // 主界面动画用的地面和草贴图（无限拼接）
    sf::Texture groundTexture_;   // 地面贴图
    sf::Texture grassTexture_;    // 草贴图
    sf::Texture skyTexture_;      // 天空贴图（主界面背景）
    sf::Texture logoTexture_;     // Logo贴图
    std::optional<sf::Sprite> logoSprite_;  // Logo精灵

    Scene scene_{Scene::Splash};
    float splashTimer_{3.0f};  // 开屏动画时长（秒），后续可调
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

    // 主界面动画状态（无限滚动地面和草 + 纯视觉小鸟）
    float menuGroundOffset_{0.0f};          // 地面水平偏移
    float menuGroundSpeed_{80.0f};          // 地面向左移动速度（像素/秒）
    float menuSkyOffset_{0.0f};             // 天空水平偏移
    float menuBirdSpawnAccum_{0.0f};        // 生成小鸟的时间累积
    std::vector<SplashBirdVisual> menuBirds_;  // 当前在屏幕上的视觉小鸟
    float groundTextureWidth_{0.0f};        // 地面贴图宽度（用于无限拼接）
    float grassTextureWidth_{0.0f};         // 草贴图宽度（用于无限拼接）
    float skyTextureWidth_{0.0f};           // 天空贴图宽度（用于无限拼接）
    float menuCycleLCM_{0.0f};              // 地面和草贴图的最小公倍数周期（用于同步重置）
    
    // 游戏开始时的缩放动画
    bool zoomAnimationActive_{false};       // 缩放动画是否激活
    float zoomAnimationTime_{0.0f};         // 缩放动画已进行的时间
    float zoomAnimationDuration_{2.5f};     // 缩放动画持续时间（秒）
    float startZoom_{1.0f};                 // 起始缩放比例
    float targetZoom_{1.0f};                // 目标缩放比例
    sf::Vector2f startViewCenter_{};        // 起始视图中心
    sf::Vector2f targetViewCenter_{};       // 目标视图中心
    
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
    
    // 缩放动画相关
    void calculateAndStartZoomAnimation();  // 计算并启动缩放动画
    void updateZoomAnimation(float dt);     // 更新缩放动画
    float easeInOutCubic(float t);          // 缓动函数（慢->快->慢）
};

