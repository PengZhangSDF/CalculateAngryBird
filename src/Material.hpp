// Definitions for material properties used by blocks and objects.
#pragma once

#include <SFML/Graphics/Color.hpp>
#include <string>
#include <unordered_map>

struct Material {
    std::string name;
    float density;
    float friction;
    float restitution;
    float strength;   // Break threshold
    float opacity;    // 0-1
    sf::Color color;
};

inline const std::unordered_map<std::string, Material>& materialLibrary() {
    static const std::unordered_map<std::string, Material> kMaterials = {
        {"glass", {"glass", 0.5f, 0.2f, 0.4f, 120.0f, 0.7f, sf::Color(160, 200, 255)}},
        {"wood", {"wood", 0.8f, 0.5f, 0.2f, 240.0f, 1.0f, sf::Color(160, 120, 70)}},
        {"woodboard", {"woodboard", 1.0f, 0.5f, 0.25f, 320.0f, 1.0f, sf::Color(140, 100, 60)}},
        {"stone", {"stone", 2.5f, 0.7f, 0.05f, 800.0f, 1.0f, sf::Color(130, 130, 130)}},
        {"stoneslab", {"stoneslab", 1.8f, 0.6f, 0.1f, 560.0f, 1.0f, sf::Color(150, 150, 160)}}
    };
    return kMaterials;
}

inline Material getMaterialOrDefault(const std::string& name) {
    auto it = materialLibrary().find(name);
    if (it != materialLibrary().end()) {
        return it->second;
    }
    return {"wood", 1.0f, 0.5f, 0.2f, 30.0f, 1.0f, sf::Color(160, 120, 70)};
}


