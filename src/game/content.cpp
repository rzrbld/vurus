#include "game/content.h"
#include "game/content_names.h"
#include "game/enemy_behavior_native.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <sstream>
#include <unordered_map>

namespace vyruz {
namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool is(const TextNode& n, const char* key) { return lower(n.key) == key; }
std::string join(const std::vector<std::string>& words) {
    std::string s;
    for (const auto& word : words) { if (!s.empty()) s += ' '; s += word; }
    return s;
}
[[noreturn]] void fail(const TextNode& n, const std::string& reason) {
    throw std::runtime_error(n.source + ":" + std::to_string(n.line) + ": " + reason);
}
std::string value(const TextNode& n) {
    if (n.kind == TextKind::Assignment) return join(n.values);
    if (n.kind == TextKind::Block && n.children.size() == 1 &&
        n.children[0].kind == TextKind::Bare) {
        auto s = n.children[0].key;
        if (!n.children[0].arguments.empty()) s += " " + join(n.children[0].arguments);
        return s;
    }
    fail(n, "expected scalar value for " + n.key);
}
std::string numericToken(std::string s) {
    if (!s.empty() && s.back() == ';') s.pop_back();
    return s;
}
int integer(const TextNode& n) {
    const auto s = numericToken(value(n)); int v = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) fail(n, "invalid integer: " + s);
    return v;
}
float number(const TextNode& n, std::string s) {
    s = numericToken(s); float v = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || !std::isfinite(v))
        fail(n, "invalid finite number: " + s);
    return v;
}
ContentPosition position(const TextNode& n) {
    // The syntax tree retains commas as separate values or tokenizer separators.
    std::vector<std::string> vals;
    for (const auto& s : n.values) if (s != ",") vals.push_back(s);
    if (vals.size() != 2) fail(n, "expected two coordinates");
    return {number(n, vals[0]), number(n, vals[1])};
}
const TextNode& require(const std::vector<TextNode>& nodes, const char* key, const TextNode& at) {
    const auto* p = contentProperty(nodes, key);
    if (!p) fail(at, std::string("missing ") + key);
    return *p;
}
std::string directory(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    auto slash = s.rfind('/');
    return slash == std::string::npos ? "" : s.substr(0, slash + 1);
}
std::string normalizePath(const std::string& path) {
    std::string s = path; std::replace(s.begin(), s.end(), '\\', '/');
    if (s.empty() || s.front() == '/' || s.find(':') != std::string::npos)
        throw std::runtime_error("expected relative content path: " + path);
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= s.size()) {
        auto end = s.find('/', start); if (end == std::string::npos) end = s.size();
        auto part = s.substr(start, end - start);
        if (part == "..") {
            if (parts.empty()) throw std::runtime_error("content path escapes data root: " + path);
            parts.pop_back();
        } else if (!part.empty() && part != ".") parts.push_back(part);
        if (end == s.size()) break;
        start = end + 1;
    }
    s.clear(); for (const auto& p : parts) { if (!s.empty()) s += '/'; s += p; }
    return s;
}
bool numericBlock(const TextNode& n) {
    const auto k = lower(n.key);
    return n.kind == TextKind::Block && (k == "energy" || k == "shield" || k == "weight" || k == "thrust");
}
void overlay(std::vector<TextNode>& properties, const TextNode& node) {
    const auto key = lower(node.key);
    if ((key == "type" || key == "add") &&
        (node.kind != TextKind::Block || node.arguments.size() != 1))
        fail(node,"TYPE/ADD requires one qualifier and a block");
    // HAVE suppression is evaluated by the death-drop consumer at the
    // retained Override-Objects marker. Preserve the existing property stream
    // here: it forms the content fingerprint in already-issued checkpoints.
    const char* remove = key == "override-objects" ? "object" :
                         key == "override-random-objects" ? "random" : nullptr;
    if (key == "override-attributes" || remove) {
        std::erase_if(properties, [&](const auto& old) {
            return remove ? is(old, remove) : (is(old, "type") || is(old, "create") || is(old,"add"));
        });
        properties.push_back(node);
        return;
    }
    if (node.kind == TextKind::Assignment || numericBlock(node)) {
        for (auto& old : properties) {
            if (lower(old.key) != key) continue;
            if (numericBlock(node) && numericBlock(old)) {
                for (const auto& child : node.children) overlay(old.children, child);
            } else old = node;
            return;
        }
    }
    properties.push_back(node);
}
SpriteReference sprite(const std::vector<TextNode>& properties, const char* name, const char* index) {
    SpriteReference ref;
    if (auto* n = contentProperty(properties, name)) {
        const auto path = value(*n);
        if (lower(path) != "none" && !path.empty())
            ref.path = normalizePath(directory(n->source) + "DATA/" + path);
    }
    if (auto* n = contentProperty(properties, index)) {
        const int seq = integer(*n);
        if (seq < 0) fail(*n, "ANM sequence index cannot be negative");
        ref.sequence.value = static_cast<uint32_t>(seq);
    }
    return ref;
}
ActorFlags flags(const std::vector<TextNode>& properties) {
    ActorFlags f;
    const auto set = [&](const char* key, std::optional<bool>& dst) {
        if (auto* n = contentProperty(properties, key)) {
            auto s = lower(numericToken(value(*n)));
            if (s == "1" || s == "true" || s == "yes") dst = true;
            else if (s == "0" || s == "false" || s == "no") dst = false;
            else fail(*n, "invalid boolean for " + n->key);
        }
    };
    set("invisible",f.invisible); set("is-detail",f.detail); set("in-ground",f.inGround);
    set("over-water",f.overWater); set("z-shadow",f.zShadow); set("managed",f.managed);
    set("use-inertia",f.inertia);
    return f;
}
std::vector<std::string> behaviours(const std::vector<TextNode>& properties) {
    if (const auto* b = contentProperty(properties,"behaviour")) {
        EnemyBehaviorParse parsed;std::string error;
        if(!parseEnemyBehavior(value(*b),parsed,error))fail(*b,error);
        return std::move(parsed.tokens);
    }
    return {};
}
void version(const TextDocument& doc) {
    if (doc.nodes.empty() || doc.nodes.front().kind != TextKind::Bare || doc.nodes.front().key != "1.3")
        throw std::runtime_error(doc.source + ": expected content version 1.3");
}
TextDocument document(const ContentReader& read, const std::string& path) {
    std::vector<uint8_t> bytes;
    if (!read(path, bytes)) throw std::runtime_error(path + ": content file unavailable");
    TextDocument d; std::string error;
    if (!parseContentText(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),path,d,error))
        throw std::runtime_error(error);
    version(d); return d;
}
} // namespace

const TextNode* contentProperty(const std::vector<TextNode>& nodes, const std::string& key) {
    for (auto i = nodes.rbegin(); i != nodes.rend(); ++i) if (contentNameEqual(i->key,key)) return &*i;
    return nullptr;
}

bool buildClassCatalog(const std::vector<TextDocument>& documents,
                       std::vector<ObjectClass>& out, std::string& error) {
    try {
        std::vector<ObjectClass> classes;
        std::unordered_map<std::string, ClassId> ids;
        for (const auto& doc : documents) {
            version(doc);
            std::vector<TextNode> defaults;
            for (const auto& node : doc.nodes) {
                if (is(node, "default-property")) {
                    if (node.kind != TextKind::Block) fail(node, "expected default block");
                    for (const auto& field : node.children) overlay(defaults, field);
                } else if (is(node, "object-class")) {
                    if (node.kind != TextKind::Block) fail(node, "expected class block");
                    ObjectClass c; c.id = {static_cast<uint32_t>(classes.size())}; c.properties = defaults;
                    for (const auto& field : node.children) {
                        if (is(field, "derived-from")) {
                            auto it = ids.find(lower(value(field)));
                            if (it == ids.end()) fail(field, "base class must already exist: " + value(field));
                            c.parent = it->second; c.properties = classes[it->second.value].properties;
                            c.name.clear(); // native inheritance replaces preceding fields, including Name
                        } else {
                            if ((is(field,"override-attributes") || is(field,"override-objects") ||
                                 is(field,"override-random-objects")) && !c.parent)
                                fail(field,"override directive requires derivation");
                            if (is(field, "name")) c.name = value(field);
                            overlay(c.properties, field);
                        }
                    }
                    if (c.name.empty()) fail(node, "class requires Name after derivation");
                    if (!ids.emplace(lower(c.name), c.id).second) fail(node, "duplicate class: " + c.name);
                    c.sprite = sprite(c.properties,"gaf-name","gaf-index");
                    c.shadow = sprite(c.properties,"shadow-gaf","shadow-index");
                    c.flags = flags(c.properties);
                    c.behaviours = behaviours(c.properties);
                    classes.push_back(std::move(c));
                }
            }
        }
        out = std::move(classes); error.clear(); return true;
    } catch (const std::runtime_error& e) { error = e.what(); return false; }
}

bool loadSceneContent(const ContentReader& read, const std::string& mapPath,
                      SceneContent& out, std::string& error) {
    try {
        SceneContent scene;
        scene.mapPath = normalizePath(mapPath);
        const auto dir = directory(scene.mapPath);
        const auto dot = scene.mapPath.rfind('.');
        if (dot == std::string::npos || lower(scene.mapPath.substr(dot)) != ".3dg")
            throw std::runtime_error("expected .3DG level path");
        const auto prefix = scene.mapPath.substr(0,dot);
        for (const char* name : {"DIFFICULTY","THING","FX","AMMO","WEAPON","RESOURCE",
                                 "ITEM","OBJECT","SPECIFIC","DECOR","COMMON-DECOR"}) {
            std::string path = dir + name + ".3DC";
            std::vector<uint8_t> bytes;
            if (!read(path,bytes)) path = std::string("COMMON/") + name + ".3DC";
            scene.classDocuments.push_back(document(read,path));
        }
        if (!buildClassCatalog(scene.classDocuments,scene.classes,error)) return false;
        std::unordered_map<std::string,ClassId> classes;
        for (const auto& c : scene.classes) classes.emplace(lower(c.name), c.id);
        scene.pathsDocument = document(read,prefix + ".3DP");
        std::unordered_map<int,size_t> paths;
        for (const auto& n : scene.pathsDocument.nodes) if (is(n,"create-path")) {
            PatrolPath p; p.authored = n;
            p.id.value = integer(require(n.children,"id",n));
            if (p.id.value < 0 || !paths.emplace(p.id.value,scene.paths.size()).second)
                fail(n,"invalid or duplicate path ID");
            const int count = integer(require(n.children,"count",n));
            for (const auto& point : n.children) if (is(point,"point")) p.points.push_back(position(point));
            if (count < 0 || static_cast<size_t>(count) != p.points.size()) fail(n,"path Count mismatch");
            scene.paths.push_back(std::move(p));
        }
        scene.objectsDocument = document(read,prefix + ".3DO");
        const auto& top = scene.objectsDocument.nodes;
        const auto& at = top.front();
        scene.levelName = value(require(top,"level-name",at));
        if (const auto* id = contentProperty(top,"level-id")) scene.levelId = value(*id);
        else scene.diagnostics.push_back(scene.objectsDocument.source + ": LEVEL-ID absent; retained as unspecified");
        const auto declaredMap = normalizePath(dir + value(require(top,"3dg-filename",at)));
        if (lower(declaredMap) != lower(scene.mapPath))
            scene.diagnostics.push_back(scene.objectsDocument.source + ": authored map is " + declaredMap +
                                        "; explicitly selected " + scene.mapPath);
        const int expected = integer(require(top,"object-count",at));
        for (const auto& n : top) if (is(n,"create-object")) {
            const auto& classNode = require(n.children,"class-name",n);
            auto it = classes.find(lower(value(classNode)));
            if (it == classes.end()) fail(classNode,"unknown class: " + value(classNode));
            const auto& c = scene.classes[it->second.value];
            ObjectPlacement object;
            object.id = {static_cast<uint32_t>(scene.objects.size())}; object.classId = c.id;
            object.authored = n;
            object.position = position(require(n.children,"position",n));
            object.properties = c.properties;
            std::vector<size_t> claimedAttributes;
            for (const auto& field : n.children) {
                // Native .3DO dispatch has no class-inheritance directives.
                // Reject these before they can erase claimed attribute slots.
                if (is(field,"override-attributes") || is(field,"override-objects") ||
                    is(field,"override-random-objects") || is(field,"derived-from"))
                    fail(field,"class inheritance directive is not valid in a placement");
                bool merged = false;
                if (field.kind == TextKind::Block && (is(field,"type") || is(field,"add"))) {
                    for (size_t i = 0; i < object.properties.size(); ++i) {
                        auto& old = object.properties[i];
                        if (lower(old.key) != lower(field.key) || lower(join(old.arguments)) != lower(join(field.arguments)) ||
                            std::find(claimedAttributes.begin(),claimedAttributes.end(),i) != claimedAttributes.end()) continue;
                        for (const auto& child : field.children) overlay(old.children,child);
                        claimedAttributes.push_back(i); merged = true; break;
                    }
                }
                if (!merged) {
                    overlay(object.properties,field);
                    if (field.kind == TextKind::Block && (is(field,"type") || is(field,"add")))
                        claimedAttributes.push_back(object.properties.size()-1);
                }
            }
            object.flags = flags(object.properties);
            object.behaviours = behaviours(object.properties);
            object.sprite = sprite(object.properties,"gaf-name","gaf-index");
            object.shadow = sprite(object.properties,"shadow-gaf","shadow-index");
            for (const auto& attr : object.properties) {
                if (!is(attr,"type") || attr.arguments.empty() || lower(attr.arguments[0]) != "patrol") continue;
                PatrolReference ref;
                const auto* pathNode = contentProperty(attr.children,"path-id");
                if (pathNode) ref.path.value = integer(*pathNode);
                if (auto* id = contentProperty(attr.children,"point-id")) ref.point = integer(*id);
                if (ref.path.value < -1 || ref.point < -1) fail(attr,"invalid negative patrol reference");
                if (ref.path.value != -1) {
                    auto p = paths.find(ref.path.value);
                    ref.resolved = p != paths.end();
                    // Inherited Path-ID=0 is a common unbound default. Keep it
                    // unresolved; diagnose explicit placement references, including 0.
                    if (!ref.resolved && (ref.path.value != 0 || (pathNode && pathNode->source == scene.objectsDocument.source)))
                        scene.diagnostics.push_back(pathNode->source + ":" + std::to_string(pathNode->line) +
                            ": object " + std::to_string(object.id.value) + " references missing patrol path " +
                            std::to_string(ref.path.value));
                    if (ref.resolved && ref.point >= 0 && static_cast<size_t>(ref.point) >= scene.paths[p->second].points.size())
                        fail(attr,"patrol point index outside path");
                }
                object.patrols.push_back(ref);
            }
            if (std::any_of(object.behaviours.begin(), object.behaviours.end(), [](const auto& b) {return lower(b)=="player";})) {
                if (scene.player) fail(n,"multiple player placements");
                scene.player = object.id;
            }
            scene.objects.push_back(std::move(object));
        }
        if (expected < 0 || static_cast<size_t>(expected) != scene.objects.size()) fail(at,"OBJECT-COUNT mismatch");
        out = std::move(scene); error.clear(); return true;
    } catch (const std::runtime_error& e) { error = e.what(); return false; }
}
} // namespace vyruz
