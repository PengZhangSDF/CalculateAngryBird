// Implementation of score tracking UI.
#include "ScoreSystem.hpp"

#include <string>

ScoreSystem::ScoreSystem(const sf::Font& font) : font_(font) {}

void ScoreSystem::addPoints(int pts) {
    score_ += pts;
    pulse_ = 0.25f;
    if (score_ > highScore_) highScore_ = score_;
}

void ScoreSystem::addBonusForRemainingBirds(int count) {
    addPoints(count * 1000);
}

void ScoreSystem::update(float dt) {
    pulse_ = std::max(0.0f, pulse_ - dt);
}

void ScoreSystem::draw(sf::RenderWindow& window, float x, float y) {
    sf::Text text(font_, "Score: " + std::to_string(score_) + "  High: " + std::to_string(highScore_),
                  20 + static_cast<int>(pulse_ * 20));
    text.setFillColor(sf::Color::Yellow);
    text.setPosition({x, y});
    window.draw(text);
}


