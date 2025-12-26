// Parse level JSON into runtime structures.
#include "Level.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
BirdType birdFromString(const std::string& s) {
    if (s == "yellow") return BirdType::Yellow;
    if (s == "bomb") return BirdType::Bomb;
    return BirdType::Red;
}

PigType pigFromString(const std::string& s) {
    if (s == "king") return PigType::Large;
    if (s == "normal") return PigType::Medium;
    return PigType::Small;
}
}  // namespace

LevelData LevelLoader::load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("无法打开关卡文件: " + path);
    }

    nlohmann::json j;
    in >> j;

    LevelData data;
    data.id = j.value("id", 1);
    data.targetScore = j.value("targetScore", 5000);
    if (j.contains("slingshot")) {
        data.slingshot = {j["slingshot"].value("x", config::kSlingshotX),
                          j["slingshot"].value("y", config::kSlingshotY)};
    } else {
        data.slingshot = {config::kSlingshotX, config::kSlingshotY};
    }

    if (j.contains("blocks")) {
        for (auto& b : j["blocks"]) {
            BlockSpec spec;
            spec.material = b.value("material", "wood");
            spec.position = {b.value("x", 0.0f), b.value("y", 0.0f)};
            spec.size = {b.value("width", 50.0f), b.value("height", 20.0f)};
            // Normalize negative sizes: move top-left accordingly so center stays correct
            if (spec.size.x < 0.0f) {
                spec.position.x += spec.size.x;
                spec.size.x = -spec.size.x;
            }
            if (spec.size.y < 0.0f) {
                spec.position.y += spec.size.y;
                spec.size.y = -spec.size.y;
            }
            data.blocks.push_back(spec);
        }
    }

    if (j.contains("pigs")) {
        for (auto& p : j["pigs"]) {
            PigSpec spec;
            spec.type = pigFromString(p.value("type", "small"));
            spec.position = {p.value("x", 0.0f), p.value("y", 0.0f)};
            data.pigs.push_back(spec);
        }
    }

    if (j.contains("birds")) {
        for (auto& b : j["birds"]) {
            BirdSpec spec;
            spec.type = birdFromString(b.value("type", "red"));
            spec.position = {b.value("x", 0.0f), b.value("y", 0.0f)};
            data.birds.push_back(spec);
        }
    }

    return data;
}


