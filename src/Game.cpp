// Game implementation handling scenes and gameplay.
#include "Game.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <variant>
#include <random>

#include "Config.hpp"
#include "Material.hpp"
#include "LevelEditor.hpp"
#include "AIController.hpp"
#include "Logger.hpp"

namespace {
sf::Vector2f clampVec(const sf::Vector2f& v, float maxLen) {
    float lenSq = v.x * v.x + v.y * v.y;
    if (lenSq <= maxLen * maxLen) return v;
    float len = std::sqrt(lenSq);
    return v * (maxLen / len);
}
}  // namespace

Game::Game()
    : window_(sf::VideoMode({config::kWindowWidth, config::kWindowHeight}), config::kWindowTitle),
      physics_({0.f, config::kGravity}),
      scoreSystem_(font_),
      popups_(font_) {
    // 初始化日志系统
    Logger::getInstance().init("last_run.log");
    Logger::getInstance().info("游戏启动");
    
    window_.setFramerateLimit(60);
    // Try to load fonts in order, with better error reporting
    bool fontLoaded = false;
    if (font_.openFromFile(config::kFontPathPrimary)) {
        fontLoaded = true;
        std::cerr << "成功加载字体: " << config::kFontPathPrimary << "\n";
    } else if (font_.openFromFile(config::kFontPathSecondary)) {
        fontLoaded = true;
        std::cerr << "成功加载字体: " << config::kFontPathSecondary << "\n";
    } else if (font_.openFromFile(config::kFontPathFallback)) {
        fontLoaded = true;
        std::cerr << "成功加载字体: " << config::kFontPathFallback << "\n";
    }
    
    if (!fontLoaded) {
        std::cerr << "警告: 无法加载任何指定字体，中文可能无法正常显示。\n";
        std::cerr << "尝试的字体路径:\n";
        std::cerr << "  1. " << config::kFontPathPrimary << "\n";
        std::cerr << "  2. " << config::kFontPathSecondary << "\n";
        std::cerr << "  3. " << config::kFontPathFallback << "\n";
    }
    
    // Load background image
    if (!backgroundTexture_.loadFromFile("image/background.png")) {
        std::cerr << "警告: 无法加载背景图片 image/background.png\n";
    } else {
        backgroundSprite_ = sf::Sprite(backgroundTexture_);
        // Scale background to fit window
        sf::Vector2u textureSize = backgroundTexture_.getSize();
        float scaleX = static_cast<float>(config::kWindowWidth) / static_cast<float>(textureSize.x);
        float scaleY = static_cast<float>(config::kWindowHeight) / static_cast<float>(textureSize.y);
        backgroundSprite_->setScale(sf::Vector2f(scaleX, scaleY));
    }
    
    // Load choice background texture (level select)
    if (!choiceBackgroundTexture_.loadFromFile("image/choice_background.png")) {
        std::cerr << "警告: 无法加载选关界面背景图片 image/choice_background.png\n";
    } else {
        choiceBackgroundSprite_ = sf::Sprite(choiceBackgroundTexture_);
        // Scale background to fit window
        sf::Vector2u textureSize = choiceBackgroundTexture_.getSize();
        float scaleX = static_cast<float>(config::kWindowWidth) / static_cast<float>(textureSize.x);
        float scaleY = static_cast<float>(config::kWindowHeight) / static_cast<float>(textureSize.y);
        choiceBackgroundSprite_->setScale(sf::Vector2f(scaleX, scaleY));
    }
    
    // Load win background texture (score screen)
    if (!winBackgroundTexture_.loadFromFile("image/win_back.png")) {
        std::cerr << "警告: 无法加载胜利界面背景图片 image/win_back.png\n";
    } else {
        winBackgroundSprite_ = sf::Sprite(winBackgroundTexture_);
        // Scale background to fit window
        sf::Vector2u textureSize = winBackgroundTexture_.getSize();
        float scaleX = static_cast<float>(config::kWindowWidth) / static_cast<float>(textureSize.x);
        float scaleY = static_cast<float>(config::kWindowHeight) / static_cast<float>(textureSize.y);
        winBackgroundSprite_->setScale(sf::Vector2f(scaleX, scaleY));
    }
    
    // Load slingshot texture
    if (!slingshotTexture_.loadFromFile("image/dangong.png")) {
        std::cerr << "警告: 无法加载弹弓贴图 image/dangong.png\n";
    } else {
        slingshotSprite_ = sf::Sprite(slingshotTexture_);
        // Set origin to center
        sf::Vector2u textureSize = slingshotTexture_.getSize();
        slingshotSprite_->setOrigin(sf::Vector2f(textureSize.x * 0.5f, textureSize.y * 0.5f));
        
        // 缩放弹弓贴图使其高度等于两个鸟重叠的高度
        // 鸟的半径是14.f，所以一个鸟的直径是28像素
        // 两个鸟向上重叠的高度大约是42像素（一个完整鸟 + 另一个鸟的一半重叠）
        const float birdRadius = 14.0f;
        const float targetHeight = birdRadius * 3.0f;  // 两个鸟重叠的高度：1.5个直径 = 42像素
        float scale = targetHeight / static_cast<float>(textureSize.y);
        slingshotSprite_->setScale(sf::Vector2f(scale, scale));
    }

    // 主界面动画用小鸟贴图（纯视觉，不参与物理/声音）
    if (!splashBirdRedTexture_.loadFromFile("image/bird_red.png")) {
        std::cerr << "警告: 无法加载主界面动画红鸟贴图 image/bird_red.png\n";
    }
    if (!splashBirdYellowTexture_.loadFromFile("image/bird_yellow.png")) {
        std::cerr << "警告: 无法加载主界面动画黄鸟贴图 image/bird_yellow.png\n";
    }
    if (!splashBirdBlackTexture_.loadFromFile("image/bird_black.png")) {
        std::cerr << "警告: 无法加载主界面动画黑鸟贴图 image/bird_black.png\n";
    }
    
    // 加载主界面动画用的地面、草和天空贴图
    if (!groundTexture_.loadFromFile("image/ground.png")) {
        std::cerr << "警告: 无法加载地面贴图 image/ground.png\n";
    } else {
        groundTextureWidth_ = static_cast<float>(groundTexture_.getSize().x);
    }
    if (!grassTexture_.loadFromFile("image/grass.png")) {
        std::cerr << "警告: 无法加载草贴图 image/grass.png\n";
    } else {
        grassTextureWidth_ = static_cast<float>(grassTexture_.getSize().x);
    }
    if (!skyTexture_.loadFromFile("image/sky.png")) {
        std::cerr << "警告: 无法加载天空贴图 image/sky.png\n";
    } else {
        skyTextureWidth_ = static_cast<float>(skyTexture_.getSize().x);
    }
    
    // 加载Logo贴图
    if (!logoTexture_.loadFromFile("image/logo.png")) {
        std::cerr << "警告: 无法加载Logo贴图 image/logo.png\n";
    } else {
        logoSprite_ = sf::Sprite(logoTexture_);
        // 缩小50%
        logoSprite_->setScale(sf::Vector2f(0.5f, 0.5f));
        // 设置Logo位置：在窗口上方居中（考虑缩放后的尺寸）
        sf::Vector2u logoSize = logoTexture_.getSize();
        float scaledWidth = static_cast<float>(logoSize.x) * 0.5f;
        float logoX = (static_cast<float>(config::kWindowWidth) - scaledWidth) * 0.5f;
        float logoY = 60.0f;  // 距离顶部60像素
        logoSprite_->setPosition({logoX, logoY});
    }
    
    // 计算地面和草贴图的最小公倍数周期（考虑50%缩放）
    float groundScaledWidth = groundTextureWidth_ * 0.5f;
    float grassScaledWidth = grassTextureWidth_ * 0.5f;
    if (groundScaledWidth > 0.0f && grassScaledWidth > 0.0f) {
        // 计算最小公倍数：LCM(a, b) = |a * b| / GCD(a, b)
        float a = groundScaledWidth;
        float b = grassScaledWidth;
        // 简化计算：找到最大公约数
        float gcd = a;
        float temp = b;
        while (temp > 0.01f) {  // 浮点数比较，使用小的阈值
            float remainder = std::fmod(gcd, temp);
            gcd = temp;
            temp = remainder;
        }
        menuCycleLCM_ = (a * b) / gcd;
        std::cerr << "地面和草贴图周期 LCM: " << menuCycleLCM_ << " 像素\n";
    } else {
        menuCycleLCM_ = static_cast<float>(config::kWindowWidth);  // 备用值
    }
    
    initAudio();
    initButtons();
    
    // 初始化AI控制器
    aiController_ = std::make_unique<AIController>();
    aiController_->setGame(this);
    
    loadLevel(levelIndex_);
}

Game::~Game() {
    Logger::getInstance().info("游戏关闭");
    Logger::getInstance().close();
}

void Game::run() {
    sf::Clock clock;
    while (window_.isOpen()) {
        float dt = clock.restart().asSeconds();
        processEvents();
        update(dt);
        render();
    }
}

void Game::processEvents() {
    // Track ESC key state for pause functionality
    prevEscPressed_ = escPressed_;
    escPressed_ = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Escape);
    
    // Track T key for debug collision boxes toggle
    static bool prevTPressed = false;
    bool tPressed = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::T);
    if (scene_ == Scene::Playing && tPressed && !prevTPressed) {
        showDebugCollisionBoxes_ = !showDebugCollisionBoxes_;
    }
    prevTPressed = tPressed;
    
    // Track A key for AI mode toggle
    bool aPressed = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A);
    if (scene_ == Scene::Playing && aPressed && !prevAPressed_) {
        aiModeEnabled_ = !aiModeEnabled_;
        if (aiController_) {
            aiController_->setEnabled(aiModeEnabled_);
        }
        Logger::getInstance().info("AI模式切换: " + std::string(aiModeEnabled_ ? "开启" : "关闭"));
        std::cerr << "AI Mode: " << (aiModeEnabled_ ? "ON" : "OFF") << "\n";
    }
    prevAPressed_ = aPressed;
    
    while (auto event = window_.pollEvent()) {
        if (event->is<sf::Event::Closed>()) window_.close();
        
        // Handle ESC key for pause (only when just pressed, not held)
        if (scene_ == Scene::Playing && escPressed_ && !prevEscPressed_) {
            scene_ = Scene::Paused;
            Logger::getInstance().info("场景切换: Playing -> Paused");
        }
        
        // Handle ESC key to return from LevelEditor to main menu
        if (scene_ == Scene::LevelEditor && escPressed_ && !prevEscPressed_) {
            scene_ = Scene::MainMenu;
            Logger::getInstance().info("场景切换: LevelEditor -> MainMenu");
        }
        
        // Handle skill activation - unified event handling
        if (scene_ == Scene::Playing && !birds_.empty()) {
            handleSkillInput();
        }
        
        // Handle LevelEditor events
        if (scene_ == Scene::LevelEditor && levelEditor_) {
            levelEditor_->handleEvent(*event);
        }
    }
}

void Game::update(float dt) {
    // Update music based on scene changes
    updateMusic();
    switch (scene_) {
        case Scene::Splash:
            splashTimer_ -= dt;
            if (splashTimer_ <= 0) {
                scene_ = Scene::MainMenu;
                Logger::getInstance().info("场景切换: Splash -> MainMenu");
            }
            break;
        case Scene::MainMenu:
            // 更新主界面动画（无限滚动地面和草 + 视觉小鸟）
            updateMenuAnimation(dt);
            updateButtons(dt);
            break;
        case Scene::LevelSelect:
            updateButtons(dt);
            break;
        case Scene::Playing: {
            gameTime_ += dt;
            
            // Update buttons first (to detect clicks before bird launching)
            updateButtons(dt);
            
            // Check if any button was clicked (if so, don't process bird launching)
            bool buttonClicked = false;
            for (const auto& btn : gameButtons_) {
                if (btn->isPressed()) {
                    buttonClicked = true;
                    break;
                }
            }
            
            // Update launch state machine
            updateLaunchState(dt);
            
            // Update AI controller
            if (aiController_ && aiModeEnabled_) {
                updateAI(dt);
                handleAIControl(dt);
            }
            
            // Handle bird launching input (only if AI is not enabled and no button was clicked)
            // KEY FIX: Only process input if we're still playing (not won/lost)
            // Also check if mouse is over any button - if so, don't process bird launching
            bool mouseOverButton = false;
            if (!buttonClicked) {
                sf::Vector2i pixelPos = sf::Mouse::getPosition(window_);
                sf::Vector2f mousePos = window_.mapPixelToCoords(pixelPos);
                for (const auto& btn : gameButtons_) {
                    if (btn->isHovered()) {
                        mouseOverButton = true;
                        break;
                    }
                }
            }
            
            if (!birds_.empty() && scene_ == Scene::Playing && !aiModeEnabled_ && !buttonClicked && !mouseOverButton) {
                auto& currentBird = *birds_.front();
                bool mouseDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
                
                // KEY FIX: If current bird is launched, we should be controlling the NEXT bird
                bool canControlCurrentBird = !currentBird.isLaunched();
                
                // If current bird is launched, check if it has moved away from slingshot
                // Only then move next bird to slingshot position (to avoid blocking)
                if (!canControlCurrentBird && birds_.size() > 1 && !nextBirdMovedToSlingshot_) {
                    // Check if current bird has moved away from slingshot (at least 50 pixels)
                    auto* currentBody = currentBird.body();
                    if (currentBody) {
                        sf::Vector2f currentPos = currentBody->position();
                        float distFromSlingshot = std::sqrt(
                            (currentPos.x - slingshotPos_.x) * (currentPos.x - slingshotPos_.x) +
                            (currentPos.y - slingshotPos_.y) * (currentPos.y - slingshotPos_.y)
                        );
                        
                        // Only move next bird when current bird has moved away (at least 50 pixels)
                        if (distFromSlingshot > 50.0f) {
                            auto& nextBird = *birds_[1];
                            if (auto* body = nextBird.body()) {
                                body->setPosition(slingshotPos_);
                                body->setDynamic(false);  // Reset to static
                                body->setVelocity({0.0f, 0.0f});  // Clear velocity
                                nextBirdMovedToSlingshot_ = true;  // Mark as moved
                            }
                        }
                    }
                }
                
                // If current bird is not launched and not at slingshot position, move it there
                if (canControlCurrentBird) {
                    auto* body = currentBird.body();
                    if (body) {
                        sf::Vector2f currentPos = body->position();
                        float distToSlingshot = std::sqrt(
                            (currentPos.x - slingshotPos_.x) * (currentPos.x - slingshotPos_.x) +
                            (currentPos.y - slingshotPos_.y) * (currentPos.y - slingshotPos_.y)
                        );
                        // If bird is far from slingshot (more than 10 pixels), move it there
                        if (distToSlingshot > 10.0f) {
                            body->setPosition(slingshotPos_);
                            body->setDynamic(false);  // Ensure it's static
                            body->setVelocity({0.0f, 0.0f});  // Clear velocity
                        }
                    }
                }
                
                // State machine for launching
                // KEY: If current bird is launched but next bird is at slingshot, we can control next bird
                bool canControlNextBird = false;
                if (!canControlCurrentBird && birds_.size() > 1 && nextBirdMovedToSlingshot_) {
                    // Current bird is launched, next bird is at slingshot
                    // Check if we can control the next bird (it's ready at slingshot)
                    auto& nextBird = *birds_[1];
                    if (!nextBird.isLaunched()) {
                        canControlNextBird = true;
                    }
                }
                
                switch (launchState_) {
                    case LaunchState::Ready: {
                        // Can start dragging if current bird is not launched OR next bird is ready
                        if ((canControlCurrentBird || canControlNextBird) && mouseDown && !prevMouseDown_) {
                            launchState_ = LaunchState::Dragging;
                            // Use next bird if current is launched, otherwise use current
                            // Save reference to target bird to avoid interference from launched birds
                            draggingBird_ = canControlNextBird ? birds_[1].get() : birds_.front().get();
                            if (auto* body = draggingBird_->body()) {
                                dragStart_ = body->position();
                            } else {
                                sf::Vector2i pixelPos = sf::Mouse::getPosition(window_);
                                dragStart_ = window_.mapPixelToCoords(pixelPos);
                            }
                            
                            // Play bird select sound when bird is selected (dragging starts)
                            if (draggingBird_) {
                                playBirdSelectSound(draggingBird_->type());
                                birdSelected_ = true;  // Mark bird as selected
                            }
                        }
                        break;
                    }
                    case LaunchState::Dragging: {
                        // Use saved draggingBird_ reference instead of recalculating
                        // This prevents interference from launched birds
                        // KEY FIX: Only check if draggingBird_ is valid, not if it's launched
                        // The bird should not be launched until we release the mouse
                        if (draggingBird_) {
                            // Verify the bird is still in the birds_ list and not launched
                            bool birdStillValid = false;
                            for (const auto& bird : birds_) {
                                if (bird.get() == draggingBird_) {
                                    birdStillValid = !bird->isLaunched();
                                    break;
                                }
                            }
                            
                            if (birdStillValid) {
                                // Update drag position
                                sf::Vector2i pixelPos = sf::Mouse::getPosition(window_);
                                dragCurrent_ = window_.mapPixelToCoords(pixelPos);
                                
                                // Release to launch
                                if (!mouseDown && prevMouseDown_) {
                                    launchCurrentBird();
                                    // After launching, immediately go to Ready for next bird (no cooldown)
                                    launchState_ = LaunchState::Ready;
                                    draggingBird_ = nullptr;  // Clear reference
                                    lastBirdLaunchTime_ = gameTime_;
                                }
                            } else {
                                // Bird was launched by other means or removed, go to Ready
                                launchState_ = LaunchState::Ready;
                                draggingBird_ = nullptr;  // Clear reference
                            }
                        } else {
                            // No valid dragging bird, go to Ready
                            launchState_ = LaunchState::Ready;
                        }
                        break;
                    }
                    case LaunchState::Launched:
                    case LaunchState::Cooldown:
                        // These states should immediately transition to Ready (handled by updateLaunchState)
                        // But also allow immediate input if next bird is ready
                        if ((canControlCurrentBird || canControlNextBird) && mouseDown && !prevMouseDown_) {
                            launchState_ = LaunchState::Dragging;
                            // Save reference to target bird
                            draggingBird_ = canControlNextBird ? birds_[1].get() : birds_.front().get();
                            if (auto* body = draggingBird_->body()) {
                                dragStart_ = body->position();
                            } else {
                                sf::Vector2i pixelPos = sf::Mouse::getPosition(window_);
                                dragStart_ = window_.mapPixelToCoords(pixelPos);
                            }
                        }
                        break;
                }
                
                prevMouseDown_ = mouseDown;
                
                // Update trajectory preview while dragging
                previewPath_.clear();
                // Use saved draggingBird_ reference for preview (avoids interference from launched birds)
                if (launchState_ == LaunchState::Dragging && draggingBird_ && !draggingBird_->isLaunched()) {
                    auto* birdBody = draggingBird_->body();
                    if (birdBody) {
                        // Calculate pull and initial velocity exactly as in launchCurrentBird()
                        sf::Vector2f pull = dragStart_ - dragCurrent_;
                        // 对于黄鸟，只在AI模式下允许2倍拉弓距离（手动模式下使用正常距离）
                        float maxPullDist = config::kMaxPullDistance;
                        if (draggingBird_->type() == BirdType::Yellow && aiModeEnabled_) {
                            maxPullDist = config::kMaxPullDistance * 2.0f;
                        }
                        pull = clampVec(pull, maxPullDist);
                        // 修正：pull向量指向从拖拽点回到弹弓的方向（向后拉的方向）
                        // 初速度应该指向发射方向（向前，与pull相反），所以应该是pull方向，而不是-pull
                        sf::Vector2f v0 = pull * config::kSlingshotStiffness;
                        
                        // Clamp preview speed based on bird type (same as Bird::launch())
                        float speedSq = v0.x * v0.x + v0.y * v0.y;
                        float speed = std::sqrt(speedSq);
                        
                        // Get bird-specific initial max speed
                        float initialMaxSpeed = config::kMaxBodySpeed;
                        BirdType birdType = draggingBird_->type();
                        switch (birdType) {
                            case BirdType::Red:
                                initialMaxSpeed = config::bird_speed::kRedInitialMax;
                                break;
                            case BirdType::Yellow:
                                initialMaxSpeed = config::bird_speed::kYellowInitialMax;
                                break;
                            case BirdType::Bomb:
                                initialMaxSpeed = config::bird_speed::kBombInitialMax;
                                break;
                        }
                        
                        // Clamp to bird-specific initial max speed (same as Bird::launch())
                        if (speed > initialMaxSpeed) {
                            v0 = v0 * (initialMaxSpeed / speed);
                        }
                        
                        // Simulate trajectory with physics (same as actual bird flight)
                        // 包括重力和空气阻力，与实际物理模拟一致
                        sf::Vector2f pos = birdBody->position();
                        sf::Vector2f vel = v0;
                        const int steps = 60;  // More steps for accuracy
                        const float stepDt = 0.05f;
                        for (int i = 0; i < steps; ++i) {
                            vel.y += config::kGravity * stepDt;
                            
                            // 应用空气阻力（与实际物理模拟一致）
                            // kAirResistanceAccel是m/s²，需要转换为像素/秒²
                            float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
                            if (speed > 0.001f) {
                                const float airResistanceAccelPixels = config::kAirResistanceAccel * config::kPixelsPerMeter;
                                sf::Vector2f resistanceDir = sf::Vector2f(vel.x / speed, vel.y / speed);
                                // 空气阻力方向必须与速度方向相反（阻力阻碍运动）
                                sf::Vector2f resistanceAccel = -resistanceDir * airResistanceAccelPixels;
                                vel += resistanceAccel * stepDt;
                            }
                            
                            pos += vel * stepDt;
                            if (pos.y > static_cast<float>(config::kWindowHeight) + 100.0f) break;
                            previewPath_.emplace_back(pos, sf::Color(80, 80, 80, 200));
                        }
                    }
                }
            }

            // IMPORTANT: Update order matters for explosion damage
            // 1. First step physics (clears hitStrength from previous frame)
            // 2. Update birds (may trigger explosions, setting hitStrength)
            // 3. Update blocks and pigs (read hitStrength to apply damage)
            physics_.step(config::kFixedDelta);
            for (auto& b : birds_) b->update(dt);  // Birds update first (explosions set hitStrength)
            for (auto& b : blocks_) b->update(dt);  // Blocks read hitStrength
            for (auto& p : pigs_) p->update(dt);    // Pigs read hitStrength

            for (auto it = blocks_.begin(); it != blocks_.end();) {
                if ((*it)->isDestroyed()) {
                    int pts = static_cast<int>((*it)->material().strength * 5);
                    scoreSystem_.addPoints(pts);
                    popups_.spawn((*it)->position(), pts);
                    it = blocks_.erase(it);
                } else {
                    ++it;
                }
            }

            for (auto it = pigs_.begin(); it != pigs_.end();) {
                if ((*it)->isDestroyed()) {
                    int pts = 0;
                    switch ((*it)->type()) {
                        case PigType::Small: pts = 1000; break;
                        case PigType::Medium: pts = 3000; break;
                        case PigType::Large: pts = 5000; break;
                    }
                    scoreSystem_.addPoints(pts);
                    popups_.spawn((*it)->position(), pts);
                    it = pigs_.erase(it);
                } else {
                    ++it;
                }
            }
            // Remove destroyed birds immediately
            // Also remove birds that have been launched and are inactive (destroyed)
            // Note: Next bird should already be at slingshot position (moved when previous bird was launched)
            while (!birds_.empty() && birds_.front()->isDestroyed()) {
                birds_.pop_front();
                // When a bird is removed, next bird (now at front) should already be at slingshot
                // But ensure it's properly positioned and ready
                if (!birds_.empty()) {
                    auto& nextBird = *birds_.front();
                    if (auto* body = nextBird.body()) {
                        // Ensure it's at slingshot position (should already be there)
                        body->setPosition(slingshotPos_);
                        body->setDynamic(false);  // Reset to static until launched
                        body->setVelocity({0.0f, 0.0f});  // Clear any velocity
                    }
                    launchState_ = LaunchState::Ready;
                }
            }
            
            popups_.update(dt);
            scoreSystem_.update(dt);

            // Check win/lose conditions AFTER removing destroyed objects
            // IMPORTANT: Check win first (pigs empty), then lose (birds empty)
            // This ensures explosion that kills last pig triggers win, not lose
            // KEY FIX: Check immediately after updating pigs to ensure victory is detected right away
            bool won = pigs_.empty();
            bool lost = birds_.empty() && !won;  // Only lose if no birds AND not won (to prioritize win)
            
            if (won) {
                scoreSystem_.addBonusForRemainingBirds(static_cast<int>(birds_.size()));
                int finalScore = scoreSystem_.score();
                Logger::getInstance().info("关卡完成 - 关卡: " + std::to_string(levelIndex_) + 
                                          ", 最终分数: " + std::to_string(finalScore) +
                                          ", 剩余小鸟: " + std::to_string(birds_.size()));
                scene_ = Scene::Score;
                // Break immediately to prevent further updates
                break;
            } else if (lost) {
                Logger::getInstance().info("游戏失败 - 关卡: " + std::to_string(levelIndex_));
                scene_ = Scene::GameOver;
                // Break immediately to prevent further updates
                break;
            }
            break;
        }
        case Scene::Score:
        case Scene::GameOver:
            updateButtons(dt);
            break;
        case Scene::Paused:
            updateButtons(dt);
            break;
        case Scene::LevelEditor:
            if (levelEditor_) {
                levelEditor_->update(dt);
            }
            break;
    }
}

void Game::render() {
    // Draw background based on scene
    if (scene_ == Scene::Splash) {
        window_.clear(sf::Color(180, 220, 255));
        if (backgroundSprite_.has_value()) {
            window_.draw(*backgroundSprite_);
        }
    } else if (scene_ == Scene::Playing || scene_ == Scene::Paused) {
        // 游戏场景使用sky.png作为背景
        if (skyTexture_.getSize().x > 0) {
            // 绘制天空背景，填充整个窗口
            sf::Sprite skySprite(skyTexture_);
            sf::Vector2u skySize = skyTexture_.getSize();
            float windowWidth = static_cast<float>(config::kWindowWidth);
            float windowHeight = static_cast<float>(config::kWindowHeight);
            
            // 计算缩放比例，使天空填充整个窗口
            float scaleX = windowWidth / static_cast<float>(skySize.x);
            float scaleY = windowHeight / static_cast<float>(skySize.y);
            skySprite.setScale(sf::Vector2f(scaleX, scaleY));
            skySprite.setPosition(sf::Vector2f(0.0f, 0.0f));
            window_.draw(skySprite);
        } else {
            window_.clear(sf::Color(180, 220, 255));
        }
    } else {
        window_.clear(sf::Color(180, 220, 255));
    }
    // MainMenu scene uses sky.png as background (drawn in renderMenuAnimation)
    
    switch (scene_) {
        case Scene::Splash: {
            // Splash 场景只显示背景，不显示动画
            break;
        }
        case Scene::MainMenu:
            // 渲染主界面动画（无限滚动地面和草 + 视觉小鸟）
            renderMenuAnimation();
            renderMenu();
            // Draw menu buttons
            for (const auto& btn : menuButtons_) {
                btn->draw(window_);
            }
            break;
        case Scene::LevelSelect:
            // Draw choice background
            if (choiceBackgroundSprite_.has_value()) {
                window_.draw(*choiceBackgroundSprite_);
            }
            renderLevelSelect();
            // Draw level select buttons
            for (const auto& btn : levelSelectButtons_) {
                btn->draw(window_);
            }
            break;
        case Scene::Playing: {
            // AI visual feedback will be drawn with trajectory preview below
            
            // Draw visible ground using ground.png - extend to cover full game world
            {
                const float groundLeft = -200.0f;
                const float groundRight = 1600.0f;
                const float groundWidth = groundRight - groundLeft;
                const float groundHeight = 40.0f;  // 保持与原有高度一致
                const float groundY = static_cast<float>(config::kWindowHeight) - 10.0f;  // 保持与原有位置一致
                
                if (groundTexture_.getSize().x > 0) {
                    // 使用ground.png贴图
                    sf::Sprite groundSprite(groundTexture_);
                    sf::Vector2u groundTexSize = groundTexture_.getSize();
                    float texWidth = static_cast<float>(groundTexSize.x);
                    float texHeight = static_cast<float>(groundTexSize.y);
                    
                    // 设置纹理为可重复模式
                    groundTexture_.setRepeated(true);
                    
                    // 设置纹理矩形，使用实际地面尺寸（会自动重复）
                    groundSprite.setTextureRect(sf::IntRect(
                        sf::Vector2i(0, 0),
                        sf::Vector2i(static_cast<int>(groundWidth), static_cast<int>(groundHeight))
                    ));
                    
                    // 设置位置和原点
                    groundSprite.setOrigin(sf::Vector2f(groundWidth * 0.5f, groundHeight * 0.5f));
                    groundSprite.setPosition(
                        {(groundLeft + groundRight) * 0.5f, groundY});
                    
                    window_.draw(groundSprite);
                } else {
                    // 如果贴图加载失败，使用备用颜色块
                    sf::RectangleShape groundShape({groundWidth, groundHeight});
                    groundShape.setOrigin({groundWidth * 0.5f, groundHeight * 0.5f});
                    groundShape.setPosition({(groundLeft + groundRight) * 0.5f, groundY});
                    groundShape.setFillColor(sf::Color(110, 180, 80));
                    window_.draw(groundShape);
                }
            }
            
            // Draw game buttons (restart, next level)
            for (const auto& btn : gameButtons_) {
                btn->draw(window_);
            }

            // Draw slingshot
            if (slingshotSprite_.has_value()) {
                slingshotSprite_->setPosition(slingshotPos_);
                window_.draw(*slingshotSprite_);
            }
            
            for (auto& b : blocks_) b->draw(window_);
            for (auto& p : pigs_) p->draw(window_);
            for (auto& b : birds_) b->draw(window_);

            // Trajectory preview (player mode)
            if (!previewPath_.empty() && !aiModeEnabled_) {
                window_.draw(previewPath_.data(),
                             previewPath_.size(),
                             sf::PrimitiveType::LineStrip);
            }
            
            // AI trajectory preview
            if (aiModeEnabled_ && aiController_) {
                const auto& aiTrajectory = aiController_->getTrajectoryPreview();
                if (!aiTrajectory.empty()) {
                    window_.draw(aiTrajectory.data(),
                                aiTrajectory.size(),
                                sf::PrimitiveType::LineStrip);
                }
            }

            if (launchState_ == LaunchState::Dragging && !birds_.empty()) {
                sf::Vector2i pixelPos = sf::Mouse::getPosition(window_);
                dragCurrent_ = window_.mapPixelToCoords(pixelPos);
                sf::Vertex line[] = {sf::Vertex(dragStart_, sf::Color::Black),
                                     sf::Vertex(dragCurrent_, sf::Color::Black)};
                window_.draw(line, 2, sf::PrimitiveType::Lines);
            }
            renderHUD();
            popups_.draw(window_);
            
            // Debug: Draw collision boxes if T key is pressed
            if (showDebugCollisionBoxes_) {
                renderDebugCollisionBoxes();
            }
            break;
        }
        case Scene::Score:
            // Draw win background
            if (winBackgroundSprite_.has_value()) {
                window_.draw(*winBackgroundSprite_);
            }
            renderScoreScreen();
            // Draw score screen buttons
            for (const auto& btn : scoreButtons_) {
                btn->draw(window_);
            }
            break;
        case Scene::GameOver: {
            std::string failText = "关卡失败！";
            sf::Text t(font_, sf::String::fromUtf8(failText.begin(), failText.end()), 32);
            t.setFillColor(sf::Color::Red);
            t.setStyle(sf::Text::Bold);
            // Center text
            t.setPosition(sf::Vector2f(config::kWindowWidth * 0.5f, 250.f));
            sf::FloatRect textBounds = t.getLocalBounds();
            sf::Vector2f textSize = textBounds.size;
            t.setOrigin(sf::Vector2f(textSize.x * 0.5f, 0.0f));
            window_.draw(t);
            
            // Draw game over buttons
            for (const auto& btn : scoreButtons_) {
                btn->draw(window_);
            }
            break;
        }
        case Scene::Paused:
            // Draw the game scene in the background (frozen)
            // Draw visible ground using ground.png - extend to cover full game world
            {
                const float groundLeft = -200.0f;
                const float groundRight = 1600.0f;
                const float groundWidth = groundRight - groundLeft;
                const float groundHeight = 40.0f;  // 保持与原有高度一致
                const float groundY = static_cast<float>(config::kWindowHeight) - 10.0f;  // 保持与原有位置一致
                
                if (groundTexture_.getSize().x > 0) {
                    // 使用ground.png贴图
                    sf::Sprite groundSprite(groundTexture_);
                    sf::Vector2u groundTexSize = groundTexture_.getSize();
                    float texWidth = static_cast<float>(groundTexSize.x);
                    float texHeight = static_cast<float>(groundTexSize.y);
                    
                    // 设置纹理为可重复模式
                    groundTexture_.setRepeated(true);
                    
                    // 设置纹理矩形，使用实际地面尺寸（会自动重复）
                    groundSprite.setTextureRect(sf::IntRect(
                        sf::Vector2i(0, 0),
                        sf::Vector2i(static_cast<int>(groundWidth), static_cast<int>(groundHeight))
                    ));
                    
                    // 设置位置和原点
                    groundSprite.setOrigin(sf::Vector2f(groundWidth * 0.5f, groundHeight * 0.5f));
                    groundSprite.setPosition({(groundLeft + groundRight) * 0.5f, groundY});
                    
                    window_.draw(groundSprite);
                } else {
                    // 如果贴图加载失败，使用备用颜色块
                    sf::RectangleShape groundShape({groundWidth, groundHeight});
                    groundShape.setOrigin({groundWidth * 0.5f, groundHeight * 0.5f});
                    groundShape.setPosition({(groundLeft + groundRight) * 0.5f, groundY});
                    groundShape.setFillColor(sf::Color(110, 180, 80));
                    window_.draw(groundShape);
                }
            }
            
            // Draw slingshot
            if (slingshotSprite_.has_value()) {
                slingshotSprite_->setPosition(slingshotPos_);
                window_.draw(*slingshotSprite_);
            }
            
            for (auto& b : blocks_) b->draw(window_);
            for (auto& p : pigs_) p->draw(window_);
            for (auto& b : birds_) b->draw(window_);
            
            renderHUD();
            renderPauseMenu();
            break;
        case Scene::LevelEditor:
            if (levelEditor_) {
                levelEditor_->render();
            }
            break;
    }
    window_.display();
}

void Game::renderMenu() {
    // Menu screen - no title text
    // Buttons are drawn in render() function
}

void Game::renderLevelSelect() {
    std::string levelSelectText = "选择关卡";
    sf::Text title(font_, sf::String::fromUtf8(levelSelectText.begin(), levelSelectText.end()), 36);
    title.setFillColor(sf::Color::Black);
    title.setStyle(sf::Text::Bold);
    // Center text
    title.setPosition(sf::Vector2f(config::kWindowWidth * 0.5f, 160.f));
    sf::FloatRect titleBounds = title.getLocalBounds();
    sf::Vector2f titleSize = titleBounds.size;
    title.setOrigin(sf::Vector2f(titleSize.x * 0.5f, 0.0f));
    window_.draw(title);
    
    // Level select buttons are drawn in render() function
}

void Game::renderHUD() {
    scoreSystem_.draw(window_, 20, 20);
    sf::Text birdsText(font_, "Birds: " + std::to_string(birds_.size()), 20);
    birdsText.setFillColor(sf::Color::Black);
    birdsText.setPosition({20.f, 50.f});
    window_.draw(birdsText);
    
    // Display pig count in top-left corner
    sf::Text pigsText(font_, "count_pig: " + std::to_string(pigs_.size()), 20);
    pigsText.setFillColor(sf::Color::Black);
    pigsText.setPosition({20.f, 80.f});
    window_.draw(pigsText);
}

void Game::renderScoreScreen() {
    std::string scoreText = "关卡完成！分数: " + std::to_string(scoreSystem_.score());
    sf::Text t(font_, sf::String::fromUtf8(scoreText.begin(), scoreText.end()), 32);
    t.setFillColor(sf::Color::Green);
    t.setStyle(sf::Text::Bold);
    // Center text
    t.setPosition(sf::Vector2f(config::kWindowWidth * 0.5f, 300.f));
    sf::FloatRect textBounds = t.getLocalBounds();
    sf::Vector2f textSize = textBounds.size;
    t.setOrigin(sf::Vector2f(textSize.x * 0.5f, 0.0f));
    window_.draw(t);
    
    // Buttons are drawn in render() function
}

bool Game::canLaunchBird() const {
    if (birds_.empty()) return false;
    const auto& currentBird = *birds_.front();
    
    // Can launch if current bird is not launched yet (no cooldown restriction)
    // This is the key check - if current bird is launched, we can't launch it again
    // But we should be able to launch the NEXT bird immediately
    return !currentBird.isLaunched();
}

void Game::updateLaunchState(float dt) {
    // Note: Bird removal is handled in main update() loop, not here
    // This function only manages state transitions
    
    if (birds_.empty()) {
        launchState_ = LaunchState::Ready;
        return;
    }
    
    const auto& currentBird = *birds_.front();
    
    // If current bird is launched, check if next bird should be moved to slingshot
    // This is handled in main update loop to check distance first
    if (currentBird.isLaunched()) {
        launchState_ = LaunchState::Ready;
        // Next bird movement is handled in main update loop with distance check
        return;
    }
    
    // State transitions - simplified logic: no cooldown, immediately ready for next bird
    // Key: if current bird is launched, we can immediately launch the NEXT bird
    switch (launchState_) {
        case LaunchState::Ready:
            // If current bird is launched, we're ready for next bird (which will be front after removal)
            // If current bird is not launched, we're ready to launch it
            // Also ensure next bird is at slingshot position
            if (!currentBird.isLaunched() && birds_.size() > 1) {
                // Check if next bird needs to be moved to slingshot
                // This is handled in main update loop when current bird is removed
            }
            break;
            
        case LaunchState::Dragging:
            // If bird was launched (by external means), transition to Ready immediately
            if (currentBird.isLaunched()) {
                launchState_ = LaunchState::Ready;
            }
            break;
            
        case LaunchState::Launched:
            // Immediately go to Ready - no cooldown
            // This allows next bird to be launched immediately
            launchState_ = LaunchState::Ready;
            break;
            
        case LaunchState::Cooldown:
            // Immediately go to Ready - no cooldown
            launchState_ = LaunchState::Ready;
            break;
    }
}

void Game::handleSkillInput() {
    if (birds_.empty()) return;
    
    auto& currentBird = *birds_.front();
    
    // Check for skill activation input
    bool spacePressed = false;
    bool rightPressed = false;
    
    // Check current state
    bool spaceDown = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space);
    bool rightDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Right);
    
    // Detect just-pressed events
    if (spaceDown && !prevSpaceDown_) {
        spacePressed = true;
    }
    if (rightDown && !prevRightDown_) {
        rightPressed = true;
    }
    
    // Update previous state
    prevSpaceDown_ = spaceDown;
    prevRightDown_ = rightDown;
    
    // Activate skill if input detected
    if (spacePressed || rightPressed) {
        // Check if skill can be used based on bird type
        bool canUseSkill = true;
        
        if (currentBird.type() == BirdType::Yellow) {
            // Yellow bird: must be launched and moving
            canUseSkill = currentBird.isLaunched();
        } else if (currentBird.type() == BirdType::Bomb) {
            // Bomb bird: can be used anytime (even before launch)
            canUseSkill = true;
        } else if (currentBird.type() == BirdType::Red) {
            // Red bird: can be used anytime
            canUseSkill = true;
        }
        
        if (canUseSkill) {
            std::string birdTypeStr;
            switch (currentBird.type()) {
                case BirdType::Red: birdTypeStr = "红鸟"; break;
                case BirdType::Yellow: birdTypeStr = "黄鸟"; break;
                case BirdType::Bomb: birdTypeStr = "炸弹鸟"; break;
            }
            Logger::getInstance().info("激活技能: " + birdTypeStr);
            currentBird.activateSkill();
        }
    }
}

void Game::launchCurrentBird() {
    if (birds_.empty()) return;
    
    // KEY FIX: Use draggingBird_ if available, otherwise fall back to front bird
    Bird* birdToLaunch = nullptr;
    if (draggingBird_) {
        // Use the bird we're actually dragging
        birdToLaunch = draggingBird_;
    } else {
        // Fallback: determine which bird to launch
        if (birds_.front()->isLaunched() && birds_.size() > 1 && nextBirdMovedToSlingshot_) {
            // Current bird is launched, launch the next bird (at index 1)
            birdToLaunch = birds_[1].get();
        } else {
            // Launch current bird
            birdToLaunch = birds_.front().get();
        }
    }
    
    if (!birdToLaunch) return;
    
    // 记录小鸟类型
    std::string birdTypeStr;
    switch (birdToLaunch->type()) {
        case BirdType::Red: birdTypeStr = "红鸟"; break;
        case BirdType::Yellow: birdTypeStr = "黄鸟"; break;
        case BirdType::Bomb: birdTypeStr = "炸弹鸟"; break;
    }
    
    sf::Vector2f pull = dragStart_ - dragCurrent_;
    
    // 对于黄鸟，只在AI模式下允许更大的拉弓距离（2倍）
    // 手动模式下使用正常距离，技能激活后速度才翻倍
    float maxPullDist = config::kMaxPullDistance;
    if (birdToLaunch->type() == BirdType::Yellow && aiModeEnabled_) {
        maxPullDist = config::kMaxPullDistance * 2.0f;
    }
    pull = clampVec(pull, maxPullDist);
    
    // 修正：pull向量指向从拖拽点回到弹弓的方向（向后拉的方向）
    // 初速度应该指向发射方向（向前，与pull相反），所以应该是pull方向，而不是-pull
    // 但为了保持向后拉动向前发射的正确逻辑，应该使用pull（而不是-pull）
    sf::Vector2f impulse = pull * config::kSlingshotStiffness;
    
    birdToLaunch->launch(impulse);
    
    Logger::getInstance().info("发射小鸟: " + birdTypeStr + " (剩余: " + std::to_string(birds_.size() - 1) + ")");
    
    // Play bird flying sound when bird is launched
    playBirdFlyingSound(birdToLaunch->type());
    
    // Don't move next bird immediately - wait for launched bird to move away
    // This prevents next bird from blocking current bird's launch
    nextBirdMovedToSlingshot_ = false;
}

void Game::loadLevel(int index) {
    Logger::getInstance().info("加载关卡: " + std::to_string(index));
    
    // 关闭AI功能（任意方式换关时都关闭）
    aiModeEnabled_ = false;
    if (aiController_) {
        aiController_->setEnabled(false);
    }
    
    // Reset bird selection state when loading new level
    birdSelected_ = false;
    levelIndex_ = index;
    blocks_.clear();
    pigs_.clear();
    birds_.clear();
    gameTime_ = 0.0f;  // Reset game time
    lastBirdLaunchTime_ = 0.0f;  // Reset launch timer
    launchState_ = LaunchState::Ready;  // Reset launch state
    nextBirdMovedToSlingshot_ = false;  // Reset next bird movement flag
    draggingBird_ = nullptr;  // Clear dragging bird reference
    physics_ = PhysicsWorld({0.f, config::kGravity});  // Uses move assignment

    // Ground plane (static Box2D body) with very high friction to stop rolling
    // Extend ground to cover full game world (from x=-200 to x=1600)
    const float groundLeft = -200.0f;
    const float groundRight = 1600.0f;
    const float groundWidth = groundRight - groundLeft;
    sf::Vector2f groundPos((groundLeft + groundRight) * 0.5f,
                           static_cast<float>(config::kWindowHeight) - 10.0f);
    sf::Vector2f groundSize(groundWidth, 20.0f);
    physics_.createBoxBody(groundPos, groundSize, 0.0f, 2.0f, 0.1f, false, false, true, nullptr);

    try {
        currentLevel_ = levelLoader_.load(config::levelPath(index));
    } catch (const std::exception& e) {
        Logger::getInstance().error("关卡加载失败: " + std::string(e.what()));
        std::cerr << e.what() << "\n";
        return;
    }

    // Apply slingshot position from level data (fallback to config defaults)
    slingshotPos_ = currentLevel_.slingshot;

    // Convert JSON positions (top-left) to Box2D center positions
    for (auto& b : currentLevel_.blocks) {
        Material mat = getMaterialOrDefault(b.material);
        // JSON position is top-left, Box2D needs center: center = topLeft + size * 0.5f
        sf::Vector2f centerPos = b.position + b.size * 0.5f;
        blocks_.push_back(std::make_unique<Block>(mat, centerPos, b.size, physics_));
    }
    for (auto& p : currentLevel_.pigs) {
        // For circles, JSON position should already be center, but ensure it's correct
        // If JSON uses top-left, we'd need radius, but we don't have that in the spec
        // Assume JSON position is center for pigs (common convention)
        pigs_.push_back(std::make_unique<Pig>(p.type, p.position, physics_));
    }
    for (auto& b : currentLevel_.birds) {
        // For circles, JSON position should already be center
        birds_.push_back(std::make_unique<Bird>(b.type, b.position, physics_));
    }
    
    Logger::getInstance().info("关卡加载成功 - 方块数: " + std::to_string(blocks_.size()) +
                               ", 猪数: " + std::to_string(pigs_.size()) +
                               ", 小鸟数: " + std::to_string(birds_.size()));

    // Let the physics world settle to resolve any initial overlaps
    // Box2D will automatically resolve overlaps during settle phase
    // Use more iterations for better overlap resolution
    // KEY FIX: Increased settle time and iterations to fix collision box overlaps
    for (int i = 0; i < 600; ++i) {  // 10 seconds at 60 FPS - more time for better settling
        physics_.step(config::kFixedDelta);
        
        // Additional check: ensure no overlaps persist
        // This helps with "gray rectangles" (stone/stoneslab) overlapping issues
        if (i % 60 == 0) {  // Check every second
            // Force position updates to ensure collision boxes are correct
            for (auto& block : blocks_) {
                if (block->body() && block->body()->active()) {
                    // Get current position and ensure it's correct
                    sf::Vector2f pos = block->position();
                    block->body()->setPosition(pos);  // Force update
                }
            }
            for (auto& pig : pigs_) {
                if (pig->body() && pig->body()->active()) {
                    sf::Vector2f pos = pig->position();
                    pig->body()->setPosition(pos);  // Force update
                }
            }
        }
    }

    scoreSystem_.resetRound();
}

void Game::resetCurrent() { loadLevel(levelIndex_); }

void Game::initButtons() {
    // Main menu buttons
    menuButtons_.clear();
    
    auto startBtn = std::make_unique<Button>("开始", font_, sf::Vector2f(400.0f, 250.0f), sf::Vector2f(200.0f, 50.0f));
    startBtn->setCallback([this]() { 
        loadLevel(levelIndex_); 
        scene_ = Scene::Playing;
        Logger::getInstance().info("场景切换: MainMenu -> Playing");
    });
    menuButtons_.push_back(std::move(startBtn));
    
    auto levelBtn = std::make_unique<Button>("选关", font_, sf::Vector2f(400.0f, 320.0f), sf::Vector2f(200.0f, 50.0f));
    levelBtn->setCallback([this]() { 
        scene_ = Scene::LevelSelect;
        Logger::getInstance().info("场景切换: MainMenu -> LevelSelect");
    });
    menuButtons_.push_back(std::move(levelBtn));
    
    auto editorBtn = std::make_unique<Button>("关卡编辑器", font_, sf::Vector2f(400.0f, 390.0f), sf::Vector2f(200.0f, 50.0f));
    editorBtn->setCallback([this]() { 
        if (!levelEditor_) {
            levelEditor_ = std::make_unique<LevelEditor>(window_, font_, this);
        }
        scene_ = Scene::LevelEditor;
        Logger::getInstance().info("场景切换: MainMenu -> LevelEditor");
    });
    menuButtons_.push_back(std::move(editorBtn));
    
    auto quitBtn = std::make_unique<Button>("退出", font_, sf::Vector2f(400.0f, 460.0f), sf::Vector2f(200.0f, 50.0f));
    quitBtn->setCallback([this]() { window_.close(); });
    menuButtons_.push_back(std::move(quitBtn));
    
    // Game buttons (during gameplay) - positioned at top-right corner
    gameButtons_.clear();
    
    float gameBtnX = config::kWindowWidth - 120.0f;
    float gameBtnY = 20.0f;
    
    // Auto按钮（AI模式开关）
    auto autoBtn = std::make_unique<Button>("Auto", font_, sf::Vector2f(gameBtnX, gameBtnY), sf::Vector2f(100.0f, 40.0f));
    autoBtn->setCallback([this]() { 
        aiModeEnabled_ = !aiModeEnabled_;
        if (aiController_) {
            aiController_->setEnabled(aiModeEnabled_);
        }
        Logger::getInstance().info("AI模式切换(按钮): " + std::string(aiModeEnabled_ ? "开启" : "关闭"));
    });
    gameButtons_.push_back(std::move(autoBtn));
    
    auto restartBtn = std::make_unique<Button>("重新开始", font_, sf::Vector2f(gameBtnX, gameBtnY + 50.0f), sf::Vector2f(100.0f, 40.0f));
    restartBtn->setCallback([this]() { 
        Logger::getInstance().info("重新开始当前关卡");
        resetCurrent(); 
        scene_ = Scene::Playing; 
    });
    gameButtons_.push_back(std::move(restartBtn));
    
    auto nextLevelBtn = std::make_unique<Button>("下一关", font_, sf::Vector2f(gameBtnX, gameBtnY + 100.0f), sf::Vector2f(100.0f, 40.0f));
    nextLevelBtn->setCallback([this]() { 
        levelIndex_ = std::min(levelIndex_ + 1, 8);
        loadLevel(levelIndex_);
        scene_ = Scene::Playing;
        // AI已在loadLevel中关闭，这里不需要重复关闭
    });
    gameButtons_.push_back(std::move(nextLevelBtn));
    
    // Pause menu buttons
    pauseButtons_.clear();
    
    auto resumeBtn = std::make_unique<Button>("继续", font_, sf::Vector2f(400.0f, 250.0f), sf::Vector2f(200.0f, 50.0f));
    resumeBtn->setCallback([this]() { 
        scene_ = Scene::Playing;
        Logger::getInstance().info("场景切换: Paused -> Playing");
    });
    pauseButtons_.push_back(std::move(resumeBtn));
    
    auto pauseRestartBtn = std::make_unique<Button>("重新开始", font_, sf::Vector2f(400.0f, 320.0f), sf::Vector2f(200.0f, 50.0f));
    pauseRestartBtn->setCallback([this]() { 
        Logger::getInstance().info("从暂停菜单重新开始关卡");
        resetCurrent(); 
        scene_ = Scene::Playing; 
    });
    pauseButtons_.push_back(std::move(pauseRestartBtn));
    
    auto pauseLevelBtn = std::make_unique<Button>("选关", font_, sf::Vector2f(400.0f, 390.0f), sf::Vector2f(200.0f, 50.0f));
    pauseLevelBtn->setCallback([this]() { 
        scene_ = Scene::LevelSelect;
        Logger::getInstance().info("场景切换: Paused -> LevelSelect");
    });
    pauseButtons_.push_back(std::move(pauseLevelBtn));
    
    // Level select buttons
    levelSelectButtons_.clear();
    float startX = 200.0f;
    float startYLevel = 250.0f;
    float spacingX = 150.0f;
    float spacingY = 60.0f;
    
    for (int i = 1; i <= 8; ++i) {
        float x = startX + ((i - 1) % 4) * spacingX;
        float y = startYLevel + ((i - 1) / 4) * spacingY;
        
        auto levelBtn = std::make_unique<Button>("关卡 " + std::to_string(i), font_, 
                                                   sf::Vector2f(x, y), sf::Vector2f(120.0f, 50.0f));
        int levelNum = i;  // Capture for lambda
        levelBtn->setCallback([this, levelNum]() {
            levelIndex_ = levelNum;
            loadLevel(levelNum);
            scene_ = Scene::Playing;
            Logger::getInstance().info("场景切换: LevelSelect -> Playing (关卡 " + std::to_string(levelNum) + ")");
        });
        levelSelectButtons_.push_back(std::move(levelBtn));
    }
    
    // Back button for level select
    float centerX = config::kWindowWidth * 0.5f;
    auto backBtn = std::make_unique<Button>("返回", font_, sf::Vector2f(centerX - 100.0f, 450.0f), sf::Vector2f(200.0f, 50.0f));
    backBtn->setCallback([this]() { 
        scene_ = Scene::MainMenu;
        Logger::getInstance().info("场景切换: LevelSelect -> MainMenu");
    });
    levelSelectButtons_.push_back(std::move(backBtn));
    
    // Score screen buttons (also used for GameOver)
    scoreButtons_.clear();
    float scoreBtnY = 360.0f;
    float buttonSpacing = 70.0f;
    
    auto scoreNextBtn = std::make_unique<Button>("下一关", font_, 
        sf::Vector2f(centerX - 100.0f, scoreBtnY), sf::Vector2f(200.0f, 50.0f));
    scoreNextBtn->setCallback([this]() {
        levelIndex_ = std::min(levelIndex_ + 1, 8);
        loadLevel(levelIndex_);
        scene_ = Scene::Playing;
        Logger::getInstance().info("场景切换: Score -> Playing (下一关)");
        // AI已在loadLevel中关闭，这里不需要重复关闭
    });
    scoreButtons_.push_back(std::move(scoreNextBtn));
    
    auto scoreRetryBtn = std::make_unique<Button>("重新开始", font_, 
        sf::Vector2f(centerX - 100.0f, scoreBtnY + buttonSpacing), sf::Vector2f(200.0f, 50.0f));
    scoreRetryBtn->setCallback([this]() {
        Logger::getInstance().info("从得分界面重新开始关卡");
        resetCurrent();
        scene_ = Scene::Playing;
    });
    scoreButtons_.push_back(std::move(scoreRetryBtn));
    
    auto scoreLevelSelectBtn = std::make_unique<Button>("选关", font_, 
        sf::Vector2f(centerX - 100.0f, scoreBtnY + buttonSpacing * 2), sf::Vector2f(200.0f, 50.0f));
    scoreLevelSelectBtn->setCallback([this]() { 
        scene_ = Scene::LevelSelect;
        Logger::getInstance().info("场景切换: Score -> LevelSelect");
    });
    scoreButtons_.push_back(std::move(scoreLevelSelectBtn));
}

void Game::updateButtons(float dt) {
    // 使用 mapPixelToCoords 将鼠标像素坐标转换为窗口坐标，正确处理窗口缩放
    sf::Vector2i pixelPos = sf::Mouse::getPosition(window_);
    sf::Vector2f mousePos = window_.mapPixelToCoords(pixelPos);
    bool mousePressed = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    
    // Update buttons based on current scene
    switch (scene_) {
        case Scene::MainMenu:
            for (auto& btn : menuButtons_) {
                btn->update(mousePos, mousePressed);
            }
            break;
        case Scene::LevelSelect:
            for (auto& btn : levelSelectButtons_) {
                btn->update(mousePos, mousePressed);
            }
            break;
        case Scene::Playing:
            for (auto& btn : gameButtons_) {
                btn->update(mousePos, mousePressed);
            }
            break;
        case Scene::Paused:
            for (auto& btn : pauseButtons_) {
                btn->update(mousePos, mousePressed);
            }
            break;
        case Scene::Score:
        case Scene::GameOver:
            for (auto& btn : scoreButtons_) {
                btn->update(mousePos, mousePressed);
            }
            break;
        default:
            break;
    }
}



void Game::renderPauseMenu() {
    // Draw semi-transparent overlay
    sf::RectangleShape overlay;
    overlay.setSize(sf::Vector2f(static_cast<float>(config::kWindowWidth), static_cast<float>(config::kWindowHeight)));
    overlay.setFillColor(sf::Color(0, 0, 0, 180));  // Semi-transparent black
    window_.draw(overlay);
    
    // Draw pause title
    std::string pauseText = "游戏暂停";
    sf::Text title(font_, sf::String::fromUtf8(pauseText.begin(), pauseText.end()), 40);
    title.setFillColor(sf::Color::White);
    title.setStyle(sf::Text::Bold);
    // Center title text
    sf::FloatRect titleBounds = title.getLocalBounds();
    sf::Vector2f titleSize = titleBounds.size;
    sf::Vector2f titlePos = titleBounds.position;
    title.setOrigin(sf::Vector2f(titlePos.x + titleSize.x * 0.5f, titlePos.y + titleSize.y * 0.5f));
    title.setPosition(sf::Vector2f(config::kWindowWidth * 0.5f, 150.0f));
    window_.draw(title);
    
    // Draw pause buttons
    for (const auto& btn : pauseButtons_) {
        btn->draw(window_);
    }
}

void Game::initAudio() {
    // Load music files
    if (!titleTheme_.openFromFile("music/title_theme.mp3")) {
        std::cerr << "警告: 无法加载音乐文件 music/title_theme.mp3\n";
    }
    if (!gameComplete_.openFromFile("music/game_complete.mp3")) {
        std::cerr << "警告: 无法加载音乐文件 music/game_complete.mp3\n";
    }
    if (!birdsOutro_.openFromFile("music/birds_outro.mp3")) {
        std::cerr << "警告: 无法加载音乐文件 music/birds_outro.mp3\n";
    }
    
    // Load bird select sound buffers
    if (!birdSelectBuffers_[0].loadFromFile("music/bird 01 select.wav")) {
        std::cerr << "警告: 无法加载音效文件 music/bird 01 select.wav\n";
    }
    if (!birdSelectBuffers_[1].loadFromFile("music/bird 02 select.wav")) {
        std::cerr << "警告: 无法加载音效文件 music/bird 02 select.wav\n";
    }
    if (!birdSelectBuffers_[2].loadFromFile("music/bird 03 select.wav")) {
        std::cerr << "警告: 无法加载音效文件 music/bird 03 select.wav\n";
    }
    
    // Load bird flying sound buffers
    if (!birdFlyingBuffers_[0].loadFromFile("music/bird 01 flying.wav")) {
        std::cerr << "警告: 无法加载音效文件 music/bird 01 flying.wav\n";
    }
    if (!birdFlyingBuffers_[1].loadFromFile("music/bird 02 flying.wav")) {
        std::cerr << "警告: 无法加载音效文件 music/bird 02 flying.wav\n";
    }
    if (!birdFlyingBuffers_[2].loadFromFile("music/bird 03 flying.wav")) {
        std::cerr << "警告: 无法加载音效文件 music/bird 03 flying.wav\n";
    }
    
    // Set up sounds - initialize with first buffer if available
    if (birdSelectBuffers_[0].getSampleCount() > 0) {
        birdSelectSound_ = sf::Sound(birdSelectBuffers_[0]);
    }
    if (birdFlyingBuffers_[0].getSampleCount() > 0) {
        birdFlyingSound_ = sf::Sound(birdFlyingBuffers_[0]);
    }
    
    // Set music to loop - SFML 3.0 uses setLooping
    titleTheme_.setLooping(true);
    gameComplete_.setLooping(true);
    birdsOutro_.setLooping(true);
}

void Game::updateMusic() {
    // Handle music transitions based on scene changes
    if (scene_ != previousScene_) {
        // Stop all music when scene changes
        titleTheme_.stop();
        gameComplete_.stop();
        birdsOutro_.stop();
        
        // Play appropriate music for new scene
        switch (scene_) {
            case Scene::MainMenu:
            case Scene::LevelSelect:
                if (titleTheme_.getStatus() != sf::SoundSource::Status::Playing) {
                    titleTheme_.play();
                }
                break;
            case Scene::Playing:
                // Play birds_outro when game starts, before bird is selected
                if (!birdSelected_ && birdsOutro_.getStatus() != sf::SoundSource::Status::Playing) {
                    birdsOutro_.play();
                }
                break;
            case Scene::Score:
            case Scene::GameOver:
                if (gameComplete_.getStatus() != sf::SoundSource::Status::Playing) {
                    gameComplete_.play();
                }
                break;
            default:
                break;
        }
        
        previousScene_ = scene_;
    }
    
    // Handle birds_outro -> stop when bird is selected
    if (scene_ == Scene::Playing && birdSelected_ && birdsOutro_.getStatus() == sf::SoundSource::Status::Playing) {
        birdsOutro_.stop();
    }
}

void Game::playBirdSelectSound(BirdType type) {
    int index = 0;  // Default to Red
    switch (type) {
        case BirdType::Red:
            index = 0;
            break;
        case BirdType::Yellow:
            index = 1;
            break;
        case BirdType::Bomb:
            index = 2;
            break;
    }
    
    if (birdSelectBuffers_[index].getSampleCount() > 0) {
        if (!birdSelectSound_) {
            birdSelectSound_ = sf::Sound(birdSelectBuffers_[index]);
        } else {
            birdSelectSound_->setBuffer(birdSelectBuffers_[index]);
        }
        birdSelectSound_->play();
    }
}

void Game::playBirdFlyingSound(BirdType type) {
    int index = 0;  // Default to Red
    switch (type) {
        case BirdType::Red:
            index = 0;
            break;
        case BirdType::Yellow:
            index = 1;
            break;
        case BirdType::Bomb:
            index = 2;
            break;
    }
    
    if (birdFlyingBuffers_[index].getSampleCount() > 0) {
        if (!birdFlyingSound_) {
            birdFlyingSound_ = sf::Sound(birdFlyingBuffers_[index]);
        } else {
            birdFlyingSound_->setBuffer(birdFlyingBuffers_[index]);
        }
        birdFlyingSound_->play();
    }
}

void Game::renderDebugCollisionBoxes() {
    // Draw collision boxes for all physics bodies
    b2World* world = physics_.world();
    if (!world) return;
    
    // Color scheme for different entity types
    sf::Color blockColor(255, 0, 0, 180);      // Red for blocks
    sf::Color pigColor(0, 255, 0, 180);       // Green for pigs
    sf::Color birdColor(0, 0, 255, 180);       // Blue for birds
    sf::Color defaultColor(255, 255, 0, 180); // Yellow for unknown
    
    for (b2Body* body = world->GetBodyList(); body; body = body->GetNext()) {
        if (!body->IsEnabled()) continue;
        
        // Get fixture to determine entity type
        b2Fixture* fixture = body->GetFixtureList();
        if (!fixture) continue;
        
        FixtureUserData* userData = reinterpret_cast<FixtureUserData*>(
            fixture->GetUserData().pointer);
        
        sf::Color drawColor = defaultColor;
        if (userData) {
            if (userData->isBird) {
                drawColor = birdColor;
            } else if (userData->entityPtr) {
                // Try to determine if it's a pig or block
                Entity* entity = static_cast<Entity*>(userData->entityPtr);
                if (dynamic_cast<Pig*>(entity)) {
                    drawColor = pigColor;
                } else if (dynamic_cast<Block*>(entity)) {
                    drawColor = blockColor;
                }
            }
        }
        
        // Draw all fixtures of this body
        for (b2Fixture* f = body->GetFixtureList(); f; f = f->GetNext()) {
            b2Shape* shape = f->GetShape();
            
            if (shape->GetType() == b2Shape::e_polygon) {
                // Box shape (blocks)
                b2PolygonShape* polyShape = static_cast<b2PolygonShape*>(shape);
                int32 vertexCount = polyShape->m_count;
                
                // Create SFML vertices for the polygon
                std::vector<sf::Vertex> vertices;
                for (int32 i = 0; i < vertexCount; ++i) {
                    b2Vec2 worldVertex = body->GetWorldPoint(polyShape->m_vertices[i]);
                    sf::Vector2f pixelPos = PhysicsWorld::meterToPixel(worldVertex);
                    vertices.push_back(sf::Vertex(pixelPos, drawColor));
                }
                // Close the polygon
                if (!vertices.empty()) {
                    vertices.push_back(vertices[0]);
                }
                
                // Draw outline
                if (vertices.size() >= 2) {
                    window_.draw(vertices.data(), vertices.size(), sf::PrimitiveType::LineStrip);
                }
            } else if (shape->GetType() == b2Shape::e_circle) {
                // Circle shape (pigs, birds)
                b2CircleShape* circleShape = static_cast<b2CircleShape*>(shape);
                b2Vec2 center = body->GetWorldPoint(circleShape->m_p);
                float radius = PhysicsWorld::meterToPixel(circleShape->m_radius);
                sf::Vector2f centerPos = PhysicsWorld::meterToPixel(center);
                
                // Draw circle outline
                sf::CircleShape debugCircle(radius);
                debugCircle.setOrigin(sf::Vector2f(radius, radius));  // SFML 3.0 requires Vector2f
                debugCircle.setPosition(centerPos);
                debugCircle.setFillColor(sf::Color::Transparent);
                debugCircle.setOutlineColor(drawColor);
                debugCircle.setOutlineThickness(2.0f);
                window_.draw(debugCircle);
            }
        }
    }
}

// ===================== 主界面动画 =====================

void Game::updateMenuAnimation(float dt) {
    // 地面向左无限滚动
    menuGroundOffset_ -= menuGroundSpeed_ * dt;
    
    // 天空向左无限滚动（速度为地面的1/2）
    float skySpeed = menuGroundSpeed_ * 0.5f;
    menuSkyOffset_ -= skySpeed * dt;
    
    // 使用模运算来保持偏移量在合理范围内，避免数值过大
    // 使用最小公倍数周期进行模运算，确保地面和草同步
    if (menuCycleLCM_ > 0.0f) {
        // 使用模运算确保偏移量在 [-menuCycleLCM_, 0) 范围内
        while (menuGroundOffset_ < -menuCycleLCM_) {
            menuGroundOffset_ += menuCycleLCM_;
        }
    } else {
        // 如果没有计算LCM，使用窗口宽度作为备用
        float cycleWidth = static_cast<float>(config::kWindowWidth);
        while (menuGroundOffset_ < -cycleWidth) {
            menuGroundOffset_ += cycleWidth;
        }
    }
    
    // 天空偏移量重置：确保天空移动了一整个窗口宽度后才重置
    // 这样无论天空图片宽度是多少，都能确保重置时位置一致
    float windowWidth = static_cast<float>(config::kWindowWidth);
    while (menuSkyOffset_ < -windowWidth) {
        menuSkyOffset_ += windowWidth;
    }

    // 生成视觉小鸟（不参与物理、无声音）
    static std::mt19937 rng{std::random_device{}()};
    static std::uniform_real_distribution<float> spawnIntervalDist(0.8f, 2.0f);   // 间隔 0.8~2 秒
    static std::uniform_real_distribution<float> startXDist(80.0f, 720.0f);       // 出现的水平位置
    static std::uniform_real_distribution<float> speedDist(480.0f, 680.0f);       // 初速度大小（减小20%）
    static std::uniform_real_distribution<float> angleDegDist(30.0f, 60.0f);       // 至少30度向上发射（30-60度）
    static std::uniform_int_distribution<int> birdTypeDist(0, 2);                 // 0 红 1 黄 2 黑

    menuBirdSpawnAccum_ += dt;
    static float nextSpawn = spawnIntervalDist(rng);
    if (menuBirdSpawnAccum_ >= nextSpawn) {
        menuBirdSpawnAccum_ = 0.0f;
        nextSpawn = spawnIntervalDist(rng);

        // 选择贴图
        const sf::Texture* tex = nullptr;
        switch (birdTypeDist(rng)) {
            case 0: tex = splashBirdRedTexture_.getSize().x ? &splashBirdRedTexture_ : nullptr; break;
            case 1: tex = splashBirdYellowTexture_.getSize().x ? &splashBirdYellowTexture_ : nullptr; break;
            case 2: tex = splashBirdBlackTexture_.getSize().x ? &splashBirdBlackTexture_ : nullptr; break;
        }

        if (!tex) {
            // 对应贴图加载失败则跳过本次生成
            return;
        }

        SplashBirdVisual bird(*tex);

        // 把小鸟放在地板上层往下30像素的位置，向上发射
        float groundTextureHeight = groundTextureWidth_ > 0.0f ? static_cast<float>(groundTexture_.getSize().y) * 0.5f : 80.0f;  // 考虑50%缩放
        float groundTopY = static_cast<float>(config::kWindowHeight) - groundTextureHeight;  // 地面上表面
        const float launchY = groundTopY - 30.0f;  // 地板上层往下30像素
        float startX = startXDist(rng);
        bird.sprite.setPosition({startX, launchY});
        sf::FloatRect local = bird.sprite.getLocalBounds();
        bird.sprite.setOrigin({local.size.x * 0.5f, local.size.y * 0.5f});

        float speed = speedDist(rng);
        float angleDeg = angleDegDist(rng);  // 至少30度向上
        float rad = angleDeg * 3.14159265f / 180.0f;
        bird.velocity.x = std::cos(rad) * speed;   // 固定向右飞
        bird.velocity.y = -std::sin(rad) * speed;  // 向上发射（注意负号，因为Y轴向下）

        menuBirds_.push_back(std::move(bird));
    }

    // 更新小鸟位置，加入简单"重力"让其自然落下
    const float gravity = 260.0f;  // 像素/秒²，纯视觉
    for (auto& b : menuBirds_) {
        b.velocity.y += gravity * dt;
        b.sprite.move(b.velocity * dt);
    }

    // 移除飞出屏幕太久的小鸟
    const float bottomLimit = static_cast<float>(config::kWindowHeight) + 80.0f;
    menuBirds_.erase(
        std::remove_if(menuBirds_.begin(), menuBirds_.end(),
                       [bottomLimit](const SplashBirdVisual& b) {
                           return b.sprite.getPosition().y > bottomLimit ||
                                  b.sprite.getPosition().x > static_cast<float>(config::kWindowWidth) + 80.0f;
                       }),
        menuBirds_.end());
}

void Game::renderMenuAnimation() {
    const float windowWidth = static_cast<float>(config::kWindowWidth);
    const float windowHeight = static_cast<float>(config::kWindowHeight);
    
    // 地面贴图底部对齐窗口底部（贴图缩小50%）
    float groundTextureHeight = groundTextureWidth_ > 0.0f ? static_cast<float>(groundTexture_.getSize().y) * 0.5f : 0.0f;
    float grassTextureHeight = grassTextureWidth_ > 0.0f ? static_cast<float>(grassTexture_.getSize().y) * 0.5f : 0.0f;
    
    // 地面底部在窗口底部
    float groundBottomY = windowHeight;
    float groundTopY = groundBottomY - groundTextureHeight;
    
    // 草底部与地面上端无缝衔接
    float grassBottomY = groundTopY;
    float grassTopY = grassBottomY - grassTextureHeight;
    
    // 使用贴图宽度进行循环拼接（考虑50%缩放），如果没有加载贴图则使用窗口宽度
    float groundCycleWidth = groundTextureWidth_ > 0.0f ? groundTextureWidth_ * 0.5f : windowWidth;
    float grassCycleWidth = grassTextureWidth_ > 0.0f ? grassTextureWidth_ * 0.5f : windowWidth;
    
    // ========== 绘制天空背景（最先绘制，置底） ==========
    if (skyTextureWidth_ > 0.0f) {
        // 计算天空需要覆盖的高度：从地面上方到窗口顶部
        float skyTopY = 0.0f;
        float skyBottomY = groundTopY;
        float skyHeight = skyBottomY - skyTopY;
        
        // 获取天空贴图的原始尺寸
        float skyOriginalWidth = static_cast<float>(skyTexture_.getSize().x);
        float skyOriginalHeight = static_cast<float>(skyTexture_.getSize().y);
        
        // 计算缩放比例，使天空贴图完全覆盖从地面上方到窗口顶部的部分
        float skyScaleY = skyHeight / skyOriginalHeight;
        float skyScaleX = skyScaleY;  // 保持宽高比
        float skyScaledWidth = skyOriginalWidth * skyScaleX;
        
        // 使用模运算确保贴图位置正确，基于天空贴图宽度进行归一化
        float normalizedSkyOffset = std::fmod(menuSkyOffset_, skyScaledWidth);
        if (normalizedSkyOffset < 0.0f) {
            normalizedSkyOffset += skyScaledWidth;
        }
        
        // 计算需要绘制多少个天空贴图才能覆盖整个屏幕
        int skyTilesNeeded = static_cast<int>(std::ceil(windowWidth / skyScaledWidth)) + 2;
        
        // 绘制所有需要的天空贴图
        for (int i = -1; i < skyTilesNeeded; ++i) {
            float tileX = normalizedSkyOffset + i * skyScaledWidth;
            // 只绘制在屏幕可见范围内的贴图
            if (tileX + skyScaledWidth >= -skyScaledWidth && tileX < windowWidth + skyScaledWidth) {
                sf::Sprite skySprite(skyTexture_);
                skySprite.setScale(sf::Vector2f(skyScaleX, skyScaleY));
                skySprite.setPosition({tileX, skyTopY});
                window_.draw(skySprite);
            }
        }
    }
    
    // ========== 绘制Logo（在天空之后，鸟之前） ==========
    if (logoSprite_.has_value()) {
        window_.draw(*logoSprite_);
    }
    
    // 计算需要绘制多少个贴图才能覆盖整个屏幕（包括左右各一个额外的用于无缝循环）
    // 增加循环范围，确保地面贴图始终覆盖整个屏幕，即使偏移量很大时也不会消失
    int groundTilesNeeded = static_cast<int>(std::ceil(windowWidth / groundCycleWidth)) + 4;
    int grassTilesNeeded = static_cast<int>(std::ceil(windowWidth / grassCycleWidth)) + 4;
    
    // 先绘制视觉小鸟（在地面之前绘制，这样地面可以遮挡小鸟的下半部分）
    for (auto& b : menuBirds_) {
        window_.draw(b.sprite);
    }
    
    // 绘制地面贴图（无限拼接，缩小50%，在地面之后绘制以遮挡小鸟）
    if (groundTextureWidth_ > 0.0f) {
        // 使用模运算确保贴图位置正确，基于地面贴图宽度进行归一化
        float normalizedOffset = std::fmod(menuGroundOffset_, groundCycleWidth);
        if (normalizedOffset < 0.0f) {
            normalizedOffset += groundCycleWidth;
        }
        
        // 计算起始贴图索引，确保覆盖整个屏幕（扩大范围以确保覆盖）
        int startIndex = static_cast<int>(std::floor((0.0f - normalizedOffset) / groundCycleWidth)) - 2;
        int endIndex = static_cast<int>(std::ceil((windowWidth - normalizedOffset) / groundCycleWidth)) + 2;
        
        // 绘制所有需要的地面贴图
        for (int i = startIndex; i <= endIndex; ++i) {
            float tileX = normalizedOffset + i * groundCycleWidth;
            // 只绘制在屏幕可见范围内的贴图（扩大范围以确保覆盖）
            if (tileX + groundCycleWidth >= -groundCycleWidth * 2.0f && tileX < windowWidth + groundCycleWidth * 2.0f) {
                sf::Sprite groundSprite(groundTexture_);
                groundSprite.setScale(sf::Vector2f(0.5f, 0.5f));  // 缩小50%
                groundSprite.setPosition({tileX, groundTopY});
                window_.draw(groundSprite);
            }
        }
    } else {
        // 如果没有加载地面贴图，使用备用颜色块
        sf::RectangleShape fallback({windowWidth, groundTextureHeight > 0 ? groundTextureHeight : 80.0f});
        fallback.setPosition({0.0f, groundTopY});
        fallback.setFillColor(sf::Color(139, 101, 67));
        window_.draw(fallback);
    }
    
    // 绘制草贴图（无限拼接，地面上方无缝衔接，缩小50%，最后绘制以遮挡小鸟）
    if (grassTextureWidth_ > 0.0f) {
        // 使用模运算确保贴图位置正确，基于草贴图宽度进行归一化
        // 使用与地面相同的偏移量 menuGroundOffset_，但基于草贴图宽度归一化
        float normalizedOffset = std::fmod(menuGroundOffset_, grassCycleWidth);
        if (normalizedOffset < 0.0f) {
            normalizedOffset += grassCycleWidth;
        }
        
        // 计算起始贴图索引，确保覆盖整个屏幕（扩大范围以确保覆盖）
        int startIndex = static_cast<int>(std::floor((0.0f - normalizedOffset) / grassCycleWidth)) - 2;
        int endIndex = static_cast<int>(std::ceil((windowWidth - normalizedOffset) / grassCycleWidth)) + 2;
        
        // 绘制所有需要的草贴图
        for (int i = startIndex; i <= endIndex; ++i) {
            float tileX = normalizedOffset + i * grassCycleWidth;
            // 只绘制在屏幕可见范围内的贴图（扩大范围以确保覆盖）
            if (tileX + grassCycleWidth >= -grassCycleWidth * 2.0f && tileX < windowWidth + grassCycleWidth * 2.0f) {
                sf::Sprite grassSprite(grassTexture_);
                grassSprite.setScale(sf::Vector2f(0.5f, 0.5f));  // 缩小50%
                grassSprite.setPosition({tileX, grassTopY});
                window_.draw(grassSprite);
            }
        }
    } else {
        // 如果没有加载草贴图，使用备用颜色块
        sf::RectangleShape fallback({windowWidth, grassTextureHeight > 0 ? grassTextureHeight : 30.0f});
        fallback.setPosition({0.0f, grassTopY});
        fallback.setFillColor(sf::Color(60, 170, 80));
        window_.draw(fallback);
    }
}

void Game::updateAI(float dt) {
    if (!aiController_ || !aiModeEnabled_) {
        return;
    }
    
    // 更新AI控制器
    aiController_->update(dt, blocks_, pigs_, birds_, slingshotPos_);
}

void Game::handleAIControl(float dt) {
    if (!aiController_ || !aiModeEnabled_ || birds_.empty()) {
        return;
    }
    
    // 查找第一个未发射的鸟
    Bird* currentBird = nullptr;
    for (auto& bird : birds_) {
        if (bird && !bird->isLaunched()) {
            currentBird = bird.get();
            break;
        }
    }
    
    if (!currentBird) {
        return;  // 没有未发射的鸟
    }
    
    // 如果AI要求发射
    if (aiController_->shouldLaunch()) {
        // 确保鸟在弹弓位置
        auto* body = currentBird->body();
        if (body && !currentBird->isLaunched()) {
            sf::Vector2f currentPos = body->position();
            float distToSlingshot = std::sqrt(
                (currentPos.x - slingshotPos_.x) * (currentPos.x - slingshotPos_.x) +
                (currentPos.y - slingshotPos_.y) * (currentPos.y - slingshotPos_.y)
            );
            
            // 如果鸟不在弹弓位置附近，移动它到位置
            if (distToSlingshot > 20.0f) {
                body->setPosition(slingshotPos_);
                body->setDynamic(false);
                body->setVelocity({0.0f, 0.0f});
                // 等待下一帧再发射，确保位置正确
                return;
            }
            
            // 获取AI计算的瞄准结果
            const auto& aim = aiController_->getCurrentAim();
            if (aim.isValid) {
                // 检查是否是黄鸟且需要激活技能
                bool isYellowBird = (currentBird->type() == BirdType::Yellow);
                bool needSkillActivation = aiController_->shouldActivateSkill();
                
                // 设置拖拽状态
                dragStart_ = slingshotPos_;
                dragCurrent_ = aim.dragEnd;
                draggingBird_ = currentBird;
                
                // 播放选择音效
                playBirdSelectSound(currentBird->type());
                birdSelected_ = true;
                
                // 设置发射状态
                launchState_ = LaunchState::Dragging;
                
                // 如果是黄鸟，先获取指向（因为发射后currentBird可能变化）
                bool willActivateSkill = (isYellowBird && needSkillActivation);
                
                // 发射鸟
                launchCurrentBird();
                
                // 如果是黄鸟且需要激活技能，立即激活（必须在发射后的同一帧执行）
                if (willActivateSkill) {
                    // 发射后立即查找已发射的黄鸟
                    Bird* launchedBird = nullptr;
                    // 优先检查刚刚发射的鸟（通常是第一只）
                    if (!birds_.empty() && birds_.front()->isLaunched() && 
                        birds_.front()->type() == BirdType::Yellow) {
                        launchedBird = birds_.front().get();
                    } else {
                        // 如果第一只不是，遍历查找
                        for (auto& bird : birds_) {
                            if (bird && bird->isLaunched() && bird->type() == BirdType::Yellow) {
                                launchedBird = bird.get();
                                break;
                            }
                        }
                    }
                    
                    if (launchedBird && launchedBird->isLaunched()) {
                        // 黄鸟只需要launched()为true就可以激活技能
                        launchedBird->activateSkill();
                        aiController_->resetSkillFlag();
                        Logger::getInstance().info("黄鸟技能立即激活（发射后立即触发，速度翻倍）");
                    } else {
                        Logger::getInstance().info("警告：黄鸟发射后未找到，尝试备用激活");
                    }
                }
                
                // 重置AI发射标志并清除轨迹线
                aiController_->resetLaunchFlag();
                aiController_->clearTrajectory();
            }
        }
    }
    
    // 备用：处理技能激活（如果上面的逻辑没有执行，这里作为备用）
    // 检查所有已发射的黄鸟，找到需要激活技能的
    if (aiController_->shouldActivateSkill()) {
        for (auto& bird : birds_) {
            if (bird && bird->isLaunched() && bird->type() == BirdType::Yellow) {
                // 黄鸟只需要launched()为true就可以激活技能
                if (bird->isLaunched()) {
                    bird->activateSkill();
                    aiController_->resetSkillFlag();
                    Logger::getInstance().info("黄鸟技能立即激活（备用逻辑触发）");
                    break;  // 只激活第一个找到的黄鸟
                }
            }
        }
    }
}

