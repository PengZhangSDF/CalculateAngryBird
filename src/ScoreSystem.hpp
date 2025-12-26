// Score tracking and display logic.
#pragma once

#include <SFML/Graphics.hpp>

class ScoreSystem {
public:
    explicit ScoreSystem(const sf::Font& font);

    void addPoints(int pts);
    void addBonusForRemainingBirds(int count);
    void setHighScore(int score) { highScore_ = std::max(highScore_, score); }
    void resetRound() { score_ = 0; }
    int score() const { return score_; }
    int highScore() const { return highScore_; }

    void update(float dt);
    void draw(sf::RenderWindow& window, float x, float y);

private:
    const sf::Font& font_;
    int score_{0};
    int highScore_{0};
    float pulse_{0.0f};
};


