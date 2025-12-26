// Game implementation handling scenes and gameplay.
#include "Game.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <variant>

#include "Config.hpp"
#include "Material.hpp"
#include "LevelEditor.hpp"

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
    
    initAudio();
    initButtons();
    loadLevel(levelIndex_);
}

Game::~Game() = default;  // Destructor defined here so LevelEditor is complete type

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
    
    while (auto event = window_.pollEvent()) {
        if (event->is<sf::Event::Closed>()) window_.close();
        
        // Handle ESC key for pause (only when just pressed, not held)
        if (scene_ == Scene::Playing && escPressed_ && !prevEscPressed_) {
            scene_ = Scene::Paused;
        }
        
        // Handle ESC key to return from LevelEditor to main menu
        if (scene_ == Scene::LevelEditor && escPressed_ && !prevEscPressed_) {
            scene_ = Scene::MainMenu;
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
            if (splashTimer_ <= 0) scene_ = Scene::MainMenu;
            break;
        case Scene::MainMenu:
        case Scene::LevelSelect:
            updateButtons(dt);
            break;
        case Scene::Playing: {
            gameTime_ += dt;
            
            // Update launch state machine
            updateLaunchState(dt);
            
            // Handle bird launching input
            // KEY FIX: Only process input if we're still playing (not won/lost)
            if (!birds_.empty() && scene_ == Scene::Playing) {
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
                                dragStart_ = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
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
                                dragCurrent_ = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
                                
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
                                dragStart_ = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
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
                        pull = clampVec(pull, config::kMaxPullDistance);
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
                        sf::Vector2f pos = birdBody->position();
                        sf::Vector2f vel = v0;
                        const int steps = 60;  // More steps for accuracy
                        const float stepDt = 0.05f;
                        for (int i = 0; i < steps; ++i) {
                            vel.y += config::kGravity * stepDt;
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
                scene_ = Scene::Score;
                // Break immediately to prevent further updates
                break;
            } else if (lost) {
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
    window_.clear(sf::Color(180, 220, 255));
    
    // Draw background image for Splash and MainMenu scenes
    if (scene_ == Scene::Splash || scene_ == Scene::MainMenu) {
        if (backgroundSprite_.has_value()) {
            window_.draw(*backgroundSprite_);
        }
    }
    
    switch (scene_) {
        case Scene::Splash: {
            // Splash screen - no text displayed
            break;
        }
        case Scene::MainMenu:
            renderMenu();
            // Draw menu buttons
            for (const auto& btn : menuButtons_) {
                btn->draw(window_);
            }
            break;
        case Scene::LevelSelect:
            renderLevelSelect();
            // Draw level select buttons
            for (const auto& btn : levelSelectButtons_) {
                btn->draw(window_);
            }
            break;
        case Scene::Playing: {
            // Draw visible ground - extend to cover full game world
            {
                const float groundLeft = -200.0f;
                const float groundRight = 1600.0f;
                const float groundWidth = groundRight - groundLeft;
                sf::RectangleShape groundShape({groundWidth, 40.0f});
                groundShape.setOrigin({groundWidth * 0.5f, 20.0f});
                groundShape.setPosition(
                    {(groundLeft + groundRight) * 0.5f,
                     static_cast<float>(config::kWindowHeight) - 10.0f});
                groundShape.setFillColor(sf::Color(110, 180, 80));
                window_.draw(groundShape);
            }
            
            // Draw game buttons (restart, next level)
            for (const auto& btn : gameButtons_) {
                btn->draw(window_);
            }

            for (auto& b : blocks_) b->draw(window_);
            for (auto& p : pigs_) p->draw(window_);
            for (auto& b : birds_) b->draw(window_);

            // Trajectory preview
            if (!previewPath_.empty()) {
                window_.draw(previewPath_.data(),
                             previewPath_.size(),
                             sf::PrimitiveType::LineStrip);
            }

            if (launchState_ == LaunchState::Dragging && !birds_.empty()) {
                sf::Vertex line[] = {sf::Vertex(dragStart_, sf::Color::Black),
                                     sf::Vertex(dragCurrent_, sf::Color::Black)};
                dragCurrent_ = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
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
            // Draw visible ground - extend to cover full game world
            {
                const float groundLeft = -200.0f;
                const float groundRight = 1600.0f;
                const float groundWidth = groundRight - groundLeft;
                sf::RectangleShape groundShape({groundWidth, 40.0f});
                groundShape.setOrigin({groundWidth * 0.5f, 20.0f});
                groundShape.setPosition(
                    {(groundLeft + groundRight) * 0.5f,
                     static_cast<float>(config::kWindowHeight) - 10.0f});
                groundShape.setFillColor(sf::Color(110, 180, 80));
                window_.draw(groundShape);
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
    
    sf::Vector2f pull = dragStart_ - dragCurrent_;
    pull = clampVec(pull, config::kMaxPullDistance);
    sf::Vector2f impulse = pull * config::kSlingshotStiffness;
    birdToLaunch->launch(impulse);
    
    // Play bird flying sound when bird is launched
    playBirdFlyingSound(birdToLaunch->type());
    
    // Don't move next bird immediately - wait for launched bird to move away
    // This prevents next bird from blocking current bird's launch
    nextBirdMovedToSlingshot_ = false;
}

void Game::loadLevel(int index) {
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
    startBtn->setCallback([this]() { loadLevel(levelIndex_); scene_ = Scene::Playing; });
    menuButtons_.push_back(std::move(startBtn));
    
    auto levelBtn = std::make_unique<Button>("选关", font_, sf::Vector2f(400.0f, 320.0f), sf::Vector2f(200.0f, 50.0f));
    levelBtn->setCallback([this]() { scene_ = Scene::LevelSelect; });
    menuButtons_.push_back(std::move(levelBtn));
    
    auto editorBtn = std::make_unique<Button>("关卡编辑器", font_, sf::Vector2f(400.0f, 390.0f), sf::Vector2f(200.0f, 50.0f));
    editorBtn->setCallback([this]() { 
        if (!levelEditor_) {
            levelEditor_ = std::make_unique<LevelEditor>(window_, font_, this);
        }
        scene_ = Scene::LevelEditor; 
    });
    menuButtons_.push_back(std::move(editorBtn));
    
    auto quitBtn = std::make_unique<Button>("退出", font_, sf::Vector2f(400.0f, 460.0f), sf::Vector2f(200.0f, 50.0f));
    quitBtn->setCallback([this]() { window_.close(); });
    menuButtons_.push_back(std::move(quitBtn));
    
    // Game buttons (during gameplay) - positioned at top-right corner
    gameButtons_.clear();
    
    float gameBtnX = config::kWindowWidth - 120.0f;
    float gameBtnY = 20.0f;
    
    auto restartBtn = std::make_unique<Button>("重新开始", font_, sf::Vector2f(gameBtnX, gameBtnY), sf::Vector2f(100.0f, 40.0f));
    restartBtn->setCallback([this]() { resetCurrent(); scene_ = Scene::Playing; });
    gameButtons_.push_back(std::move(restartBtn));
    
    auto nextLevelBtn = std::make_unique<Button>("下一关", font_, sf::Vector2f(gameBtnX, gameBtnY + 50.0f), sf::Vector2f(100.0f, 40.0f));
    nextLevelBtn->setCallback([this]() { 
        levelIndex_ = std::min(levelIndex_ + 1, 8);
        loadLevel(levelIndex_);
        scene_ = Scene::Playing;
    });
    gameButtons_.push_back(std::move(nextLevelBtn));
    
    // Pause menu buttons
    pauseButtons_.clear();
    
    auto resumeBtn = std::make_unique<Button>("继续", font_, sf::Vector2f(400.0f, 250.0f), sf::Vector2f(200.0f, 50.0f));
    resumeBtn->setCallback([this]() { scene_ = Scene::Playing; });
    pauseButtons_.push_back(std::move(resumeBtn));
    
    auto pauseRestartBtn = std::make_unique<Button>("重新开始", font_, sf::Vector2f(400.0f, 320.0f), sf::Vector2f(200.0f, 50.0f));
    pauseRestartBtn->setCallback([this]() { resetCurrent(); scene_ = Scene::Playing; });
    pauseButtons_.push_back(std::move(pauseRestartBtn));
    
    auto pauseLevelBtn = std::make_unique<Button>("选关", font_, sf::Vector2f(400.0f, 390.0f), sf::Vector2f(200.0f, 50.0f));
    pauseLevelBtn->setCallback([this]() { scene_ = Scene::LevelSelect; });
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
        });
        levelSelectButtons_.push_back(std::move(levelBtn));
    }
    
    // Back button for level select
    float centerX = config::kWindowWidth * 0.5f;
    auto backBtn = std::make_unique<Button>("返回", font_, sf::Vector2f(centerX - 100.0f, 450.0f), sf::Vector2f(200.0f, 50.0f));
    backBtn->setCallback([this]() { scene_ = Scene::MainMenu; });
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
    });
    scoreButtons_.push_back(std::move(scoreNextBtn));
    
    auto scoreRetryBtn = std::make_unique<Button>("重新开始", font_, 
        sf::Vector2f(centerX - 100.0f, scoreBtnY + buttonSpacing), sf::Vector2f(200.0f, 50.0f));
    scoreRetryBtn->setCallback([this]() {
        resetCurrent();
        scene_ = Scene::Playing;
    });
    scoreButtons_.push_back(std::move(scoreRetryBtn));
    
    auto scoreLevelSelectBtn = std::make_unique<Button>("选关", font_, 
        sf::Vector2f(centerX - 100.0f, scoreBtnY + buttonSpacing * 2), sf::Vector2f(200.0f, 50.0f));
    scoreLevelSelectBtn->setCallback([this]() { scene_ = Scene::LevelSelect; });
    scoreButtons_.push_back(std::move(scoreLevelSelectBtn));
}

void Game::updateButtons(float dt) {
    sf::Vector2f mousePos = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
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

