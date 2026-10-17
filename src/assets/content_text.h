#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace vyruz {

enum class TextKind { Assignment, Block, Bare };
struct TextNode {
    TextKind kind=TextKind::Bare;
    std::string key;
    std::vector<std::string> arguments;
    std::vector<std::string> values;
    std::vector<TextNode> children;
    std::string source;
    size_t line = 1;
};
struct TextDocument {
    std::string source;
    std::vector<TextNode> nodes;
};

// Syntax-only reader for the text content formats. Keys retain case, and unknown
// and repeated statements retain order. A bare statement stores its first word
// in key and the remaining words in arguments (including inline block text).
// Assignments contain comma-separated values; unquoted internal whitespace and
// Windows backslashes are preserved. Double quotes delimit literal text, without
// escape processing. # starts a comment outside quotes. Newlines end statements;
// ] also ends an assignment/bare statement. Blocks can nest inline.
// Limits: 16 MiB input, 64 nested blocks, 1,000,000 lexical tokens. Failure leaves
// out untouched and reports source:line; success clears error.
bool parseContentText(std::string_view text, const std::string& source,
                      TextDocument& out, std::string& error);

} // namespace vyruz
