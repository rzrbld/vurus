#include "assets/content_text.h"

#include <utility>

namespace vyruz {
namespace {
enum class TokenKind { Word, Open, Close, Equal, Comma, Newline, End };
struct Token {
    TokenKind kind;
    std::string value;
    size_t begin, end, line;
};

class Parser {
public:
    Parser(std::string_view input, const std::string& filename, std::string& diagnostic)
        : text(input), source(filename), error(diagnostic) {}

    bool run(TextDocument& document) {
        if (text.size() > 16 * 1024 * 1024) return fail(1, "input exceeds 16 MiB limit");
        if (!lex()) return false;
        return statements(document.nodes, 0);
    }

private:
    std::string_view text;
    const std::string& source;
    std::string& error;
    std::vector<Token> tokens;
    size_t cursor = 0;

    bool fail(size_t line, const std::string& message) {
        error = source + ":" + std::to_string(line) + ": " + message;
        return false;
    }
    bool lex() {
        size_t p = 0, line = 1;
        while (p < text.size()) {
            if (tokens.size() >= 1000000) return fail(line, "token limit exceeded");
            const char c = text[p];
            if (c == ' ' || c == '\t' || c == '\v' || c == '\f') { ++p; continue; }
            if (c == '#') {
                while (p < text.size() && text[p] != '\r' && text[p] != '\n') ++p;
                continue;
            }
            const size_t begin = p, tokenLine = line;
            TokenKind kind = TokenKind::Word;
            std::string value;
            if (c == '\r' || c == '\n') {
                ++p;
                if (c == '\r' && p < text.size() && text[p] == '\n') ++p;
                kind = TokenKind::Newline;
                ++line;
            } else if (c == '[' || c == ']' || c == '=' || c == ',') {
                ++p;
                kind = c == '[' ? TokenKind::Open : c == ']' ? TokenKind::Close :
                       c == '=' ? TokenKind::Equal : TokenKind::Comma;
            } else if (c == '"') {
                ++p;
                const size_t content = p;
                while (p < text.size() && text[p] != '"') {
                    if (text[p] == '\r' || text[p] == '\n')
                        return fail(tokenLine, "newline in quoted value");
                    if (text[p] == '\0') return fail(tokenLine, "NUL in input");
                    ++p;
                }
                if (p == text.size()) return fail(tokenLine, "unterminated quoted value");
                value = std::string(text.substr(content, p - content));
                ++p;
            } else {
                while (p < text.size()) {
                    const char a = text[p];
                    if (a == '\0') return fail(line, "NUL in input");
                    if (a == ' ' || a == '\t' || a == '\v' || a == '\f' ||
                        a == '\r' || a == '\n' || a == '#' || a == '[' ||
                        a == ']' || a == '=' || a == ',' || a == '"') break;
                    ++p;
                }
                value = std::string(text.substr(begin, p - begin));
            }
            tokens.push_back({kind, std::move(value), begin, p, tokenLine});
        }
        tokens.push_back({TokenKind::End, {}, p, p, line});
        return true;
    }
    const Token& peek() const { return tokens[cursor]; }
    bool statements(std::vector<TextNode>& nodes, size_t depth) {
        while (true) {
            while (peek().kind == TokenKind::Newline) ++cursor;
            if (peek().kind == TokenKind::End) {
                return depth == 0 || fail(peek().line, "missing closing ]");
            }
            if (peek().kind == TokenKind::Close) {
                if (depth == 0) return fail(peek().line, "unexpected ]");
                ++cursor;
                return true;
            }
            if (peek().kind != TokenKind::Word)
                return fail(peek().line, "expected statement name");
            TextNode node{TextKind::Bare, peek().value, {}, {}, {}, source, peek().line};
            ++cursor;
            while (peek().kind == TokenKind::Word) {
                node.arguments.push_back(peek().value);
                ++cursor;
            }
            if (peek().kind == TokenKind::Open) {
                if (depth >= 64) return fail(peek().line, "block nesting exceeds 64");
                node.kind = TextKind::Block;
                ++cursor;
                if (!statements(node.children, depth + 1)) return false;
            } else if (peek().kind == TokenKind::Equal) {
                if (!node.arguments.empty()) return fail(node.line, "assignment name must be one word");
                node.kind = TextKind::Assignment;
                ++cursor;
                while (true) {
                    if (peek().kind != TokenKind::Word)
                        return fail(peek().line, "expected assignment value");
                    std::string value = peek().value;
                    size_t previousEnd = peek().end;
                    ++cursor;
                    while (peek().kind == TokenKind::Word) {
                        value.append(text.substr(previousEnd, peek().begin - previousEnd));
                        value += peek().value;
                        previousEnd = peek().end;
                        ++cursor;
                    }
                    node.values.push_back(std::move(value));
                    if (peek().kind != TokenKind::Comma) break;
                    ++cursor;
                }
                if (peek().kind != TokenKind::Newline && peek().kind != TokenKind::Close &&
                    peek().kind != TokenKind::End)
                    return fail(peek().line, "unexpected token after assignment value");
            } else if (peek().kind != TokenKind::Newline && peek().kind != TokenKind::Close &&
                       peek().kind != TokenKind::End) {
                return fail(peek().line, "unexpected token after bare statement");
            }
            nodes.push_back(std::move(node));
        }
    }
};
} // namespace

bool parseContentText(std::string_view text, const std::string& source,
                      TextDocument& out, std::string& error) {
    TextDocument parsed;
    parsed.source = source;
    Parser parser(text, source, error);
    if (!parser.run(parsed)) return false;
    out = std::move(parsed);
    error.clear();
    return true;
}
} // namespace vyruz
