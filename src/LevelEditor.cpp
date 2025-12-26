// Level Editor implementation
#include "LevelEditor.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>

#include "Config.hpp"
#include "Game.hpp"
#include "Level.hpp"
#include <nlohmann/json.hpp>

// EditorEntity implementation
sf::Vector2f EditorEntity::position() const {
    if (block) return block->position();
    if (pig) return pig->position();
    if (bird && bird->body()) return bird->body()->position();
    return {};
}

sf::Vector2f EditorEntity::size() const {
    if (block && block->body() && block->body()->body_) {
        // Get block size from physics body shape
        b2Fixture* fixture = block->body()->body_->GetFixtureList();
        if (fixture) {
            b2Shape* shape = fixture->GetShape();
            if (shape && shape->GetType() == b2Shape::e_polygon) {
                b2PolygonShape* polyShape = static_cast<b2PolygonShape*>(shape);
                // Get half-extents and convert to full size
                b2Vec2 halfSize = polyShape->m_vertices[2];  // Top-right vertex (relative to body center)
                sf::Vector2f pixelHalfSize = PhysicsWorld::meterToPixel(halfSize);
                return pixelHalfSize * 2.0f;  // Return full size
            }
        }
    }
    return {};
}

void EditorEntity::setPosition(const sf::Vector2f& pos) {
    if (block && block->body()) {
        block->body()->setPosition(pos);
    } else if (pig && pig->body()) {
        pig->body()->setPosition(pos);
    } else if (bird && bird->body()) {
        bird->body()->setPosition(pos);
    }
}

void EditorEntity::setSize(const sf::Vector2f& s) {
    // Only blocks can be resized
    // Note: This method is a placeholder - actual resize is handled by LevelEditor::resizeEntity
    // which has access to PhysicsWorld
}

void EditorEntity::draw(sf::RenderWindow& window) const {
    if (block) block->draw(window);
    if (pig) pig->draw(window);
    if (bird) bird->draw(window);
    
    // Draw selection highlight
    if (selected) {
        sf::Vector2f pos = position();
        sf::Vector2f sz = size();
        if (sz.x > 0 && sz.y > 0) {
            // Block selection with resize handles
            sf::RectangleShape outline(sz);
            outline.setPosition(pos - sz * 0.5f);
            outline.setFillColor(sf::Color::Transparent);
            outline.setOutlineColor(sf::Color::Yellow);
            outline.setOutlineThickness(3.0f);
            window.draw(outline);
            
            // Draw resize handle at bottom-right corner
            float handleSize = 8.0f;
            sf::Vector2f handlePos = pos + sz * 0.5f;
            sf::CircleShape handle(handleSize);
            handle.setPosition(handlePos);
            handle.setOrigin(sf::Vector2f(handleSize, handleSize));
            handle.setFillColor(sf::Color::Cyan);
            handle.setOutlineColor(sf::Color::Blue);
            handle.setOutlineThickness(2.0f);
            window.draw(handle);
        } else {
            // Circle selection (pig/bird)
            float radius = pig ? 20.0f : 15.0f;
            sf::CircleShape outline(radius);
            outline.setPosition(pos);
            outline.setOrigin(sf::Vector2f(radius, radius));
            outline.setFillColor(sf::Color::Transparent);
            outline.setOutlineColor(sf::Color::Yellow);
            outline.setOutlineThickness(3.0f);
            window.draw(outline);
        }
    }
}

bool EditorEntity::contains(const sf::Vector2f& point) const {
    sf::Vector2f pos = position();
    sf::Vector2f sz = size();
    if (sz.x > 0 && sz.y > 0) {
        // Block
        return point.x >= pos.x - sz.x * 0.5f && point.x <= pos.x + sz.x * 0.5f &&
               point.y >= pos.y - sz.y * 0.5f && point.y <= pos.y + sz.y * 0.5f;
    } else {
        // Circle (pig/bird)
        float radius = pig ? 20.0f : 15.0f;
        float distSq = (point.x - pos.x) * (point.x - pos.x) + (point.y - pos.y) * (point.y - pos.y);
        return distSq <= radius * radius;
    }
}

bool EditorEntity::isResizeHandle(const sf::Vector2f& point, float handleSize) const {
    if (!block || !selected) return false;
    sf::Vector2f pos = position();
    sf::Vector2f sz = size();
    if (sz.x <= 0 || sz.y <= 0) return false;
    
    // Resize handle is at bottom-right corner
    sf::Vector2f corner = pos + sz * 0.5f;
    float dist = std::sqrt((point.x - corner.x) * (point.x - corner.x) + 
                          (point.y - corner.y) * (point.y - corner.y));
    return dist <= handleSize * 2.0f;  // Larger hit area for easier clicking
}

// LevelEditor implementation
LevelEditor::LevelEditor(sf::RenderWindow& window, const sf::Font& font, Game* game)
    : window_(window), font_(font), game_(game), physics_({0.0f, config::kGravity}) {
    initUI();
    createPhysicsWorld();
}

void LevelEditor::initUI() {
    toolbarButtons_.clear();
    
    // Tool selection buttons
    float btnY = 20.0f;
    float btnX = 20.0f;
    float btnSpacing = 110.0f;
    
    auto selectBtn = std::make_unique<Button>("选择", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(100.0f, 40.0f));
    selectBtn->setCallback([this]() { currentTool_ = EditorTool::Select; });
    toolbarButtons_.push_back(std::move(selectBtn));
    btnX += btnSpacing;
    
    auto blockBtn = std::make_unique<Button>("物块", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(100.0f, 40.0f));
    blockBtn->setCallback([this]() { currentTool_ = EditorTool::PlaceBlock; });
    toolbarButtons_.push_back(std::move(blockBtn));
    btnX += btnSpacing;
    
    auto pigBtn = std::make_unique<Button>("猪猪", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(100.0f, 40.0f));
    pigBtn->setCallback([this]() { currentTool_ = EditorTool::PlacePig; });
    toolbarButtons_.push_back(std::move(pigBtn));
    btnX += btnSpacing;
    
    auto birdBtn = std::make_unique<Button>("鸟类", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(100.0f, 40.0f));
    birdBtn->setCallback([this]() { currentTool_ = EditorTool::PlaceBird; });
    toolbarButtons_.push_back(std::move(birdBtn));
    btnX += btnSpacing;
    
    auto deleteBtn = std::make_unique<Button>("删除", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(100.0f, 40.0f));
    deleteBtn->setCallback([this]() { currentTool_ = EditorTool::Delete; });
    toolbarButtons_.push_back(std::move(deleteBtn));
    btnX += btnSpacing;
    
    // Material selection buttons
    btnY = 70.0f;
    btnX = 20.0f;
    auto woodBtn = std::make_unique<Button>("木板", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(80.0f, 30.0f));
    woodBtn->setCallback([this]() { currentMaterial_ = EditorMaterial::Wood; });
    toolbarButtons_.push_back(std::move(woodBtn));
    btnX += 90.0f;
    
    auto glassBtn = std::make_unique<Button>("玻璃", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(80.0f, 30.0f));
    glassBtn->setCallback([this]() { currentMaterial_ = EditorMaterial::Glass; });
    toolbarButtons_.push_back(std::move(glassBtn));
    btnX += 90.0f;
    
    auto stoneBtn = std::make_unique<Button>("石头", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(80.0f, 30.0f));
    stoneBtn->setCallback([this]() { currentMaterial_ = EditorMaterial::Stone; });
    toolbarButtons_.push_back(std::move(stoneBtn));
    btnX += 90.0f;
    
    // Pig type selection buttons
    btnY = 110.0f;
    btnX = 20.0f;
    auto pigSmallBtn = std::make_unique<Button>("小猪", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(70.0f, 30.0f));
    pigSmallBtn->setCallback([this]() { currentPigType_ = PigType::Small; });
    toolbarButtons_.push_back(std::move(pigSmallBtn));
    btnX += 80.0f;
    
    auto pigMediumBtn = std::make_unique<Button>("中猪", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(70.0f, 30.0f));
    pigMediumBtn->setCallback([this]() { currentPigType_ = PigType::Medium; });
    toolbarButtons_.push_back(std::move(pigMediumBtn));
    btnX += 80.0f;
    
    auto pigLargeBtn = std::make_unique<Button>("大猪", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(70.0f, 30.0f));
    pigLargeBtn->setCallback([this]() { currentPigType_ = PigType::Large; });
    toolbarButtons_.push_back(std::move(pigLargeBtn));
    btnX += 80.0f;
    
    // Bird type selection buttons
    auto birdRedBtn = std::make_unique<Button>("红鸟", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(70.0f, 30.0f));
    birdRedBtn->setCallback([this]() { currentBirdType_ = BirdType::Red; });
    toolbarButtons_.push_back(std::move(birdRedBtn));
    btnX += 80.0f;
    
    auto birdYellowBtn = std::make_unique<Button>("黄鸟", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(70.0f, 30.0f));
    birdYellowBtn->setCallback([this]() { currentBirdType_ = BirdType::Yellow; });
    toolbarButtons_.push_back(std::move(birdYellowBtn));
    btnX += 80.0f;
    
    auto birdBombBtn = std::make_unique<Button>("炸弹鸟", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(80.0f, 30.0f));
    birdBombBtn->setCallback([this]() { currentBirdType_ = BirdType::Bomb; });
    toolbarButtons_.push_back(std::move(birdBombBtn));
    btnX += 90.0f;
    
    // File operations
    btnY = 150.0f;
    btnX = 20.0f;
    auto saveBtn = std::make_unique<Button>("保存", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(80.0f, 30.0f));
    saveBtn->setCallback([this]() { 
        if (currentLevelPath_.empty()) {
            currentLevelPath_ = "./levels/editor_level.json";
        }
        // Ensure directory exists using filesystem library
        std::filesystem::path filePath(currentLevelPath_);
        std::filesystem::path dir = filePath.parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir)) {
            try {
                std::filesystem::create_directories(dir);
            } catch (const std::filesystem::filesystem_error& e) {
                std::cerr << "错误: 无法创建目录: " << dir.string() << "\n";
            }
        }
        if (saveToJSON(currentLevelPath_)) {
            // Success message is printed in saveToJSON
        } else {
            std::cerr << "错误: 关卡保存失败，请检查文件路径和权限\n";
        }
    });
    toolbarButtons_.push_back(std::move(saveBtn));
    btnX += 90.0f;
    
    auto loadBtn = std::make_unique<Button>("加载", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(80.0f, 30.0f));
    loadBtn->setCallback([this]() { 
        // Show file selection list
        refreshFileList();
        showFileList_ = !showFileList_;
        // Debug output in English to avoid Windows console encoding issues
        std::cerr << "[Load Button] File list " << (showFileList_ ? "shown" : "hidden") 
                  << ", found " << availableFiles_.size() << " files\n";
    });
    toolbarButtons_.push_back(std::move(loadBtn));
    btnX += 90.0f;
    
    auto undoBtn = std::make_unique<Button>("撤销", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(80.0f, 30.0f));
    undoBtn->setCallback([this]() { undo(); });
    toolbarButtons_.push_back(std::move(undoBtn));
    btnX += 90.0f;
    
    auto redoBtn = std::make_unique<Button>("重做", font_, sf::Vector2f(btnX, btnY), sf::Vector2f(80.0f, 30.0f));
    redoBtn->setCallback([this]() { redo(); });
    toolbarButtons_.push_back(std::move(redoBtn));
    btnX += 90.0f;
    
    // Back button - ESC key will be handled in handleEvent
}

void LevelEditor::update(float dt) {
    // Update physics
    updatePhysics(dt);
    
    // Update entities
    for (auto& entity : entities_) {
        if (entity.block) entity.block->update(dt);
        if (entity.pig) entity.pig->update(dt);
        if (entity.bird) entity.bird->update(dt);
    }
    
    // Update UI
    updateUI(dt);
    
    // Auto-save removed - user must manually save
}

void LevelEditor::updateUI(float dt) {
    sf::Vector2f mousePos = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
    bool mousePressed = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    
    for (auto& btn : toolbarButtons_) {
        btn->update(mousePos, mousePressed);
    }
}

void LevelEditor::render() {
    window_.clear(sf::Color(200, 230, 255));
    
    // Draw ground
    {
        const float groundLeft = -200.0f;
        const float groundRight = 1600.0f;
        const float groundWidth = groundRight - groundLeft;
        sf::RectangleShape groundShape({groundWidth, 40.0f});
        groundShape.setOrigin({groundWidth * 0.5f, 20.0f});
        groundShape.setPosition({(groundLeft + groundRight) * 0.5f, static_cast<float>(config::kWindowHeight) - 10.0f});
        groundShape.setFillColor(sf::Color(110, 180, 80));
        window_.draw(groundShape);
    }
    
    // Draw entities
    for (const auto& entity : entities_) {
        entity.draw(window_);
    }
    
    // Draw UI
    renderUI();
}

void LevelEditor::renderUI() {
    renderToolbar();
    if (showPropertyPanel_) {
        renderPropertyPanel();
    }
    if (showFileList_) {
        renderFileList();
    }
}

void LevelEditor::renderToolbar() {
    for (const auto& btn : toolbarButtons_) {
        btn->draw(window_);
    }
    
    // Draw tool indicator
    std::string toolLabelStr = "当前工具: ";
    sf::Text toolText(font_, sf::String::fromUtf8(toolLabelStr.begin(), toolLabelStr.end()), 16);
    toolText.setFillColor(sf::Color::Black);
    toolText.setPosition({20.0f, 190.0f});
    window_.draw(toolText);
    
    std::string toolName;
    switch (currentTool_) {
        case EditorTool::Select: toolName = "选择"; break;
        case EditorTool::PlaceBlock: toolName = "放置物块"; break;
        case EditorTool::PlacePig: toolName = "放置猪猪"; break;
        case EditorTool::PlaceBird: toolName = "放置鸟类"; break;
        case EditorTool::Delete: toolName = "删除"; break;
    }
    sf::Text toolNameText(font_, sf::String::fromUtf8(toolName.begin(), toolName.end()), 16);
    toolNameText.setFillColor(sf::Color::Blue);
    toolNameText.setPosition({120.0f, 190.0f});
    window_.draw(toolNameText);
    
    // Draw current material/pig/bird type indicator
    if (currentTool_ == EditorTool::PlaceBlock) {
        std::string matName;
        switch (currentMaterial_) {
            case EditorMaterial::Wood: matName = "木板"; break;
            case EditorMaterial::Glass: matName = "玻璃"; break;
            case EditorMaterial::Stone: matName = "石头"; break;
            case EditorMaterial::StoneSlab: matName = "石条"; break;
            case EditorMaterial::Woodboard: matName = "木板条"; break;
        }
        std::string matTextStr = "材质: " + matName;
        sf::Text matText(font_, sf::String::fromUtf8(matTextStr.begin(), matTextStr.end()), 14);
        matText.setFillColor(sf::Color(100, 100, 100));
        matText.setPosition({20.0f, 210.0f});
        window_.draw(matText);
    } else if (currentTool_ == EditorTool::PlacePig) {
        std::string pigTypeName;
        switch (currentPigType_) {
            case PigType::Small: pigTypeName = "小"; break;
            case PigType::Medium: pigTypeName = "中"; break;
            case PigType::Large: pigTypeName = "大"; break;
        }
        std::string pigTextStr = "类型: " + pigTypeName + "猪";
        sf::Text pigText(font_, sf::String::fromUtf8(pigTextStr.begin(), pigTextStr.end()), 14);
        pigText.setFillColor(sf::Color(100, 100, 100));
        pigText.setPosition({20.0f, 210.0f});
        window_.draw(pigText);
    } else if (currentTool_ == EditorTool::PlaceBird) {
        std::string birdTypeName;
        switch (currentBirdType_) {
            case BirdType::Red: birdTypeName = "红鸟"; break;
            case BirdType::Yellow: birdTypeName = "黄鸟"; break;
            case BirdType::Bomb: birdTypeName = "炸弹鸟"; break;
        }
        std::string birdTextStr = "类型: " + birdTypeName;
        sf::Text birdText(font_, sf::String::fromUtf8(birdTextStr.begin(), birdTextStr.end()), 14);
        birdText.setFillColor(sf::Color(100, 100, 100));
        birdText.setPosition({20.0f, 210.0f});
        window_.draw(birdText);
    }
    
    // Draw help text
    std::string helpTextStr = "提示: ESC返回主菜单 | Ctrl+Z撤销 | Ctrl+Y重做 | Delete删除选中 | 拖拽右下角控制点缩放物块";
    sf::Text helpText(font_, sf::String::fromUtf8(helpTextStr.begin(), helpTextStr.end()), 14);
    helpText.setFillColor(sf::Color(100, 100, 100));
    helpText.setPosition({20.0f, config::kWindowHeight - 30.0f});
    window_.draw(helpText);
    
    // Draw entity count
    std::string entityCountStr = "实体数量: " + std::to_string(entities_.size());
    sf::Text entityCountText(font_, sf::String::fromUtf8(entityCountStr.begin(), entityCountStr.end()), 12);
    entityCountText.setFillColor(sf::Color(100, 100, 100));
    entityCountText.setPosition({20.0f, config::kWindowHeight - 50.0f});
    window_.draw(entityCountText);
    
    // Draw slingshot position indicator
    sf::CircleShape slingshotIndicator(10.0f);
    slingshotIndicator.setPosition(slingshotPos_);
    slingshotIndicator.setOrigin(sf::Vector2f(10.0f, 10.0f));
    slingshotIndicator.setFillColor(sf::Color::Red);
    slingshotIndicator.setOutlineColor(sf::Color::Black);
    slingshotIndicator.setOutlineThickness(2.0f);
    window_.draw(slingshotIndicator);
    
    std::string slingshotTextStr = "发射点";
    sf::Text slingshotText(font_, sf::String::fromUtf8(slingshotTextStr.begin(), slingshotTextStr.end()), 12);
    slingshotText.setFillColor(sf::Color::Black);
    slingshotText.setPosition(slingshotPos_ + sf::Vector2f(15.0f, -5.0f));
    window_.draw(slingshotText);
}

void LevelEditor::renderPropertyPanel() {
    if (!selectedIndex_.has_value() || selectedIndex_.value() >= entities_.size()) return;
    
    const EditorEntity& entity = entities_[selectedIndex_.value()];
    
    // Draw property panel background
    sf::RectangleShape panel({250.0f, 350.0f});
    panel.setPosition({config::kWindowWidth - 270.0f, 100.0f});
    panel.setFillColor(sf::Color(240, 240, 240, 230));
    panel.setOutlineColor(sf::Color::Black);
    panel.setOutlineThickness(2.0f);
    window_.draw(panel);
    
    // Draw property title
    std::string titleTextStr = "属性";
    sf::Text titleText(font_, sf::String::fromUtf8(titleTextStr.begin(), titleTextStr.end()), 18);
    titleText.setFillColor(sf::Color::Black);
    titleText.setStyle(sf::Text::Bold);
    titleText.setPosition({config::kWindowWidth - 260.0f, 110.0f});
    window_.draw(titleText);
    
    float yPos = 140.0f;
    
    // Display entity type
    std::string typeStr;
    if (entity.type == EditorEntityType::Block) typeStr = "类型: 物块";
    else if (entity.type == EditorEntityType::Pig) typeStr = "类型: 猪猪";
    else if (entity.type == EditorEntityType::Bird) typeStr = "类型: 鸟类";
    
    sf::Text typeText(font_, sf::String::fromUtf8(typeStr.begin(), typeStr.end()), 14);
    typeText.setFillColor(sf::Color::Black);
    typeText.setPosition({config::kWindowWidth - 260.0f, yPos});
    window_.draw(typeText);
    yPos += 25.0f;
    
    // Display position with editable fields
    sf::Vector2f pos = entity.position();
    std::string posLabelStr = "位置: X=";
    sf::Text posLabelText(font_, sf::String::fromUtf8(posLabelStr.begin(), posLabelStr.end()), 14);
    posLabelText.setFillColor(sf::Color::Black);
    posLabelText.setPosition({config::kWindowWidth - 260.0f, yPos});
    window_.draw(posLabelText);
    
    // X input field
    float inputX = config::kWindowWidth - 180.0f;
    sf::RectangleShape xInputBox({80.0f, 20.0f});
    xInputBox.setPosition({inputX, yPos});
    xInputBox.setFillColor(activeInputField_ == InputField::PosX ? sf::Color::White : sf::Color(220, 220, 220));
    xInputBox.setOutlineColor(sf::Color::Black);
    xInputBox.setOutlineThickness(1.0f);
    window_.draw(xInputBox);
    
    std::string xValueStr = (activeInputField_ == InputField::PosX) ? 
                            inputText_ : std::to_string(static_cast<int>(pos.x));
    sf::Text xValueText(font_, sf::String::fromUtf8(xValueStr.begin(), xValueStr.end()), 12);
    xValueText.setFillColor(sf::Color::Black);
    xValueText.setPosition({inputX + 5.0f, yPos + 2.0f});
    window_.draw(xValueText);
    
    // Y label and input
    std::string yLabelStr = " Y=";
    sf::Text yLabelText(font_, sf::String::fromUtf8(yLabelStr.begin(), yLabelStr.end()), 14);
    yLabelText.setFillColor(sf::Color::Black);
    yLabelText.setPosition({inputX + 90.0f, yPos});
    window_.draw(yLabelText);
    
    sf::RectangleShape yInputBox({80.0f, 20.0f});
    yInputBox.setPosition({inputX + 120.0f, yPos});
    yInputBox.setFillColor(activeInputField_ == InputField::PosY ? sf::Color::White : sf::Color(220, 220, 220));
    yInputBox.setOutlineColor(sf::Color::Black);
    yInputBox.setOutlineThickness(1.0f);
    window_.draw(yInputBox);
    
    std::string yValueStr = (activeInputField_ == InputField::PosY) ? 
                            inputText_ : std::to_string(static_cast<int>(pos.y));
    sf::Text yValueText(font_, sf::String::fromUtf8(yValueStr.begin(), yValueStr.end()), 12);
    yValueText.setFillColor(sf::Color::Black);
    yValueText.setPosition({inputX + 125.0f, yPos + 2.0f});
    window_.draw(yValueText);
    
    yPos += 25.0f;
    
    // Display size (for blocks) with editable fields
    if (entity.type == EditorEntityType::Block) {
        sf::Vector2f sz = entity.size();
        std::string sizeLabelStr = "大小: W=";
        sf::Text sizeLabelText(font_, sf::String::fromUtf8(sizeLabelStr.begin(), sizeLabelStr.end()), 14);
        sizeLabelText.setFillColor(sf::Color::Black);
        sizeLabelText.setPosition({config::kWindowWidth - 260.0f, yPos});
        window_.draw(sizeLabelText);
        
        // Width input field
        float inputX = config::kWindowWidth - 180.0f;
        sf::RectangleShape wInputBox({80.0f, 20.0f});
        wInputBox.setPosition({inputX, yPos});
        wInputBox.setFillColor(activeInputField_ == InputField::SizeX ? sf::Color::White : sf::Color(220, 220, 220));
        wInputBox.setOutlineColor(sf::Color::Black);
        wInputBox.setOutlineThickness(1.0f);
        window_.draw(wInputBox);
        
        std::string wValueStr = (activeInputField_ == InputField::SizeX) ? 
                                inputText_ : std::to_string(static_cast<int>(sz.x));
        sf::Text wValueText(font_, sf::String::fromUtf8(wValueStr.begin(), wValueStr.end()), 12);
        wValueText.setFillColor(sf::Color::Black);
        wValueText.setPosition({inputX + 5.0f, yPos + 2.0f});
        window_.draw(wValueText);
        
        // Height label and input
        std::string hLabelStr = " H=";
        sf::Text hLabelText(font_, sf::String::fromUtf8(hLabelStr.begin(), hLabelStr.end()), 14);
        hLabelText.setFillColor(sf::Color::Black);
        hLabelText.setPosition({inputX + 90.0f, yPos});
        window_.draw(hLabelText);
        
        sf::RectangleShape hInputBox({80.0f, 20.0f});
        hInputBox.setPosition({inputX + 120.0f, yPos});
        hInputBox.setFillColor(activeInputField_ == InputField::SizeY ? sf::Color::White : sf::Color(220, 220, 220));
        hInputBox.setOutlineColor(sf::Color::Black);
        hInputBox.setOutlineThickness(1.0f);
        window_.draw(hInputBox);
        
        std::string hValueStr = (activeInputField_ == InputField::SizeY) ? 
                                inputText_ : std::to_string(static_cast<int>(sz.y));
        sf::Text hValueText(font_, sf::String::fromUtf8(hValueStr.begin(), hValueStr.end()), 12);
        hValueText.setFillColor(sf::Color::Black);
        hValueText.setPosition({inputX + 125.0f, yPos + 2.0f});
        window_.draw(hValueText);
        
        yPos += 25.0f;
        
        // Display material
        if (entity.block) {
            std::string matStr = "材质: " + entity.block->material().name;
            sf::Text matText(font_, sf::String::fromUtf8(matStr.begin(), matStr.end()), 14);
            matText.setFillColor(sf::Color::Black);
            matText.setPosition({config::kWindowWidth - 260.0f, yPos});
            window_.draw(matText);
            yPos += 25.0f;
        }
    }
    
    // Display pig/bird type with change buttons
    if (entity.type == EditorEntityType::Pig && entity.pig) {
        std::string pigTypeStr = "猪类型: ";
        switch (entity.pig->type()) {
            case PigType::Large: pigTypeStr += "大"; break;
            case PigType::Medium: pigTypeStr += "中"; break;
            case PigType::Small: pigTypeStr += "小"; break;
        }
        sf::Text pigTypeText(font_, sf::String::fromUtf8(pigTypeStr.begin(), pigTypeStr.end()), 14);
        pigTypeText.setFillColor(sf::Color::Black);
        pigTypeText.setPosition({config::kWindowWidth - 260.0f, yPos});
        window_.draw(pigTypeText);
        yPos += 25.0f;
        
        // Note: Type change requires deletion and recreation
        std::string noteTextStr = "提示: 删除后重新放置可更改类型";
        sf::Text noteText(font_, sf::String::fromUtf8(noteTextStr.begin(), noteTextStr.end()), 12);
        noteText.setFillColor(sf::Color(150, 150, 150));
        noteText.setPosition({config::kWindowWidth - 260.0f, yPos});
        window_.draw(noteText);
        yPos += 20.0f;
    }
    
    if (entity.type == EditorEntityType::Bird && entity.bird) {
        std::string birdTypeStr = "鸟类型: ";
        switch (entity.bird->type()) {
            case BirdType::Red: birdTypeStr += "红鸟"; break;
            case BirdType::Yellow: birdTypeStr += "黄鸟"; break;
            case BirdType::Bomb: birdTypeStr += "炸弹鸟"; break;
        }
        sf::Text birdTypeText(font_, sf::String::fromUtf8(birdTypeStr.begin(), birdTypeStr.end()), 14);
        birdTypeText.setFillColor(sf::Color::Black);
        birdTypeText.setPosition({config::kWindowWidth - 260.0f, yPos});
        window_.draw(birdTypeText);
        yPos += 25.0f;
        
        // Note: Type change requires deletion and recreation
        std::string noteTextStr = "提示: 删除后重新放置可更改类型";
        sf::Text noteText(font_, sf::String::fromUtf8(noteTextStr.begin(), noteTextStr.end()), 12);
        noteText.setFillColor(sf::Color(150, 150, 150));
        noteText.setPosition({config::kWindowWidth - 260.0f, yPos});
        window_.draw(noteText);
        yPos += 20.0f;
    }
}

void LevelEditor::handleEvent(const sf::Event& event) {
    if (event.is<sf::Event::Closed>()) {
        window_.close();
        return;
    }
    
    if (event.is<sf::Event::KeyPressed>()) {
        auto keyEvent = event.getIf<sf::Event::KeyPressed>();
        if (keyEvent) {
            if (keyEvent->code == sf::Keyboard::Key::Escape) {
                // Cancel input if active
                if (activeInputField_ != InputField::None) {
                    activeInputField_ = InputField::None;
                    inputText_.clear();
                }
                // Close file list if open
                else if (showFileList_) {
                    showFileList_ = false;
                }
                // Return to main menu - handled by Game class via ESC key check
            } else if (keyEvent->code == sf::Keyboard::Key::Delete && selectedIndex_.has_value()) {
                deleteEntity(selectedIndex_.value());
            } else if (keyEvent->code == sf::Keyboard::Key::Z && 
                      sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl)) {
                // Ctrl+Z for undo
                undo();
            } else if (keyEvent->code == sf::Keyboard::Key::Y && 
                      sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl)) {
                // Ctrl+Y for redo
                redo();
            } else if (keyEvent->code == sf::Keyboard::Key::Enter && activeInputField_ != InputField::None) {
                // Apply input on Enter
                applyInputFieldChange();
            } else if (keyEvent->code == sf::Keyboard::Key::Backspace && activeInputField_ != InputField::None) {
                // Handle backspace
                if (!inputText_.empty()) {
                    inputText_.pop_back();
                }
            }
        }
    }
    
    // Handle text input
    if (event.is<sf::Event::TextEntered>() && activeInputField_ != InputField::None) {
        auto textEvent = event.getIf<sf::Event::TextEntered>();
        if (textEvent) {
            char c = static_cast<char>(textEvent->unicode);
            // Only allow digits, minus sign, and decimal point
            if ((c >= '0' && c <= '9') || c == '-' || c == '.') {
                inputText_ += c;
            }
        }
    }
    
    if (event.is<sf::Event::MouseButtonPressed>()) {
        auto mouseEvent = event.getIf<sf::Event::MouseButtonPressed>();
            if (mouseEvent && mouseEvent->button == sf::Mouse::Button::Left) {
            sf::Vector2f mousePos = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
            
            // Check if clicking on file list first
            if (showFileList_) {
                if (handleFileListClick(mousePos)) {
                    return;  // File list handled the click
                }
            }
            
            // Check if clicking on toolbar buttons - let Button class handle it via updateUI
            // Button callbacks are triggered on mouse release in updateUI(), so we just mark it here
            bool clickedUI = false;
            for (const auto& btn : toolbarButtons_) {
                if (btn->isHovered()) {
                    clickedUI = true;
                    break;
                }
            }
            
            // Check if clicking on property panel input fields
            bool clickedInputField = false;
            if (showPropertyPanel_ && selectedIndex_.has_value() && selectedIndex_.value() < entities_.size()) {
                const EditorEntity& entity = entities_[selectedIndex_.value()];
                float inputX = config::kWindowWidth - 180.0f;
                float inputY = 140.0f + 25.0f;  // Position Y row
                
                // Check position X input
                if (mousePos.x >= inputX && mousePos.x <= inputX + 80.0f &&
                    mousePos.y >= inputY && mousePos.y <= inputY + 20.0f) {
                    activeInputField_ = InputField::PosX;
                    inputText_ = std::to_string(static_cast<int>(entity.position().x));
                    inputStartValue_ = entity.position();
                    clickedInputField = true;
                }
                // Check position Y input
                else if (mousePos.x >= inputX + 120.0f && mousePos.x <= inputX + 200.0f &&
                    mousePos.y >= inputY && mousePos.y <= inputY + 20.0f) {
                    activeInputField_ = InputField::PosY;
                    inputText_ = std::to_string(static_cast<int>(entity.position().y));
                    inputStartValue_ = entity.position();
                    clickedInputField = true;
                }
                // Check size inputs (for blocks only)
                else if (entity.type == EditorEntityType::Block) {
                    float sizeY = inputY + 25.0f;  // Size row
                    // Check size X input
                    if (mousePos.x >= inputX && mousePos.x <= inputX + 80.0f &&
                        mousePos.y >= sizeY && mousePos.y <= sizeY + 20.0f) {
                        activeInputField_ = InputField::SizeX;
                        inputText_ = std::to_string(static_cast<int>(entity.size().x));
                        inputStartValue_ = entity.size();
                        clickedInputField = true;
                    }
                    // Check size Y input
                    else if (mousePos.x >= inputX + 120.0f && mousePos.x <= inputX + 200.0f &&
                        mousePos.y >= sizeY && mousePos.y <= sizeY + 20.0f) {
                        activeInputField_ = InputField::SizeY;
                        inputText_ = std::to_string(static_cast<int>(entity.size().y));
                        inputStartValue_ = entity.size();
                        clickedInputField = true;
                    }
                }
            }
            
            // If clicking outside input fields, deselect input
            if (!clickedInputField && activeInputField_ != InputField::None) {
                applyInputFieldChange();
            }
            
            if (clickedInputField) {
                // Input field was clicked, don't process other clicks
                return;
            }
            
            // If clicked on a toolbar button, don't process editor tool clicks
            // (Button callback will be handled in updateUI when mouse is released)
            if (clickedUI) {
                return;
            }
            
            // Process editor tool clicks
            {
                switch (currentTool_) {
                    case EditorTool::Select: {
                        // Check if clicking on slingshot (allow moving it)
                        float slingshotDist = std::sqrt((mousePos.x - slingshotPos_.x) * (mousePos.x - slingshotPos_.x) +
                                                       (mousePos.y - slingshotPos_.y) * (mousePos.y - slingshotPos_.y));
                        if (slingshotDist <= 15.0f) {
                            // Start dragging slingshot
                            dragStartPos_ = mousePos;
                            dragStartEntityPos_ = slingshotPos_;
                            isDragging_ = true;
                            break;
                        }
                        
                        EditorEntity* entity = getEntityAt(mousePos);
                        if (entity) {
                            size_t index = entity - &entities_[0];
                            selectEntity(index);
                            
                            // Check if clicking on resize handle
                            if (entity->type == EditorEntityType::Block && entity->isResizeHandle(mousePos)) {
                                startResize(mousePos);
                            } else {
                                startDrag(mousePos);
                            }
                            showPropertyPanel_ = true;  // Show property panel when selecting
                        } else {
                            deselectAll();
                            showPropertyPanel_ = false;
                        }
                        break;
                    }
                    case EditorTool::PlaceBlock: {
                        std::string materialName;
                        switch (currentMaterial_) {
                            case EditorMaterial::Wood: materialName = "wood"; break;
                            case EditorMaterial::Glass: materialName = "glass"; break;
                            case EditorMaterial::Stone: materialName = "stone"; break;
                            case EditorMaterial::StoneSlab: materialName = "stoneslab"; break;
                            case EditorMaterial::Woodboard: materialName = "woodboard"; break;
                        }
                        Material mat = getMaterialOrDefault(materialName);
                        addBlock(currentMaterial_, mousePos, {120.0f, 30.0f});
                        break;
                    }
                    case EditorTool::PlacePig: {
                        addPig(currentPigType_, mousePos);
                        break;
                    }
                    case EditorTool::PlaceBird: {
                        addBird(currentBirdType_, mousePos);
                        break;
                    }
                    case EditorTool::Delete: {
                        EditorEntity* entity = getEntityAt(mousePos);
                        if (entity) {
                            size_t index = entity - &entities_[0];
                            deleteEntity(index);
                        }
                        break;
                    }
                }
            }
        }
    }
    
    if (event.is<sf::Event::MouseButtonReleased>()) {
        auto mouseEvent = event.getIf<sf::Event::MouseButtonReleased>();
        if (mouseEvent && mouseEvent->button == sf::Mouse::Button::Left) {
            if (isDragging_) {
                endDrag();
            }
            if (isResizing_) {
                endResize();
            }
        }
    }
    
    if (event.is<sf::Event::MouseMoved>()) {
        auto mouseEvent = event.getIf<sf::Event::MouseMoved>();
        if (mouseEvent) {
            sf::Vector2f mousePos = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
            if (isDragging_) {
                updateDrag(mousePos);
            }
            if (isResizing_) {
                updateResize(mousePos);
            }
        }
    }
}

void LevelEditor::addBlock(EditorMaterial material, const sf::Vector2f& pos, const sf::Vector2f& size) {
    std::string materialName;
    switch (material) {
        case EditorMaterial::Wood: materialName = "wood"; break;
        case EditorMaterial::Glass: materialName = "glass"; break;
        case EditorMaterial::Stone: materialName = "stone"; break;
        case EditorMaterial::StoneSlab: materialName = "stoneslab"; break;
        case EditorMaterial::Woodboard: materialName = "woodboard"; break;
    }
    
    Material mat = getMaterialOrDefault(materialName);
    EditorEntity entity;
    entity.type = EditorEntityType::Block;
    entity.block = std::make_unique<Block>(mat, pos, size, physics_);
    // Mark as editor entity to disable damage
    if (entity.block->body() && entity.block->body()->userData_) {
        entity.block->body()->userData_->isEditorEntity = true;
    }
    entities_.push_back(std::move(entity));
    
    // Push undo action
    EditorAction action;
    action.type = EditorAction::Add;
    action.entityIndex = entities_.size() - 1;
    pushAction(action);
}

void LevelEditor::addPig(PigType type, const sf::Vector2f& pos) {
    EditorEntity entity;
    entity.type = EditorEntityType::Pig;
    entity.pig = std::make_unique<Pig>(type, pos, physics_);
    // Mark as editor entity to disable damage
    if (entity.pig->body() && entity.pig->body()->userData_) {
        entity.pig->body()->userData_->isEditorEntity = true;
    }
    entities_.push_back(std::move(entity));
    
    EditorAction action;
    action.type = EditorAction::Add;
    action.entityIndex = entities_.size() - 1;
    pushAction(action);
}

void LevelEditor::addBird(BirdType type, const sf::Vector2f& pos) {
    EditorEntity entity;
    entity.type = EditorEntityType::Bird;
    entity.bird = std::make_unique<Bird>(type, pos, physics_);
    // Mark as editor entity to disable damage
    if (entity.bird->body() && entity.bird->body()->userData_) {
        entity.bird->body()->userData_->isEditorEntity = true;
    }
    entities_.push_back(std::move(entity));
    
    EditorAction action;
    action.type = EditorAction::Add;
    action.entityIndex = entities_.size() - 1;
    pushAction(action);
}

void LevelEditor::deleteEntity(size_t index) {
    if (index >= entities_.size()) return;
    
    // Store snapshot before deletion
    EditorAction action;
    action.type = EditorAction::Delete;
    action.entityIndex = index;
    
    // Store entity data for undo
    const EditorEntity& entity = entities_[index];
    action.entityType = entity.type;
    action.entityPos = entity.position();
    action.entitySize = entity.size();
    if (entity.block) {
        action.materialName = entity.block->material().name;
    } else if (entity.pig) {
        action.pigType = entity.pig->type();
    } else if (entity.bird) {
        action.birdType = entity.bird->type();
    }
    
    // Destroy physics body before erasing entity
    b2World* world = physics_.world();
    if (world) {
        if (entity.block && entity.block->body() && entity.block->body()->body_) {
            world->DestroyBody(entity.block->body()->body_);
        } else if (entity.pig && entity.pig->body() && entity.pig->body()->body_) {
            world->DestroyBody(entity.pig->body()->body_);
        } else if (entity.bird && entity.bird->body() && entity.bird->body()->body_) {
            world->DestroyBody(entity.bird->body()->body_);
        }
    }
    
    pushAction(action);
    
    entities_.erase(entities_.begin() + index);
    if (selectedIndex_.has_value()) {
        if (selectedIndex_.value() == index) {
            selectedIndex_.reset();
            showPropertyPanel_ = false;
        } else if (selectedIndex_.value() > index) {
            // Adjust index if entity before selection was deleted
            selectedIndex_ = selectedIndex_.value() - 1;
        }
    }
}

void LevelEditor::selectEntity(size_t index) {
    if (index >= entities_.size()) return;
    deselectAll();
    selectedIndex_ = index;
    entities_[index].selected = true;
}

void LevelEditor::deselectAll() {
    for (auto& entity : entities_) {
        entity.selected = false;
    }
    selectedIndex_.reset();
}

EditorEntity* LevelEditor::getEntityAt(const sf::Vector2f& pos) {
    // Check in reverse order (top entities first)
    for (int i = static_cast<int>(entities_.size()) - 1; i >= 0; --i) {
        if (entities_[i].contains(pos)) {
            return &entities_[i];
        }
    }
    return nullptr;
}

void LevelEditor::startDrag(const sf::Vector2f& pos) {
    if (!selectedIndex_.has_value()) return;
    isDragging_ = true;
    dragStartPos_ = pos;
    dragStartEntityPos_ = entities_[selectedIndex_.value()].position();
}

void LevelEditor::updateDrag(const sf::Vector2f& pos) {
    if (!isDragging_) return;
    
    // Check if dragging slingshot
    if (!selectedIndex_.has_value()) {
        sf::Vector2f delta = pos - dragStartPos_;
        slingshotPos_ = dragStartEntityPos_ + delta;
        return;
    }
    
    // Dragging entity
    sf::Vector2f delta = pos - dragStartPos_;
    entities_[selectedIndex_.value()].setPosition(dragStartEntityPos_ + delta);
}

void LevelEditor::endDrag() {
    if (!isDragging_) return;
    
    // Check if dragging slingshot
    if (!selectedIndex_.has_value()) {
        // Slingshot was dragged - no undo/redo for slingshot position
        isDragging_ = false;
        return;
    }
    
    // Entity was dragged
    isDragging_ = false;
    
    EditorAction action;
    action.type = EditorAction::Move;
    action.entityIndex = selectedIndex_.value();
    action.oldValue = dragStartEntityPos_;
    action.newValue = entities_[selectedIndex_.value()].position();
    pushAction(action);
}

void LevelEditor::startResize(const sf::Vector2f& pos) {
    if (!selectedIndex_.has_value()) return;
    EditorEntity& entity = entities_[selectedIndex_.value()];
    if (entity.type != EditorEntityType::Block) return;
    
    isResizing_ = true;
    resizeStartPos_ = pos;
    resizeStartSize_ = entity.size();
}

void LevelEditor::updateResize(const sf::Vector2f& pos) {
    if (!selectedIndex_.has_value() || !isResizing_) return;
    EditorEntity& entity = entities_[selectedIndex_.value()];
    if (entity.type != EditorEntityType::Block) return;
    
    // Calculate new size based on drag distance
    sf::Vector2f delta = pos - resizeStartPos_;
    sf::Vector2f newSize = resizeStartSize_ + delta * 2.0f;  // Scale factor
    
    // Clamp minimum and maximum size
    // Box2D requires minimum area, so ensure at least 30 pixels (1 meter)
    const float minSize = 30.0f;
    newSize.x = std::max(minSize, std::min(500.0f, newSize.x));
    newSize.y = std::max(minSize, std::min(500.0f, newSize.y));
    
    // Apply resize immediately (recreates physics body)
    resizeEntity(selectedIndex_.value(), newSize);
}

void LevelEditor::endResize() {
    if (!isResizing_ || !selectedIndex_.has_value()) return;
    
    EditorEntity& entity = entities_[selectedIndex_.value()];
    sf::Vector2f finalSize = entity.size();
    
    // Push resize action for undo/redo
    EditorAction action;
    action.type = EditorAction::Resize;
    action.entityIndex = selectedIndex_.value();
    action.oldValue = resizeStartSize_;
    action.newValue = finalSize;
    pushAction(action);
    
    isResizing_ = false;
}

void LevelEditor::resizeEntity(size_t index, const sf::Vector2f& newSize) {
    if (index >= entities_.size()) return;
    EditorEntity& entity = entities_[index];
    if (entity.type != EditorEntityType::Block || !entity.block) return;
    
    // Store current properties BEFORE destroying the body
    sf::Vector2f pos = entity.position();  // Get position from current body
    Material mat = entity.block->material();
    
    // Destroy old physics body before creating new one
    if (entity.block->body() && entity.block->body()->body_) {
        b2World* world = entity.block->body()->body_->GetWorld();
        if (world) {
            world->DestroyBody(entity.block->body()->body_);
        }
    }
    
    // Clear the block pointer before creating new one (ensures old body is fully destroyed)
    entity.block.reset();
    
    // Create new block with new size - this will create a new physics body
    entity.block = std::make_unique<Block>(mat, pos, newSize, physics_);
    // Mark as editor entity to disable damage
    if (entity.block->body() && entity.block->body()->userData_) {
        entity.block->body()->userData_->isEditorEntity = true;
    }
}

void LevelEditor::pushAction(const EditorAction& action) {
    undoStack_.push_back(action);
    if (undoStack_.size() > kMaxUndoHistory) {
        undoStack_.pop_front();
    }
    redoStack_.clear();
}

void LevelEditor::undo() {
    if (undoStack_.empty()) return;
    
    EditorAction action = undoStack_.back();
    undoStack_.pop_back();
    
    // Apply undo based on action type
    switch (action.type) {
        case EditorAction::Add:
            if (action.entityIndex < entities_.size()) {
                // Store entity data for redo
                const EditorEntity& entity = entities_[action.entityIndex];
                action.entityType = entity.type;
                action.entityPos = entity.position();
                action.entitySize = entity.size();
                if (entity.block) {
                    action.materialName = entity.block->material().name;
                } else if (entity.pig) {
                    action.pigType = entity.pig->type();
                } else if (entity.bird) {
                    action.birdType = entity.bird->type();
                }
                
                // Destroy physics body before erasing entity
                b2World* world = physics_.world();
                if (world) {
                    if (entity.block && entity.block->body() && entity.block->body()->body_) {
                        world->DestroyBody(entity.block->body()->body_);
                    } else if (entity.pig && entity.pig->body() && entity.pig->body()->body_) {
                        world->DestroyBody(entity.pig->body()->body_);
                    } else if (entity.bird && entity.bird->body() && entity.bird->body()->body_) {
                        world->DestroyBody(entity.bird->body()->body_);
                    }
                }
                
                entities_.erase(entities_.begin() + action.entityIndex);
                if (selectedIndex_.has_value() && selectedIndex_.value() == action.entityIndex) {
                    selectedIndex_.reset();
                    showPropertyPanel_ = false;
                } else if (selectedIndex_.has_value() && selectedIndex_.value() > action.entityIndex) {
                    selectedIndex_ = selectedIndex_.value() - 1;
                }
            }
            redoStack_.push_back(action);
            break;
        case EditorAction::Delete:
            // Re-add entity from stored data
            if (action.entityIndex <= entities_.size()) {
                EditorEntity restored;
                restored.type = action.entityType;
                if (action.entityType == EditorEntityType::Block) {
                    Material mat = getMaterialOrDefault(action.materialName);
                    restored.block = std::make_unique<Block>(mat, action.entityPos, action.entitySize, physics_);
                } else if (action.entityType == EditorEntityType::Pig) {
                    restored.pig = std::make_unique<Pig>(action.pigType, action.entityPos, physics_);
                } else if (action.entityType == EditorEntityType::Bird) {
                    restored.bird = std::make_unique<Bird>(action.birdType, action.entityPos, physics_);
                }
                entities_.insert(entities_.begin() + action.entityIndex, std::move(restored));
            }
            redoStack_.push_back(action);
            break;
        case EditorAction::Move:
            if (action.entityIndex < entities_.size()) {
                // Store new position for redo
                sf::Vector2f currentPos = entities_[action.entityIndex].position();
                action.newValue = currentPos;
                entities_[action.entityIndex].setPosition(action.oldValue);
            }
            redoStack_.push_back(action);
            break;
        case EditorAction::Resize:
            if (action.entityIndex < entities_.size()) {
                // Store new size for redo
                sf::Vector2f currentSize = entities_[action.entityIndex].size();
                action.newValue = currentSize;
                resizeEntity(action.entityIndex, action.oldValue);
            }
            redoStack_.push_back(action);
            break;
        default:
            redoStack_.push_back(action);
            break;
    }
}

void LevelEditor::redo() {
    if (redoStack_.empty()) return;
    
    EditorAction action = redoStack_.back();
    redoStack_.pop_back();
    
    // Apply redo based on action type
    switch (action.type) {
        case EditorAction::Add:
            // Re-add entity from stored data
            if (action.entityIndex <= entities_.size()) {
                EditorEntity restored;
                restored.type = action.entityType;
                if (action.entityType == EditorEntityType::Block) {
                    Material mat = getMaterialOrDefault(action.materialName);
                    restored.block = std::make_unique<Block>(mat, action.entityPos, action.entitySize, physics_);
                } else if (action.entityType == EditorEntityType::Pig) {
                    restored.pig = std::make_unique<Pig>(action.pigType, action.entityPos, physics_);
                } else if (action.entityType == EditorEntityType::Bird) {
                    restored.bird = std::make_unique<Bird>(action.birdType, action.entityPos, physics_);
                }
                entities_.insert(entities_.begin() + action.entityIndex, std::move(restored));
            }
            undoStack_.push_back(action);
            break;
        case EditorAction::Delete:
            if (action.entityIndex < entities_.size()) {
                // Store entity data for undo
                const EditorEntity& entity = entities_[action.entityIndex];
                action.entityType = entity.type;
                action.entityPos = entity.position();
                action.entitySize = entity.size();
                if (entity.block) {
                    action.materialName = entity.block->material().name;
                } else if (entity.pig) {
                    action.pigType = entity.pig->type();
                } else if (entity.bird) {
                    action.birdType = entity.bird->type();
                }
                
                // Destroy physics body before erasing entity
                b2World* world = physics_.world();
                if (world) {
                    if (entity.block && entity.block->body() && entity.block->body()->body_) {
                        world->DestroyBody(entity.block->body()->body_);
                    } else if (entity.pig && entity.pig->body() && entity.pig->body()->body_) {
                        world->DestroyBody(entity.pig->body()->body_);
                    } else if (entity.bird && entity.bird->body() && entity.bird->body()->body_) {
                        world->DestroyBody(entity.bird->body()->body_);
                    }
                }
                
                entities_.erase(entities_.begin() + action.entityIndex);
                if (selectedIndex_.has_value() && selectedIndex_.value() == action.entityIndex) {
                    selectedIndex_.reset();
                    showPropertyPanel_ = false;
                } else if (selectedIndex_.has_value() && selectedIndex_.value() > action.entityIndex) {
                    selectedIndex_ = selectedIndex_.value() - 1;
                }
            }
            undoStack_.push_back(action);
            break;
        case EditorAction::Move:
            if (action.entityIndex < entities_.size()) {
                // Store old position for undo
                sf::Vector2f currentPos = entities_[action.entityIndex].position();
                action.oldValue = currentPos;
                entities_[action.entityIndex].setPosition(action.newValue);
            }
            undoStack_.push_back(action);
            break;
        case EditorAction::Resize:
            if (action.entityIndex < entities_.size()) {
                // Store old size for undo
                sf::Vector2f currentSize = entities_[action.entityIndex].size();
                action.oldValue = currentSize;
                resizeEntity(action.entityIndex, action.newValue);
            }
            undoStack_.push_back(action);
            break;
        default:
            undoStack_.push_back(action);
            break;
    }
}

void LevelEditor::updatePhysics(float dt) {
    physics_.step(dt);
}

void LevelEditor::createPhysicsWorld() {
    // Physics world is already created in constructor
    // Create ground
    physics_.createBoxBody(
        {config::kWindowWidth * 0.5f, config::kWindowHeight - 20.0f},
        {config::kWindowWidth * 2.0f, 40.0f},
        0.0f, 0.7f, 0.0f,
        false, false, true, nullptr, false  // Ground is not an editor entity
    );
}

void LevelEditor::clearPhysicsWorld() {
    // Clear physics bodies (simplified)
}

void LevelEditor::applyInputFieldChange() {
    if (activeInputField_ == InputField::None || !selectedIndex_.has_value() || 
        selectedIndex_.value() >= entities_.size()) {
        activeInputField_ = InputField::None;
        inputText_.clear();
        return;
    }
    
    // If input is empty, don't apply changes (keep original value)
    if (inputText_.empty()) {
        activeInputField_ = InputField::None;
        inputText_.clear();
        return;
    }
    
    EditorEntity& entity = entities_[selectedIndex_.value()];
    
    try {
        float value = std::stof(inputText_);
        
        switch (activeInputField_) {
            case InputField::PosX: {
                sf::Vector2f pos = entity.position();
                pos.x = value;
                entity.setPosition(pos);
                // Push move action
                EditorAction action;
                action.type = EditorAction::Move;
                action.entityIndex = selectedIndex_.value();
                action.oldValue = inputStartValue_;
                action.newValue = pos;
                pushAction(action);
                break;
            }
            case InputField::PosY: {
                sf::Vector2f pos = entity.position();
                pos.y = value;
                entity.setPosition(pos);
                // Push move action
                EditorAction action;
                action.type = EditorAction::Move;
                action.entityIndex = selectedIndex_.value();
                action.oldValue = inputStartValue_;
                action.newValue = pos;
                pushAction(action);
                break;
            }
            case InputField::SizeX: {
                if (entity.type == EditorEntityType::Block) {
                    sf::Vector2f size = entity.size();
                    // Box2D requires minimum area, so ensure at least 30 pixels (1 meter)
                    const float minSize = 30.0f;
                    size.x = std::max(minSize, std::min(500.0f, value));
                    resizeEntity(selectedIndex_.value(), size);
                    // Push resize action
                    EditorAction action;
                    action.type = EditorAction::Resize;
                    action.entityIndex = selectedIndex_.value();
                    action.oldValue = inputStartValue_;
                    action.newValue = size;
                    pushAction(action);
                }
                break;
            }
            case InputField::SizeY: {
                if (entity.type == EditorEntityType::Block) {
                    sf::Vector2f size = entity.size();
                    // Box2D requires minimum area, so ensure at least 30 pixels (1 meter)
                    const float minSize = 30.0f;
                    size.y = std::max(minSize, std::min(500.0f, value));
                    resizeEntity(selectedIndex_.value(), size);
                    // Push resize action
                    EditorAction action;
                    action.type = EditorAction::Resize;
                    action.entityIndex = selectedIndex_.value();
                    action.oldValue = inputStartValue_;
                    action.newValue = size;
                    pushAction(action);
                }
                break;
            }
            default:
                break;
        }
    } catch (...) {
        // Invalid input, ignore
    }
    
    activeInputField_ = InputField::None;
    inputText_.clear();
}

bool LevelEditor::loadFromJSON(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "错误: 无法打开文件: " << path << "\n";
            return false;
        }
        
        // Parse JSON manually to get slingshot position and all data
        nlohmann::json j;
        file >> j;
        file.close();
        
        // Clear existing entities and physics world
        entities_.clear();
        selectedIndex_.reset();  // Clear selection
        showPropertyPanel_ = false;  // Hide property panel
        activeInputField_ = InputField::None;  // Clear input field
        inputText_.clear();
        undoStack_.clear();  // Clear undo/redo history
        redoStack_.clear();
        // Recreate physics world to clear all bodies
        physics_ = PhysicsWorld({0.f, config::kGravity});
        createPhysicsWorld();
        
        // Load slingshot position
        if (j.contains("slingshot")) {
            slingshotPos_.x = j["slingshot"].value("x", config::kSlingshotX);
            slingshotPos_.y = j["slingshot"].value("y", config::kSlingshotY);
        } else {
            slingshotPos_ = {config::kSlingshotX, config::kSlingshotY};
        }
        
        // Load blocks
        if (j.contains("blocks")) {
            for (auto& b : j["blocks"]) {
                std::string materialName = b.value("material", "wood");
                Material mat = getMaterialOrDefault(materialName);
                // JSON position is top-left, Block constructor expects center position
                sf::Vector2f topLeftPos = {b.value("x", 0.0f), b.value("y", 0.0f)};
                sf::Vector2f size = {b.value("width", 50.0f), b.value("height", 20.0f)};
                // Normalize negative sizes to keep geometry correct
                if (size.x < 0.0f) {
                    topLeftPos.x += size.x;
                    size.x = -size.x;
                }
                if (size.y < 0.0f) {
                    topLeftPos.y += size.y;
                    size.y = -size.y;
                }
                sf::Vector2f centerPos = topLeftPos + size * 0.5f;  // Convert to center position
                
                EditorEntity entity;
                entity.type = EditorEntityType::Block;
                entity.block = std::make_unique<Block>(mat, centerPos, size, physics_);
                if (entity.block->body() && entity.block->body()->userData_) {
                    entity.block->body()->userData_->isEditorEntity = true;
                }
                entities_.push_back(std::move(entity));
            }
        }
        
        // Load pigs
        if (j.contains("pigs")) {
            for (auto& p : j["pigs"]) {
                std::string typeStr = p.value("type", "normal");
                PigType type = PigType::Medium;
                if (typeStr == "king") type = PigType::Large;
                else if (typeStr == "small") type = PigType::Small;
                
                sf::Vector2f pos = {p.value("x", 0.0f), p.value("y", 0.0f)};
                
                EditorEntity entity;
                entity.type = EditorEntityType::Pig;
                entity.pig = std::make_unique<Pig>(type, pos, physics_);
                if (entity.pig->body() && entity.pig->body()->userData_) {
                    entity.pig->body()->userData_->isEditorEntity = true;
                }
                entities_.push_back(std::move(entity));
            }
        }
        
        // Load birds
        if (j.contains("birds")) {
            for (auto& b : j["birds"]) {
                std::string typeStr = b.value("type", "red");
                BirdType type = BirdType::Red;
                if (typeStr == "yellow") type = BirdType::Yellow;
                else if (typeStr == "bomb") type = BirdType::Bomb;
                
                sf::Vector2f pos = {b.value("x", 0.0f), b.value("y", 0.0f)};
                
                EditorEntity entity;
                entity.type = EditorEntityType::Bird;
                entity.bird = std::make_unique<Bird>(type, pos, physics_);
                if (entity.bird->body() && entity.bird->body()->userData_) {
                    entity.bird->body()->userData_->isEditorEntity = true;
                }
                entities_.push_back(std::move(entity));
            }
        }
        
        currentLevelPath_ = path;
        std::cerr << "✓ 关卡加载成功: " << path << " (包含 " << entities_.size() << " 个实体)\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "✗ 错误: 加载关卡失败: " << e.what() << "\n";
        return false;
    }
}

bool LevelEditor::saveToJSON(const std::string& path) {
    try {
        // Ensure directory exists using filesystem library
        std::filesystem::path filePath(path);
        std::filesystem::path dir = filePath.parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir)) {
            try {
                std::filesystem::create_directories(dir);
            } catch (const std::filesystem::filesystem_error& e) {
                std::cerr << "错误: 无法创建目录: " << dir.string() << " - " << e.what() << "\n";
                return false;
            }
        }
        
        // Generate unique filename if file already exists
        std::string finalPath = path;
        if (std::filesystem::exists(finalPath)) {
            std::filesystem::path originalPath(path);
            std::string stem = originalPath.stem().string();  // filename without extension
            std::string extension = originalPath.extension().string();  // .json
            std::filesystem::path parentDir = originalPath.parent_path();
            
            // Try to find an available filename with number suffix
            int counter = 1;
            do {
                std::string newFilename = stem + "_" + std::to_string(counter) + extension;
                finalPath = (parentDir / newFilename).string();
                counter++;
                // Prevent infinite loop (max 1000 attempts)
                if (counter > 1000) {
                    std::cerr << "错误: 无法找到可用的文件名（尝试次数过多）\n";
                    return false;
                }
            } while (std::filesystem::exists(finalPath));
            
            std::cerr << "提示: 文件已存在，已保存为新文件: " << finalPath << "\n";
        }
        
        std::ofstream file(finalPath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            std::cerr << "错误: 无法创建文件: " << finalPath << " (请检查路径和权限)\n";
            return false;
        }
        
        file << "{\n";
        file << "  \"id\": 1,\n";
        file << "  \"targetScore\": 5000,\n";
        file << "  \"slingshot\": {\"x\": " << static_cast<int>(slingshotPos_.x) 
             << ", \"y\": " << static_cast<int>(slingshotPos_.y) << "},\n";
        
        // Save birds
        file << "  \"birds\": [\n";
        bool first = true;
        for (const auto& entity : entities_) {
            if (entity.type == EditorEntityType::Bird && entity.bird) {
                if (!first) file << ",\n";
                first = false;
                sf::Vector2f pos = entity.position();
                std::string typeStr = "red";
                if (entity.bird->type() == BirdType::Yellow) typeStr = "yellow";
                else if (entity.bird->type() == BirdType::Bomb) typeStr = "bomb";
                file << "    {\"type\": \"" << typeStr << "\", \"x\": " << static_cast<int>(pos.x) 
                     << ", \"y\": " << static_cast<int>(pos.y) << "}";
            }
        }
        file << "\n  ],\n";
        
        // Save blocks
        file << "  \"blocks\": [\n";
        first = true;
        for (const auto& entity : entities_) {
            if (entity.type == EditorEntityType::Block && entity.block) {
                if (!first) file << ",\n";
                first = false;
                sf::Vector2f centerPos = entity.position();  // Center position
                sf::Vector2f sz = entity.size();
                // Convert center position to top-left position for JSON format
                sf::Vector2f topLeftPos = centerPos - sz * 0.5f;
                file << "    {\"material\": \"" << entity.block->material().name 
                     << "\", \"x\": " << static_cast<int>(topLeftPos.x)
                     << ", \"y\": " << static_cast<int>(topLeftPos.y)
                     << ", \"width\": " << static_cast<int>(sz.x)
                     << ", \"height\": " << static_cast<int>(sz.y) << "}";
            }
        }
        file << "\n  ],\n";
        
        // Save pigs
        file << "  \"pigs\": [\n";
        first = true;
        for (const auto& entity : entities_) {
            if (entity.type == EditorEntityType::Pig && entity.pig) {
                if (!first) file << ",\n";
                first = false;
                sf::Vector2f pos = entity.position();
                std::string typeStr = "normal";
                if (entity.pig->type() == PigType::Large) typeStr = "king";
                else if (entity.pig->type() == PigType::Small) typeStr = "small";
                file << "    {\"type\": \"" << typeStr << "\", \"x\": " << static_cast<int>(pos.x) 
                     << ", \"y\": " << static_cast<int>(pos.y) << "}";
            }
        }
        file << "\n  ]\n";
        
        file << "}\n";
        file.close();
        
        // Verify file was written successfully
        if (!std::filesystem::exists(finalPath)) {
            std::cerr << "错误: 文件保存后未找到: " << finalPath << "\n";
            return false;
        }
        
        currentLevelPath_ = finalPath;
        std::cerr << "✓ 关卡已保存到: " << finalPath << " (包含 " << entities_.size() << " 个实体)\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "✗ 错误: 保存关卡失败: " << e.what() << "\n";
        return false;
    }
}

void LevelEditor::refreshFileList() {
    availableFiles_.clear();
    std::string levelsDir = "./levels";
    
    try {
        if (!std::filesystem::exists(levelsDir)) {
            // Directory doesn't exist, create it
            std::filesystem::create_directories(levelsDir);
            return;
        }
        
        // Scan directory for JSON files
        for (const auto& entry : std::filesystem::directory_iterator(levelsDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                availableFiles_.push_back(entry.path().string());
            }
        }
        
        // Sort files by name
        std::sort(availableFiles_.begin(), availableFiles_.end());
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "错误: 无法扫描目录: " << levelsDir << " - " << e.what() << "\n";
    }
}

void LevelEditor::renderFileList() {
    const float panelX = 20.0f;
    const float panelY = 200.0f;
    const float panelWidth = 300.0f;
    const float itemHeight = 25.0f;
    const float maxHeight = 400.0f;
    const float panelHeight = std::min(static_cast<float>(availableFiles_.size() + 1) * itemHeight + 10.0f, maxHeight);
    
    // Draw background panel
    sf::RectangleShape panel({panelWidth, panelHeight});
    panel.setPosition({panelX, panelY});
    panel.setFillColor(sf::Color(240, 240, 240));
    panel.setOutlineColor(sf::Color(100, 100, 100));
    panel.setOutlineThickness(2.0f);
    window_.draw(panel);
    
    // Draw title
    std::string titleStr = "选择关卡文件:";
    sf::Text title(font_, sf::String::fromUtf8(titleStr.begin(), titleStr.end()), 14);
    title.setFillColor(sf::Color::Black);
    title.setPosition({panelX + 10.0f, panelY + 5.0f});
    window_.draw(title);
    
    // Draw file list
    float yPos = panelY + 30.0f;
    for (size_t i = 0; i < availableFiles_.size() && (yPos + itemHeight) <= (panelY + panelHeight); ++i) {
        const std::string& filePath = availableFiles_[i];
        std::filesystem::path path(filePath);
        std::string filename = path.filename().string();
        
        // Check if mouse is hovering over this item
        sf::Vector2f mousePos = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
        bool isHovered = (mousePos.x >= panelX && mousePos.x <= panelX + panelWidth &&
                         mousePos.y >= yPos && mousePos.y <= yPos + itemHeight);
        
        // Draw item background
        sf::RectangleShape itemBg({panelWidth - 4.0f, itemHeight - 2.0f});
        itemBg.setPosition({panelX + 2.0f, yPos + 1.0f});
        itemBg.setFillColor(isHovered ? sf::Color(200, 220, 255) : sf::Color(255, 255, 255));
        window_.draw(itemBg);
        
        // Draw filename
        sf::Text fileText(font_, sf::String::fromUtf8(filename.begin(), filename.end()), 12);
        fileText.setFillColor(sf::Color::Black);
        fileText.setPosition({panelX + 10.0f, yPos + 5.0f});
        window_.draw(fileText);
        
        yPos += itemHeight;
    }
    
    // Draw close button area
    if (availableFiles_.empty()) {
        std::string emptyStr = "（无可用文件）";
        sf::Text emptyText(font_, sf::String::fromUtf8(emptyStr.begin(), emptyStr.end()), 12);
        emptyText.setFillColor(sf::Color(150, 150, 150));
        emptyText.setPosition({panelX + 10.0f, yPos});
        window_.draw(emptyText);
    }
}

bool LevelEditor::handleFileListClick(const sf::Vector2f& mousePos) {
    const float panelX = 20.0f;
    const float panelY = 200.0f;
    const float panelWidth = 300.0f;
    const float itemHeight = 25.0f;
    const float maxHeight = 400.0f;
    const float panelHeight = std::min(static_cast<float>(availableFiles_.size() + 1) * itemHeight + 10.0f, maxHeight);
    
    // Check if clicking outside the panel
    if (mousePos.x < panelX || mousePos.x > panelX + panelWidth ||
        mousePos.y < panelY || mousePos.y > panelY + panelHeight) {
        // Click outside - close file list
        showFileList_ = false;
        return true;
    }
    
    // Check if clicking on a file item
    float yPos = panelY + 30.0f;
    for (size_t i = 0; i < availableFiles_.size() && (yPos + itemHeight) <= (panelY + panelHeight); ++i) {
        if (mousePos.y >= yPos && mousePos.y <= yPos + itemHeight) {
            // Clicked on file item - load it
            const std::string& filePath = availableFiles_[i];
            if (loadFromJSON(filePath)) {
                currentLevelPath_ = filePath;
                std::cerr << "✓ 关卡已加载: " << filePath << "\n";
            } else {
                std::cerr << "错误: 关卡加载失败: " << filePath << "\n";
            }
            showFileList_ = false;
            return true;
        }
        yPos += itemHeight;
    }
    
    return false;  // Click was in panel but not on an item
}

