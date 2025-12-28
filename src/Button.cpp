// Button implementation.
#include "Button.hpp"

#include <algorithm>
#include <iostream>

// Static texture for wood background (shared by all buttons)
sf::Texture& Button::getWoodTexture() {
    static sf::Texture woodTexture;
    static bool textureLoaded = false;
    
    if (!textureLoaded) {
        if (!woodTexture.loadFromFile("image/wood.png")) {
            std::cerr << "警告: 无法加载按钮背景贴图 image/wood.png\n";
            // If loading fails, texture will be invalid but button will still work with default color
        } else {
            // Enable texture repeating for seamless tiling
            woodTexture.setRepeated(true);
        }
        textureLoaded = true;
    }
    
    return woodTexture;
}

Button::Button(const std::string& text, const sf::Font& font, const sf::Vector2f& position, 
               const sf::Vector2f& size)
    : text_(font, sf::String::fromUtf8(text.begin(), text.end()), 24) {
    shape_.setSize(size);
    shape_.setPosition(position);
    shape_.setOutlineColor(sf::Color::Black);
    shape_.setOutlineThickness(2.0f);
    
    // Set wood texture
    sf::Texture& woodTex = getWoodTexture();
    shape_.setTexture(&woodTex);
    // Set texture rect to cover button size (texture will repeat if enabled)
    sf::Vector2u texSize = woodTex.getSize();
    if (texSize.x > 0 && texSize.y > 0) {
        // Use texture rect to make texture fill the button (with repetition)
        sf::IntRect texRect(sf::Vector2i(0, 0), sf::Vector2i(static_cast<int>(size.x), static_cast<int>(size.y)));
        shape_.setTextureRect(texRect);
    }
    
    text_.setFillColor(textColor_);
    text_.setStyle(sf::Text::Bold);
    
    updateVisuals();
}

void Button::setText(const std::string& text) {
    text_.setString(sf::String::fromUtf8(text.begin(), text.end()));
    updateVisuals();
}

void Button::setPosition(const sf::Vector2f& position) {
    shape_.setPosition(position);
    updateVisuals();
}

void Button::setSize(const sf::Vector2f& size) {
    shape_.setSize(size);
    // Update texture rect to match new size
    sf::Texture& woodTex = getWoodTexture();
    sf::Vector2u texSize = woodTex.getSize();
    if (texSize.x > 0 && texSize.y > 0) {
        sf::IntRect texRect(sf::Vector2i(0, 0), sf::Vector2i(static_cast<int>(size.x), static_cast<int>(size.y)));
        shape_.setTextureRect(texRect);
    }
    updateVisuals();
}

void Button::update(const sf::Vector2f& mousePos, bool mousePressed) {
    sf::FloatRect bounds = shape_.getGlobalBounds();
    hovered_ = bounds.contains(mousePos);
    
    wasPressed_ = pressed_;
    pressed_ = hovered_ && mousePressed;
    
    // Trigger callback on release (click)
    if (wasPressed_ && !pressed_ && hovered_ && callback_) {
        callback_();
    }
    
    updateVisuals();
}

void Button::draw(sf::RenderWindow& window) const {
    window.draw(shape_);
    
    // Center text in button
    sf::FloatRect textBounds = text_.getLocalBounds();
    sf::Vector2f textPos = shape_.getPosition();
    sf::Vector2f shapeSize = shape_.getSize();
    sf::Vector2f textSize = textBounds.size;
    sf::Vector2f textPosOffset = textBounds.position;
    textPos.x += (shapeSize.x - textSize.x) * 0.5f;
    textPos.y += (shapeSize.y - textSize.y) * 0.5f - textPosOffset.y;
    
    // Create a non-const copy for positioning (since draw is const)
    sf::Text textCopy = text_;
    textCopy.setPosition(textPos);
    window.draw(textCopy);
}

void Button::updateVisuals() {
    // Apply color tint to the texture for hover/pressed effects
    if (pressed_) {
        shape_.setFillColor(pressedColor_);
    } else if (hovered_) {
        shape_.setFillColor(hoverColor_);
    } else {
        shape_.setFillColor(normalColor_);
    }
}

