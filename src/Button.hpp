// Button UI component for game menus.
#pragma once

#include <SFML/Graphics.hpp>
#include <functional>
#include <string>

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
    
    sf::RectangleShape shape_;
    sf::Text text_;
    std::function<void()> callback_;
    
    bool hovered_{false};
    bool pressed_{false};
    bool wasPressed_{false};
    
    // Colors
    sf::Color normalColor_{sf::Color(200, 200, 200)};
    sf::Color hoverColor_{sf::Color(150, 150, 255)};
    sf::Color pressedColor_{sf::Color(100, 100, 200)};
    sf::Color textColor_{sf::Color::Black};
};

