// Level loading and management from JSON files.
#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

#include "Config.hpp"
#include "Entity.hpp"
#include "Material.hpp"
#include "Physics.hpp"

struct BlockSpec {
    std::string material;
    sf::Vector2f position;
    sf::Vector2f size;
};

struct PigSpec {
    PigType type;
    sf::Vector2f position;
};

struct BirdSpec {
    BirdType type;
    sf::Vector2f position;
};

struct LevelData {
    int id{1};
    int targetScore{10000};
    sf::Vector2f slingshot{config::kSlingshotX, config::kSlingshotY};
    std::vector<BlockSpec> blocks;
    std::vector<PigSpec> pigs;
    std::vector<BirdSpec> birds;
};

class LevelLoader {
public:
    LevelData load(const std::string& path);
};


