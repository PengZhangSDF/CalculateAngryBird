// Level Editor for creating and editing game levels
#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <optional>

#include "Config.hpp"
#include "Entity.hpp"
#include "Material.hpp"
#include "Physics.hpp"
#include "Button.hpp"

// Forward declarations
class Game;

// Editor entity types
enum class EditorEntityType {
    Block,
    Pig,
    Bird
};

// Editor entity wrapper
struct EditorEntity {
    EditorEntityType type;
    std::unique_ptr<Block> block;
    std::unique_ptr<Pig> pig;
    std::unique_ptr<Bird> bird;
    bool selected{false};
    bool isResizing{false};
    sf::Vector2f resizeStartSize;
    sf::Vector2f resizeStartPos;
    
    sf::Vector2f position() const;
    sf::Vector2f size() const;  // For blocks only
    void setPosition(const sf::Vector2f& pos);
    void setSize(const sf::Vector2f& s);  // For blocks only
    void draw(sf::RenderWindow& window) const;
    bool contains(const sf::Vector2f& point) const;
    bool isResizeHandle(const sf::Vector2f& point, float handleSize = 8.0f) const;
};

// Editor tool types
enum class EditorTool {
    Select,
    PlaceBlock,
    PlacePig,
    PlaceBird,
    Delete
};

// Material selection
enum class EditorMaterial {
    Wood,
    Glass,
    Stone,
    StoneSlab,
    Woodboard
};

// Undo/Redo action
struct EditorAction {
    enum Type { Add, Delete, Move, Resize, Modify };
    Type type;
    size_t entityIndex;
    // Store entity data instead of full entity (since EditorEntity is not copyable)
    EditorEntityType entityType;
    sf::Vector2f entityPos;
    sf::Vector2f entitySize;
    std::string materialName;  // For blocks
    PigType pigType;  // For pigs
    BirdType birdType;  // For birds
    sf::Vector2f oldValue;
    sf::Vector2f newValue;
};

class LevelEditor {
public:
    LevelEditor(sf::RenderWindow& window, const sf::Font& font, Game* game);
    
    void update(float dt);
    void render();
    void handleEvent(const sf::Event& event);
    
    // Level management
    bool loadFromJSON(const std::string& path);
    bool saveToJSON(const std::string& path);
    
private:
    // UI
    void initUI();
    void updateUI(float dt);
    void renderUI();
    void renderToolbar();
    void renderPropertyPanel();
    
    // Entity management
    void addBlock(EditorMaterial material, const sf::Vector2f& pos, const sf::Vector2f& size);
    void addPig(PigType type, const sf::Vector2f& pos);
    void addBird(BirdType type, const sf::Vector2f& pos);
    void deleteEntity(size_t index);
    void selectEntity(size_t index);
    void deselectAll();
    EditorEntity* getEntityAt(const sf::Vector2f& pos);
    
    // Editing operations
    void startDrag(const sf::Vector2f& pos);
    void updateDrag(const sf::Vector2f& pos);
    void endDrag();
    void startResize(const sf::Vector2f& pos);
    void updateResize(const sf::Vector2f& pos);
    void endResize();
    void resizeEntity(size_t index, const sf::Vector2f& newSize);
    
    // Undo/Redo
    void pushAction(const EditorAction& action);
    void undo();
    void redo();
    
    // Physics
    void updatePhysics(float dt);
    void createPhysicsWorld();
    void clearPhysicsWorld();
    
    // Input field handling
    void applyInputFieldChange();
    
    // Member variables
    sf::RenderWindow& window_;
    const sf::Font& font_;
    Game* game_;
    
    PhysicsWorld physics_;
    std::vector<EditorEntity> entities_;
    std::vector<std::unique_ptr<Button>> toolbarButtons_;
    std::vector<std::unique_ptr<Button>> propertyButtons_;
    
    EditorTool currentTool_{EditorTool::Select};
    EditorMaterial currentMaterial_{EditorMaterial::Wood};
    PigType currentPigType_{PigType::Medium};
    BirdType currentBirdType_{BirdType::Red};
    
    // Selection and editing
    std::optional<size_t> selectedIndex_;
    bool isDragging_{false};
    sf::Vector2f dragStartPos_;
    sf::Vector2f dragStartEntityPos_;
    bool isResizing_{false};
    sf::Vector2f resizeStartPos_;
    sf::Vector2f resizeStartSize_;
    
    // Undo/Redo stacks
    std::deque<EditorAction> undoStack_;
    std::deque<EditorAction> redoStack_;
    static constexpr size_t kMaxUndoHistory = 50;
    
    // UI state
    bool showPropertyPanel_{false};
    bool showFileList_{false};
    sf::Vector2f cameraOffset_{0.0f, 0.0f};
    float zoom_{1.0f};
    
    // Property input fields
    enum class InputField { None, PosX, PosY, SizeX, SizeY };
    InputField activeInputField_{InputField::None};
    std::string inputText_;
    sf::Vector2f inputStartValue_;
    
    // File selection
    std::vector<std::string> availableFiles_;
    void refreshFileList();
    void renderFileList();
    bool handleFileListClick(const sf::Vector2f& mousePos);
    
    // Auto-save
    std::string currentLevelPath_;
    
    // Slingshot position
    sf::Vector2f slingshotPos_{config::kSlingshotX, config::kSlingshotY};
};

