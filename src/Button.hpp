// Button UI component for game menus.
#pragma once

#include <SFML/Graphics.hpp>
#include <functional>
#include <string>
#include <memory>

class Button {
public:
    Button(const std::string& text, const sf::Font& font, const sf::Vector2f& position, 
           const sf::Vector2f& size = {200.0f, 50.0f});
    
    void setCallback(std::function<void()> callback) { callback_ = callback; }
    void setText(const std::string& text);
    void setPosition(const sf::Vector2f& position);
    void setSize(const sf::Vector2f& size);
    
    void update(const sf::Vector2f& mousePos, bool mousePressed);
    void draw(sf::RenderWindow& window) const;
    
    bool isHovered() const { return hovered_; }
    bool isPressed() const { return pressed_; }
    
private:
    void updateVisuals();
    static sf::Texture& getWoodTexture();  // Static method to load and return wood texture
    
    sf::RectangleShape shape_;
    sf::Text text_;
    std::function<void()> callback_;
    
    bool hovered_{false};
    bool pressed_{false};
    bool wasPressed_{false};
    
    // Colors for hover/pressed effects (applied as tint)
    sf::Color normalColor_{sf::Color::White};
    sf::Color hoverColor_{sf::Color(200, 200, 255)};  // Slight blue tint when hovered
    sf::Color pressedColor_{sf::Color(150, 150, 200)};  // Darker blue tint when pressed
    sf::Color textColor_{sf::Color::Black};
};

