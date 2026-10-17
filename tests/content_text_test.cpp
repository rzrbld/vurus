#include "assets/content_text.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>

int main() {
    using namespace vyruz;
    TextDocument doc;
    std::string error;
    assert(parseContentText("# comment\r\n1.3 # version\r\n"
        "OBJECT-CLASS [\r\nName = Lift\r\n"
        "GAF-name = D:\\Game  Files\\OBJECT\\LIFT.GAF\r\n"
        "TYPE Patrol [Points = 1, 2, -3.5]\r\n"
        "TYPE Patrol [Unknown [flag]]\r\n"
        "override-attributes\r\n]\r\n"
        "LEVEL-NAME [Entering Tower 1]\r\n"
        "Quoted = \"D:\\Game Files\\foo#bar\", \"\"", "fixture.3dc", doc, error));
    assert(error.empty() && doc.source == "fixture.3dc" && doc.nodes.size() == 4);
    assert(doc.nodes[0].kind == TextKind::Bare && doc.nodes[0].key == "1.3");
    const auto& object = doc.nodes[1];
    assert(object.kind == TextKind::Block && object.line == 3 && object.children.size() == 5);
    assert(object.children[1].values[0] == "D:\\Game  Files\\OBJECT\\LIFT.GAF");
    assert(object.children[2].key == "TYPE" && object.children[2].arguments[0] == "Patrol");
    assert(object.children[2].children[0].values.size() == 3);
    assert(object.children[3].children[0].children[0].key == "flag");
    assert(object.children[4].kind == TextKind::Bare);
    assert(doc.nodes[2].children[0].key == "Entering");
    assert(doc.nodes[2].children[0].arguments == (std::vector<std::string>{"Tower", "1"}));
    assert(doc.nodes[3].values == (std::vector<std::string>{"D:\\Game Files\\foo#bar", ""}));

    const auto failure = [&](const std::string& text) {
        assert(!parseContentText(text, "bad.3do", doc, error));
        assert(error.rfind("bad.3do:", 0) == 0);
        assert(doc.source == "fixture.3dc" && doc.nodes.size() == 4);
    };
    for (const auto* text : {"[", "]", "A [", "A =", "A = # missing\n", "A = ,1",
                            "A = 1,", "A = 1,,2", "A = 1 = 2", "A , B", "= 2",
                            "A = \"oops", "A = \"one\ntwo\"", "A B = 1", "A = B [C]"})
        failure(text);
    failure(std::string("A = x\0y", 8));
    failure(std::string(16 * 1024 * 1024 + 1, ' '));
    std::string nested;
    for (int i = 0; i < 64; ++i) nested += "A [";
    assert(parseContentText(nested + std::string(64, ']'), "depth", doc, error));
    assert(!parseContentText(nested + "A []" + std::string(64, ']'), "depth", doc, error));
    assert(error.find("nesting") != std::string::npos);
    assert(parseContentText("A [B [] C [D = 2]]\nRepeat=1\nRepeat=2", "inline", doc, error));
    assert(doc.nodes[0].children.size() == 2 && doc.nodes.size() == 3);
    assert(doc.nodes[1].values[0] == "1" && doc.nodes[2].values[0] == "2");
    assert(parseContentText("Behaviour = Straight Tracker\nUse-video-mem = 0;\n"
                            "RANDOM Shield 1 4\nADD Ammo [Name = Rocket]", "quirks", doc, error));
    assert(doc.nodes[0].values[0] == "Straight Tracker");
    assert(doc.nodes[1].values[0] == "0;");
    assert(doc.nodes[2].arguments == (std::vector<std::string>{"Shield", "1", "4"}));
    assert(doc.nodes[3].arguments[0] == "Ammo");
    assert(!parseContentText(std::string(1000001, '\n'), "tokens", doc, error));
    assert(error.find("token limit") != std::string::npos);
    assert(parseContentText(std::string(16 * 1024 * 1024, ' '), "limit", doc, error));
    assert(parseContentText("", "empty", doc, error) && doc.nodes.empty() && error.empty());
}
