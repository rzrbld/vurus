#pragma once
#include "assets/content_text.h"
#include "game/shared_properties.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vyruz {
struct ClassId { uint32_t value; bool operator==(const ClassId&) const = default; };
struct ObjectId { uint32_t value; bool operator==(const ObjectId&) const = default; };
struct SequenceId { uint32_t value = 0; }; // one-based ANM; zero disables the channel
struct PathId { int value; };
struct ContentPosition { float x = 0, y = 0; };
struct SpriteReference { std::string path; SequenceId sequence; };
struct ActorFlags {
    std::optional<bool> invisible, detail, inGround, overWater, zShadow, managed, inertia;
};
struct ObjectClass {
    ClassId id{};
    std::string name;
    std::optional<ClassId> parent;
    SharedProperties properties; // resolved snapshot, unknown fields retained
    SpriteReference sprite, shadow;
    ActorFlags flags;
    std::vector<std::string> behaviours;
};
struct PatrolPath {
    PathId id{};
    std::vector<ContentPosition> points;
    TextNode authored;
};
struct PatrolReference {
    PathId path{-1};
    int point = -1;
    bool resolved = false;
};
struct ObjectPlacement {
    ObjectId id{};
    ClassId classId{};
    std::optional<ObjectId> owner; // root placements have no runtime creator
    ContentPosition position;
    ActorFlags flags;
    SpriteReference sprite, shadow;
    std::vector<std::string> behaviours;
    SharedProperties properties;
    std::vector<PatrolReference> patrols;
    TextNode authored;
};
struct SceneContent {
    std::vector<TextDocument> classDocuments; // includes defaults/difficulty/unknown statements
    TextDocument objectsDocument, pathsDocument;
    std::vector<ObjectClass> classes;
    std::vector<ObjectPlacement> objects;
    std::vector<PatrolPath> paths;
    std::optional<ObjectId> player;
    std::string levelName, levelId, mapPath;
    std::vector<std::string> diagnostics; // retained shipped-data inconsistencies
};

using ContentReader = std::function<bool(const std::string&, std::vector<uint8_t>&)>;
// Both APIs publish their output only after successful validation.
bool buildClassCatalog(const std::vector<TextDocument>& documents,
                       std::vector<ObjectClass>& out, std::string& error);
bool loadSceneContent(const ContentReader& read, const std::string& mapPath,
                      SceneContent& out, std::string& error);
const TextNode* contentProperty(const std::vector<TextNode>& nodes, const std::string& key);
} // namespace vyruz
