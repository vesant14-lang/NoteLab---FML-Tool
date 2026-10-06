#include "NoteCode.hpp"

#include <regex>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace fml::notelab {

std::vector<CodeColorSpan> colorBlockSource(Engine engine, const std::string& source, bool config) {
    const bool lua = engine == Engine::Psych;
    static const std::set<std::string> luaWords{"and", "break", "do", "else", "elseif", "end", "for", "function", "goto",
        "if", "in", "local", "not", "or", "repeat", "return", "then", "until", "while"};
    static const std::set<std::string> haxeWords{"abstract", "as", "break", "case", "cast", "catch", "class", "continue",
        "default", "do", "dynamic", "else", "enum", "extends", "extern", "final", "for", "function", "if", "implements",
        "import", "in", "inline", "interface", "macro", "new", "override", "package", "private", "public", "return",
        "static", "super", "switch", "this", "throw", "try", "typedef", "untyped", "using", "var", "while"};
    static const std::set<std::string> types{"Any", "Array", "Bool", "Dynamic", "Float", "Int", "Map", "Null", "String", "Void"};
    std::vector<CodeColorSpan> spans;
    auto longBracket = [&](size_t at, size_t& content, std::string& close) {
        if (at >= source.size() || source[at] != '[') return false;
        size_t end = at + 1;
        while (end < source.size() && source[end] == '=') ++end;
        if (end >= source.size() || source[end] != '[') return false;
        close = "]" + std::string(end - at - 1, '=') + "]";
        content = end + 1;
        return true;
    };
    auto takeThrough = [&](size_t at, const std::string& close) {
        const size_t end = source.find(close, at);
        return end == std::string::npos ? source.size() : end + close.size();
    };
    for (size_t i = 0; i < source.size();) {
        const size_t begin = i;
        const unsigned char c = static_cast<unsigned char>(source[i]);
        CodeColorKind kind = CodeColorKind::Text;
        size_t content = 0;
        std::string close;
        if (!config && ((lua && source.compare(i, 2, "--") == 0) || (!lua && source.compare(i, 2, "//") == 0))) {
            kind = CodeColorKind::Comment;
            if (lua && longBracket(i + 2, content, close)) i = takeThrough(content, close);
            else { i = source.find('\n', i); if (i == std::string::npos) i = source.size(); }
        } else if (!config && !lua && source.compare(i, 2, "/*") == 0) {
            kind = CodeColorKind::Comment;
            i = takeThrough(i + 2, "*/");
        } else if (!config && lua && longBracket(i, content, close)) {
            kind = CodeColorKind::String;
            i = takeThrough(content, close);
        } else if (c == '\'' || c == '"') {
            kind = CodeColorKind::String;
            const char quote = source[i++];
            while (i < source.size()) {
                if (source[i] == '\\') { i = std::min(source.size(), i + 2); continue; }
                if (source[i] == quote) { ++i; break; }
                if (source[i] == '\n' || source[i] == '\r') break;
                ++i;
            }
        } else if (std::isdigit(c) || (c == '.' && i + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[i + 1])))) {
            kind = CodeColorKind::Number;
            if (source.compare(i, 2, "0x") == 0 || source.compare(i, 2, "0X") == 0) {
                i += 2;
                while (i < source.size() && std::isxdigit(static_cast<unsigned char>(source[i]))) ++i;
            } else {
                while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) ++i;
                if (i < source.size() && source[i] == '.' && source.compare(i, 2, "..") != 0) {
                    ++i;
                    while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) ++i;
                }
                if (i < source.size() && (source[i] == 'e' || source[i] == 'E')) {
                    size_t end = i + 1;
                    if (end < source.size() && (source[end] == '+' || source[end] == '-')) ++end;
                    const size_t digits = end;
                    while (end < source.size() && std::isdigit(static_cast<unsigned char>(source[end]))) ++end;
                    if (end > digits) i = end;
                }
            }
        } else if (std::isalpha(c) || c == '_' || c >= 0x80) {
            ++i;
            while (i < source.size()) {
                const unsigned char next = static_cast<unsigned char>(source[i]);
                if (!std::isalnum(next) && next != '_' && next < 0x80) break;
                ++i;
            }
            if (!config) {
                const std::string word = source.substr(begin, i - begin);
                if (word == "true" || word == "false" || (lua ? word == "nil" : word == "null")) kind = CodeColorKind::Literal;
                else if ((lua ? luaWords : haxeWords).count(word)) kind = CodeColorKind::Keyword;
                else if (!lua && types.count(word)) kind = CodeColorKind::Type;
                else {
                    size_t after = i;
                    while (after < source.size() && std::isspace(static_cast<unsigned char>(source[after]))) ++after;
                    if (after < source.size() && source[after] == '(') kind = CodeColorKind::Function;
                }
            }
        } else ++i;
        if (!spans.empty() && spans.back().kind == kind && spans.back().end == begin) spans.back().end = i;
        else spans.push_back({begin, i, kind});
    }
    return spans;
}

namespace {

struct Token { std::string key, value; char kind = 'p'; int line = 1; };
struct Lexed { std::vector<Token> tokens; std::vector<std::string> comments; std::string error; int line = 1; };

std::string trimmed(std::string s) {
    const size_t a = s.find_first_not_of(" \r\n\t");
    return a == std::string::npos ? std::string() : s.substr(a, s.find_last_not_of(" \r\n\t") - a + 1);
}

Lexed lex(const std::string& source, bool lua, bool config) {
    Lexed out;
    if (source.size() > 262144) { out.error = "Code exceeds the 256 KiB editor limit."; return out; }
    std::vector<std::pair<char, int>> brackets;
    std::vector<std::pair<std::string, int>> luaBlocks;
    for (size_t i = 0; i < source.size();) {
        char c = source[i];
        if (std::isspace(static_cast<unsigned char>(c))) { if (c == '\n') ++out.line; ++i; continue; }
        const bool lineComment = (c == '/' && i + 1 < source.size() && source[i + 1] == '/') ||
            (lua && c == '-' && i + 1 < source.size() && source[i + 1] == '-');
        if (lineComment && !(lua && source.compare(i, 4, "--[[") == 0)) {
            size_t end = source.find('\n', i);
            out.comments.push_back(trimmed(source.substr(i + 2, end == std::string::npos ? std::string::npos : end - i - 2)));
            i = end == std::string::npos ? source.size() : end;
            continue;
        }
        if (source.compare(i, 2, "/*") == 0 || (lua && source.compare(i, 4, "--[[") == 0)) {
            const bool longLua = source.compare(i, 4, "--[[") == 0;
            const size_t end = source.find(longLua ? "]]" : "*/", i + (longLua ? 4 : 2));
            if (end == std::string::npos) { out.error = "Unclosed block comment."; return out; }
            const size_t to = end + 2;
            out.comments.push_back(trimmed(source.substr(i + (longLua ? 4 : 2), end - i - (longLua ? 4 : 2))));
            out.line += static_cast<int>(std::count(source.begin() + i, source.begin() + to, '\n'));
            i = to; continue;
        }
        Token t; t.line = out.line;
        if (c == '\'' || c == '"') {
            const char quote = c;
            ++i; bool closed = false;
            for (; i < source.size(); ++i) {
                c = source[i];
                if (c == quote) { ++i; closed = true; break; }
                if (c == '\n' || c == '\r') { out.error = "Unclosed string."; return out; }
                if (c == '\\' && i + 1 < source.size()) {
                    c = source[++i];
                    t.value += c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c;
                } else t.value += c;
            }
            if (!closed) { out.error = "Unclosed string."; return out; }
            t.kind = 's'; t.key = "s:" + t.value;
        } else if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[i + 1])))) {
            const size_t start = i;
            if (source.compare(i, 2, "0x") == 0 || source.compare(i, 2, "0X") == 0) {
                i += 2;
                while (i < source.size() && std::isxdigit(static_cast<unsigned char>(source[i]))) ++i;
            } else {
                char* end = nullptr;
                std::strtod(source.c_str() + i, &end);
                i = static_cast<size_t>(end - source.c_str());
            }
            if (i == start) { out.error = "Invalid number."; return out; }
            t.kind = 'n'; t.value = source.substr(start, i - start);
            char* end = nullptr; const double number = std::strtod(t.value.c_str(), &end);
            if (!std::isfinite(number) || end != t.value.c_str() + t.value.size()) { out.error = "Invalid number."; return out; }
            std::ostringstream value; value.precision(15); value << number;
            t.key = "n:" + value.str();
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const size_t start = i++;
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) ++i;
            t.kind = 'i'; t.value = source.substr(start, i - start); t.key = t.value;
            if (lua && !config) {
                if (t.value == "function" || t.value == "if" || t.value == "do" || t.value == "repeat") luaBlocks.push_back({t.value, t.line});
                else if (t.value == "end" || t.value == "until") {
                    if (luaBlocks.empty() || ((t.value == "until") != (luaBlocks.back().first == "repeat"))) {
                        out.error = "Unexpected Lua block ending."; return out;
                    }
                    luaBlocks.pop_back();
                }
            }
        } else {
            t.value.assign(1, c); t.key = t.value; ++i;
            if (!config && (c == '(' || c == '{' || c == '[')) brackets.push_back({c, t.line});
            if (!config && (c == ')' || c == '}' || c == ']')) {
                const char expected = c == ')' ? '(' : c == '}' ? '{' : '[';
                if (brackets.empty() || brackets.back().first != expected) { out.error = "Unmatched closing bracket."; return out; }
                brackets.pop_back();
            }
            if (i < source.size()) {
                const std::string two = t.value + source[i];
                if (two == "==" || two == "!=" || two == "~=" || two == "+=" || two == "-=" || two == "<=" || two == ">=" || two == "&&" || two == "||" || two == ".." || (!lua && (two == "++" || two == "--" || two == "->"))) {
                    t.value = two; t.key = two; ++i;
                }
            }
            if (!config && (c == ';' || c == ')') && !out.tokens.empty()) {
                const std::string& previous = out.tokens.back().key;
                if (previous == "=" || previous == "+=" || previous == "-=" || previous == "+" || previous == "*" || previous == "&&") {
                    out.error = "Missing value after an operator."; return out;
                }
            }
        }
        out.tokens.push_back(std::move(t));
        if (out.tokens.size() > 16000) { out.error = "Code is too large to synchronize safely."; return out; }
    }
    if (!brackets.empty()) { out.line = brackets.back().second; out.error = "Unclosed bracket."; }
    else if (!luaBlocks.empty()) { out.line = luaBlocks.back().second; out.error = "Unclosed Lua block."; }
    return out;
}

int distance(const std::vector<Token>& a, const std::vector<Token>& b) {
    size_t i = 0, j = 0; int cost = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].key == b[j].key) { ++i; ++j; continue; }
        size_t skipA = 33, skipB = 33;
        for (size_t k = 1; k <= 32 && i + k < a.size(); ++k) if (a[i + k].key == b[j].key) { skipA = k; break; }
        for (size_t k = 1; k <= 32 && j + k < b.size(); ++k) if (a[i].key == b[j + k].key) { skipB = k; break; }
        if (skipA < skipB) { i += skipA; cost += static_cast<int>(skipA); }
        else if (skipB < 33) { j += skipB; cost += static_cast<int>(skipB); }
        else { ++i; ++j; ++cost; }
    }
    return cost + static_cast<int>(a.size() - i + b.size() - j);
}

const BlockFile* fileAt(const BlockCode& code, const std::string& path) {
    for (const auto& f : code.files) if (f.path == path) return &f;
    return nullptr;
}

void applyComments(BlockProgram& program, const Lexed& baseline, const Lexed& edited, const Lexed& regenerated) {
    bool hasType = false;
    std::set<int> ids;
    for (const std::string& c : baseline.comments) {
        if (c.rfind("@user:", 0) == 0) hasType = true;
        if (c.rfind("@user#", 0) == 0) ids.insert(std::atoi(c.c_str() + 6));
    }
    if (hasType) program.comment.clear();
    for (int id : ids) if (auto* n = program.node(id)) n->comment.clear();
    std::set<std::string> seen;
    auto append = [&](std::string& to, const std::string& value) { if (!to.empty()) to += '\n'; to += value; };
    for (const std::string& c : edited.comments) {
        if (c.rfind("@user:", 0) == 0) { append(program.comment, trimmed(c.substr(6))); continue; }
        if (c.rfind("@user#", 0) == 0) {
            const size_t colon = c.find(':', 6);
            const int id = std::atoi(c.c_str() + 6);
            if (colon != std::string::npos && program.node(id)) append(program.node(id)->comment, trimmed(c.substr(colon + 1)));
            continue;
        }
        if (std::find(baseline.comments.begin(), baseline.comments.end(), c) != baseline.comments.end() ||
            std::find(regenerated.comments.begin(), regenerated.comments.end(), c) != regenerated.comments.end() || !seen.insert(c).second) continue;
        append(program.comment, c);
    }
}

}

std::string draftKey(Engine engine, const std::string& path) { return std::string(engineKey(engine)) + ":" + path; }

CodeImportResult applyBlockSource(Engine engine, const std::string& type, const std::string& fileName,
    const BlockProgram& original, const std::string& path, const std::string& text, const TypeLook& look) {
    CodeImportResult result;
    result.program = original;
    const bool config = path.size() >= 4 && path.substr(path.size() - 4) == ".txt";
    const Lexed target = lex(text, engine == Engine::Psych, config);
    result.line = target.line; result.error = target.error; result.syntaxValid = target.error.empty();
    if (!result.syntaxValid) return result;
    BlockProgram current = original;
    current.drafts.erase(draftKey(engine, path));
    for (auto& item : current.nodes)
        if (item.second.key == "code.file" && item.second.args.size() == 3 && item.second.args[0].value == engineKey(engine) && item.second.args[1].value == path) {
            item.second.args[2].value = text;
            result.program = std::move(current); result.applied = true; return result;
        }
    const BlockCode before = generateBlocks(engine, type, fileName, current, look);
    const BlockFile* old = fileAt(before, path);
    if (!old) { result.error = "The generated file no longer exists; the draft is retained."; return result; }
    const auto draft = original.drafts.find(draftKey(engine, path));
    if (draft != original.drafts.end() && draft->second.baseline != old->text) {
        result.error = "Blocks changed after this draft was started. Compare or discard the draft first."; return result;
    }
    const Lexed baseline = lex(old->text, engine == Engine::Psych, config);
    const std::vector<Token>& wanted = target.tokens;
    std::set<std::string> nums, strings;
    for (size_t i = 0; i < wanted.size(); ++i) {
        const Token& t = wanted[i];
        if (t.kind == 's') strings.insert(t.value);
        if (t.kind == 'n') {
            const std::string value = t.key.substr(2);
            nums.insert(value);
            if (i && wanted[i - 1].key == "-") nums.insert("-" + value);
        }
    }
    int score = distance(baseline.tokens, wanted);
    if (score > 0 && (original.nodes.size() > 128 || nums.size() > 96 || strings.size() > 96)) {
        result.error = "This edit is too large for automatic synchronization; keep it as an explicit custom file."; return result;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    int attempts = 0;
    bool timedOut = false;
    for (int pass = 0; pass < 16 && score > 0 && attempts < 8000 && !timedOut; ++pass) {
        BlockProgram best = current; int bestScore = score;
        auto consider = [&](BlockProgram candidate) {
            if (++attempts > 8000) return;
            if (std::chrono::steady_clock::now() >= deadline) { timedOut = true; return; }
            const BlockCode code = generateBlocks(engine, type, fileName, candidate, look);
            const BlockFile* f = fileAt(code, path);
            if (!f) return;
            const Lexed parsed = lex(f->text, engine == Engine::Psych, config);
            const int value = parsed.error.empty() ? distance(parsed.tokens, wanted) : score;
            if (value < bestScore) { bestScore = value; best = std::move(candidate); }
        };
        for (const auto& item : current.nodes) {
            if (bestScore == 0 || attempts > 8000 || timedOut) break;
            const int id = item.first; const BlockNode& node = item.second;
            const BlockDef* def = blockDef(node.key);
            if (!def || node.key == "code.file") continue;
            for (size_t arg = 0; arg < node.args.size(); ++arg) {
                if (node.args[arg].block >= 0 || arg >= def->args.size()) continue;
                const ArgDef& slot = def->args[arg];
                std::set<std::string> values;
                if (slot.kind == ArgKind::Number) values = nums;
                else if (slot.kind == ArgKind::Text || slot.kind == ArgKind::Sound) values = strings;
                else if (slot.kind == ArgKind::Choice) for (const auto& choice : slot.choices) values.insert(choice.value);
                else if (slot.kind == ArgKind::Variable) for (const auto& variable : blockVariables(current)) values.insert(std::to_string(variable.id));
                else if (slot.kind == ArgKind::Color) for (const Token& t : wanted) {
                    if (t.value.size() == 10 && t.value.rfind("0xFF", 0) == 0) values.insert(t.value.substr(4));
                    if (t.kind == 's' && t.value.size() == 6) values.insert(t.value);
                }
                for (const std::string& value : values) {
                    if (timedOut || attempts > 8000) break;
                    if (value == node.args[arg].value) continue;
                    if (slot.kind == ArgKind::Number) {
                        char* end = nullptr; const double n = std::strtod(value.c_str(), &end);
                        if (!std::isfinite(n) || !end || *end || n < slot.min || n > slot.max) continue;
                    }
                    BlockProgram candidate = current;
                    candidate.nodes[id].args[arg].value = value;
                    consider(std::move(candidate));
                }
            }
            for (const BlockDef& other : blockDefs()) {
                if (timedOut || attempts > 8000) break;
                if (other.key == node.key || other.shape != def->shape || other.category != def->category ||
                    other.args.size() != node.args.size() || other.places != def->places) continue;
                bool compatible = true;
                for (size_t a = 0; a < other.args.size(); ++a) {
                    compatible = compatible && other.args[a].kind == def->args[a].kind;
                    if (other.args[a].kind == ArgKind::Number && node.args[a].block < 0) {
                        char* end = nullptr; const double n = std::strtod(node.args[a].value.c_str(), &end);
                        compatible = compatible && std::isfinite(n) && end && !*end && n >= other.args[a].min && n <= other.args[a].max;
                    }
                }
                if (!compatible) continue;
                BlockProgram candidate = current; candidate.nodes[id].key = other.key; consider(std::move(candidate));
            }
            if (def->shape == BlockShape::Statement) { BlockProgram candidate = current; removeBlock(candidate, id); consider(std::move(candidate)); }
        }
        if (bestScore > 0 && wanted.size() > baseline.tokens.size() && !timedOut) {
            for (const auto& position : current.nodes) {
                if (timedOut || attempts > 8000 || bestScore == 0) break;
                if (position.second.key == "code.file" || placeOf(current, position.first) == 0) continue;
                const auto* parent = blockDef(position.second.key);
                if (!parent || parent->shape == BlockShape::Number || parent->shape == BlockShape::Boolean) continue;
                for (const BlockDef& def : blockDefs()) {
                    if (timedOut || attempts > 8000 || bestScore == 0) break;
                    if (def.shape != BlockShape::Statement) continue;
                    BlockProgram added = current;
                    const int id = newBlock(added, def.key);
                    if ((def.places & slotPlace(added, position.first, -1)) == 0) continue;
                    attachAfter(added, position.first, id);
                    consider(added);
                    for (size_t a = 0; a < def.args.size() && !timedOut && attempts <= 8000; ++a) {
                        const auto& slot = def.args[a];
                        const auto& values = slot.kind == ArgKind::Number ? nums : strings;
                        if (slot.kind != ArgKind::Number && slot.kind != ArgKind::Text && slot.kind != ArgKind::Sound) continue;
                        for (const auto& value : values) {
                            if (timedOut || attempts > 8000 || bestScore == 0) break;
                            if (slot.kind == ArgKind::Number) {
                                char* end = nullptr; const double n = std::strtod(value.c_str(), &end);
                                if (!std::isfinite(n) || !end || *end || n < slot.min || n > slot.max) continue;
                            }
                            BlockProgram candidate = added;
                            candidate.nodes[id].args[a].value = value;
                            consider(std::move(candidate));
                        }
                    }
                }
            }
        }
        if (bestScore >= score) break;
        current = std::move(best); score = bestScore;
    }
    if (score != 0) {
        result.error = timedOut ? "Automatic synchronization reached its time limit. The draft and last valid blocks are preserved."
            : "Some code cannot be represented by the current blocks. The draft and last valid graph are preserved; use Custom file to keep it explicitly.";
        return result;
    }
    const BlockCode regenerated = generateBlocks(engine, type, fileName, current, look);
    const BlockFile* updated = fileAt(regenerated, path);
    applyComments(current, baseline, target, updated ? lex(updated->text, engine == Engine::Psych, config) : baseline);
    result.program = std::move(current); result.applied = true; result.error.clear();
    return result;
}

CodeImportResult keepCustomSource(Engine engine, const BlockProgram& original, const std::string& path, const std::string& text) {
    CodeImportResult result;
    result.program = original;
    const bool config = path.size() >= 4 && path.substr(path.size() - 4) == ".txt";
    const Lexed checked = lex(text, engine == Engine::Psych, config);
    result.error = checked.error; result.line = checked.line; result.syntaxValid = checked.error.empty();
    if (!result.syntaxValid) return result;
    const std::string prefix = engine == Engine::Codename ? "data/notes/" : engine == Engine::Psych ? "custom_notetypes/" : "scripts/notekinds/";
    const size_t dot = path.find_last_of('.');
    const std::string ext = dot == std::string::npos ? std::string() : path.substr(dot);
    const bool pathOK = path.rfind(prefix, 0) == 0 && dot > prefix.size() &&
        path.find_first_of("/\\:", prefix.size()) == std::string::npos &&
        (engine == Engine::Codename ? ext == ".hx" : engine == Engine::Psych ? ext == ".lua" || ext == ".txt" : ext == ".hxc");
    if (!pathOK) { result.error = "The custom file must stay within this engine's note-type directory."; return result; }
    for (auto& item : result.program.nodes)
        if (item.second.key == "code.file" && item.second.args.size() == 3 && item.second.args[0].value == engineKey(engine) && item.second.args[1].value == path) {
            item.second.args[2].value = text;
            result.program.drafts.erase(draftKey(engine, path));
            result.applied = true;
            return result;
        }
    int id = newBlock(result.program, "code.file");
    result.program.nodes[id].args = {{engineKey(engine), -1}, {path, -1}, {text, -1}};
    placeTop(result.program, id, 440.0f, 60.0f + static_cast<float>(result.program.tops.size()) * 90.0f);
    result.program.drafts.erase(draftKey(engine, path));
    result.applied = true;
    return result;
}

const char* previewCoverage(const std::string& key, bool spanish) {
    if (key == "code.file") return spanish ? "Archivo propio: no se simula" : "Custom file: not simulated";
    if (key == "play.avoid" || key == "hit.causesMiss") return spanish ? "Regla aplicada en la preview" : "Rule applied in preview";
    if (key.rfind("event.", 0) == 0 || key.rfind("ctl.", 0) == 0 || key.rfind("op.", 0) == 0 || key.rfind("get.", 0) == 0 || key.rfind("is.", 0) == 0)
        return spanish ? "Lógica generada; la preview no ejecuta todo el programa" : "Generated logic; preview does not execute the full program";
    return spanish ? "Se exporta; todavía no se simula" : "Exported; not simulated yet";
}

// ------------------------------------------------- pasar un script a bloques --

namespace {

std::string importTrim(const std::string& text) {
    const size_t a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = text.find_last_not_of(" \t\r\n;");
    return b == std::string::npos || b < a ? std::string() : text.substr(a, b - a + 1);
}

// La linea sin su comentario (fuera de las comillas).
std::string importStrip(const std::string& line, bool lua) {
    char quote = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quote) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') quote = c;
        else if (lua && c == '-' && i + 1 < line.size() && line[i + 1] == '-') return line.substr(0, i);
        else if (!lua && c == '/' && i + 1 < line.size() && line[i + 1] == '/') return line.substr(0, i);
    }
    return line;
}

// Cuanto abre y cierra una linea (palabras de Lua o llaves de HScript), fuera de comillas.
int importDepth(const std::string& line, bool lua) {
    int depth = 0;
    char quote = 0;
    std::string word;
    auto flush = [&]() {
        if (word == "function" || word == "do" || word == "then" || word == "repeat") ++depth;
        if (word == "elseif") --depth;   // su «then» no abre otro bloque
        if (word == "end" || word == "until") --depth;
        word.clear();
    };
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quote) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            if (lua) flush();
            quote = c;
            continue;
        }
        if (lua) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') word += c;
            else flush();
        } else {
            if (c == '{') ++depth;
            if (c == '}') --depth;
        }
    }
    if (lua) flush();
    return depth;
}

struct ImportFunction {
    std::string name;
    std::vector<std::string> body;   // sin la firma ni el cierre
};

// Las funciones de primer nivel; lo de fuera, en `outside`.
std::vector<ImportFunction> importFunctions(const std::string& text, bool lua, std::vector<std::string>& outside) {
    std::vector<ImportFunction> out;
    std::istringstream in(text);
    std::string raw;
    int depth = 0;
    ImportFunction* current = nullptr;
    bool awaitingOpen = false, classOpen = false;
    static const std::regex signature(R"(^\s*(?:local\s+|override\s+|public\s+|private\s+|static\s+|inline\s+)*function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\()");
    static const std::regex classLine(R"(^\s*(?:@:\w+(?:\([^)]*\))?\s*)*(?:public\s+|private\s+)?class\s+\w+)");
    while (std::getline(in, raw)) {
        const std::string line = importStrip(raw, lua);
        int change = importDepth(line, lua);
        // Una clase de HScript (V-Slice) no cuenta: sus metodos son como funciones.
        if (!lua && !current && std::regex_search(line, classLine)) {
            classOpen = line.find('{') == std::string::npos;
            continue;
        }
        if (!lua && classOpen && importTrim(line) == "{") {
            classOpen = false;
            continue;
        }
        // La llave de la funcion en la linea siguiente (estilo Allman).
        if (current && awaitingOpen) {
            depth += change;
            if (depth > 0) awaitingOpen = false;
            continue;
        }
        if (depth == 0 && !current) {
            std::smatch m;
            if (std::regex_search(line, m, signature)) {
                out.push_back({m[1].str(), {}});
                current = &out.back();
                depth += change;
                if (depth <= 0) {
                    if (!lua && line.find('{') == std::string::npos) awaitingOpen = true;   // la llave viene despues
                    else current = nullptr;                                              // todo en una linea
                    depth = 0;
                }
                continue;
            }
            if (!importTrim(line).empty()) outside.push_back(importTrim(line));
            depth = std::max(0, depth + change);
            continue;
        }
        depth += change;
        if (current && depth <= 0) {
            // La linea del cierre: lo que haya antes de el, cuenta.
            const std::string before = importTrim(lua ? line.substr(0, line.rfind("end")) : line.substr(0, line.rfind('}')));
            if (!before.empty()) current->body.push_back(before);
            current = nullptr;
            depth = 0;
            continue;
        }
        if (current) current->body.push_back(line);
    }
    return out;
}

std::string importLower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

}  // namespace

ScriptImport importNoteScript(Engine engine, const std::string& type, const std::string& text) {
    ScriptImport out;
    const bool lua = engine == Engine::Psych;
    BlockProgram& program = out.program;
    std::vector<std::string> outside;
    const std::vector<ImportFunction> functions = importFunctions(text, lua, outside);
    const std::string NUM = R"((-?\d*\.?\d+))";
    const std::string STR = R"(['"]([^'"]*)['"])";
    const std::string GAME = R"((?:PlayState\.instance\.|game\.)?)";
    struct Rule {
        std::regex pattern;
        std::string key;
        int sign;   // -1: el numero cambia de signo si el operador es «-»
    };
    const std::vector<Rule> rules = {
        {std::regex("^addHealth\\(" + NUM + "\\)$"), "do.health", 0},
        {std::regex("^setProperty\\(['\"]health['\"],\\s*getProperty\\(['\"]health['\"]\\)\\s*([+-])\\s*" + NUM + "\\)$"), "do.health", -1},
        {std::regex("^" + GAME + "health\\s*([+-])=\\s*" + NUM + "$"), "do.health", -1},
        {std::regex("^setHealth\\(" + NUM + "\\)$"), "do.setHealth", 0},
        {std::regex("^setProperty\\(['\"]health['\"],\\s*" + NUM + "\\)$"), "do.setHealth", 0},
        {std::regex("^" + GAME + "health\\s*=\\s*" + NUM + "$"), "do.setHealth", 0},
        {std::regex("^addScore\\(" + NUM + "\\)$"), "do.score", 0},
        {std::regex("^" + GAME + "songScore\\s*([+-])=\\s*" + NUM + "$"), "do.score", -1},
        {std::regex("^addMisses\\(" + NUM + "\\)$"), "do.misses", 0},
        {std::regex("^" + GAME + "(?:songMisses|misses)\\s*([+-])=\\s*" + NUM + "$"), "do.misses", -1},
        {std::regex("^setProperty\\(['\"]combo['\"],\\s*" + NUM + "\\)$"), "do.combo", 0},
        {std::regex("^" + GAME + "combo\\s*=\\s*" + NUM + "$"), "do.combo", 0},
    };
    auto valueOf = [](const std::smatch& m, int sign) {
        // Con signo: grupo 1 el operador y 2 el numero; sin el, grupo 1.
        if (sign < 0) {
            std::string number = m[2].str();
            if (m[1].str() == "-") number = number.rfind('-', 0) == 0 ? number.substr(1) : "-" + number;
            return number;
        }
        return m[1].str();
    };
    auto newHat = [&](const char* key, const char* who) {
        const int hat = newBlock(program, key);
        if (who && !program.node(hat)->args.empty()) program.node(hat)->args[0].value = who;
        placeTop(program, hat, 40.0f + 340.0f * static_cast<float>(program.tops.size()), 40.0f);
        return hat;
    };
    // Una sentencia de un evento: un bloque si se reconoce, si no un «codigo».
    static const std::regex negativeGuard(R"(^if\s*\(?.*(?:noteType|kind)\s*(?:~=|!=)\s*['"]([^'"]+)['"].*\)?\s*(?:then\s+)?return(?:\s+end)?;?$)");
    static const std::regex cancelledGuard(R"(^if\s*\(?\s*(?:event\.)?(?:eventCanceled|cancelled)\s*\)?\s*(?:then\s+)?return(?:\s+end)?;?$)");
    auto statement = [&](int& last, const std::string& raw, bool asCode) {
        const std::string line = importTrim(raw);
        if (line.empty()) return;
        std::smatch guardMatch;
        if (std::regex_match(line, guardMatch, negativeGuard) && importLower(guardMatch[1].str()) == importLower(type)) return;
        if (std::regex_match(line, cancelledGuard)) return;
        int id = -1;
        std::smatch m;
        if (!asCode) {
            for (const Rule& rule : rules)
                if (std::regex_match(line, m, rule.pattern)) {
                    id = newBlock(program, rule.key);
                    program.node(id)->args[0].value = valueOf(m, rule.sign);
                    break;
                }
            static const std::regex sound(R"(^(?:playSound\(|(?:FlxG\.sound\.play|FunkinSound\.playOnce)\(Paths\.sound\()['"]([^'"]+)['"]\)?(?:,\s*(-?\d*\.?\d+))?\)$)");
            static const std::regex shake(R"(^(?:cameraShake\(['"]game['"],\s*|(?:FlxG\.camera|camGame|PlayState\.instance\.camGame|game\.camGame)\.shake\()(-?\d*\.?\d+),\s*(-?\d*\.?\d+)\)$)");
            static const std::regex flash(R"(^cameraFlash\(['"]game['"],\s*['"]#?([0-9A-Fa-f]{6})['"],\s*(-?\d*\.?\d+)\)$)");
            static const std::regex hey(R"(^(?:characterPlayAnim\(['"](?:boyfriend|bf)['"],\s*['"]hey['"](?:,\s*true)?\)|(?:PlayState\.instance\.|game\.)?boyfriend\.playAnim\(['"]hey['"](?:,\s*true)?\))$)");
            static const std::regex log(R"(^(?:debugPrint|trace)\(['"]([^'"]*)['"]\)$)");
            if (id < 0 && std::regex_match(line, m, sound)) {
                id = newBlock(program, "do.sound");
                program.node(id)->args[0].value = "sounds/" + m[1].str() + ".ogg";
                if (program.node(id)->args.size() > 1) program.node(id)->args[1].value = m[2].matched ? m[2].str() : "1";
            } else if (id < 0 && std::regex_match(line, m, shake)) {
                id = newBlock(program, "do.shake");
                program.node(id)->args[0].value = m[1].str();
                program.node(id)->args[1].value = m[2].str();
            } else if (id < 0 && std::regex_match(line, m, flash)) {
                id = newBlock(program, "do.flash");
                program.node(id)->args[0].value = m[1].str();
                program.node(id)->args[1].value = m[2].str();
            } else if (id < 0 && std::regex_match(line, hey)) {
                id = newBlock(program, "do.hey");
            } else if (id < 0 && std::regex_match(line, m, log)) {
                id = newBlock(program, "do.log");
                program.node(id)->args[0].value = m[1].str();
            }
        }
        if (id >= 0) {
            ++out.blocks;
        } else {
            id = newBlock(program, "code.line");
            program.node(id)->args[0].value = engineKey(engine);
            program.node(id)->args[1].value = line;
            ++out.kept;
        }
        attachAfter(program, last, id);
        last = id;
    };
    // La comprobacion del tipo («if noteType == 'X' then», «if (event.noteType ==
    // "X") {»...): su cuerpo es lo que importa; la de otro tipo se salta.
    static const std::regex guard(R"(^\s*if\s*\(?\s*(?:[A-Za-z_][A-Za-z0-9_.]*\.)?(?:noteType|kind)\s*==\s*['"]([^'"]+)['"]\s*\)?\s*(?:then|\{)?\s*$)");
    auto typeBody = [&](const std::vector<std::string>& body, bool& otherType) {
        otherType = false;
        for (size_t i = 0; i < body.size(); ++i) {
            std::smatch m;
            if (!std::regex_match(body[i], m, guard)) continue;
            if (importLower(m[1].str()) != importLower(type)) {
                otherType = true;
                return std::vector<std::string>{};
            }
            std::vector<std::string> inner;
            int depth = importDepth(body[i], lua);
            for (size_t j = i + 1; j < body.size(); ++j) {
                depth += importDepth(body[j], lua);
                if (depth <= 0) break;
                inner.push_back(body[j]);
            }
            return inner;
        }
        return body;
    };
    for (const ImportFunction& function : functions) {
        const std::string name = function.name;
        const char* hatKey = nullptr;
        const char* who = nullptr;
        if (lua) {
            if (name == "goodNoteHit") { hatKey = "event.hit"; who = "player"; }
            else if (name == "opponentNoteHit") { hatKey = "event.hit"; who = "opponent"; }
            else if (name == "noteMiss") hatKey = "event.miss";
            else if (name == "onCreate" || name == "onCreatePost") hatKey = "event.create";
        } else {
            if (name == "onPlayerHit" || name == "onPostPlayerHit") { hatKey = "event.hit"; who = "player"; }
            else if (name == "onDadHit" || name == "onPostDadHit") { hatKey = "event.hit"; who = "opponent"; }
            else if (name == "onNoteHit" || name == "onPostNoteHit") { hatKey = "event.hit"; who = engine == Engine::VSlice ? "player" : "any"; }
            else if (name == "onPlayerMiss" || name == "onPostPlayerMiss" || name == "onNoteMiss") hatKey = "event.miss";
        }
        if (!hatKey && name == "new") continue;   // el constructor de un NoteKind
        if (!hatKey) {
            out.notes.push_back({"«" + name + "» (" + std::to_string(function.body.size()) + " lines) has no event block: it stays in the original script.",
                                 "«" + name + "» (" + std::to_string(function.body.size()) + " líneas) no tiene bloque de evento: se queda en el script original."});
            continue;
        }
        if (std::string(hatKey) == "event.create") {
            // Psych: el bucle de unspawnNotes con setPropertyFromGroup.
            static const std::regex prop(R"(^setPropertyFromGroup\(['"]unspawnNotes['"],\s*i,\s*['"](\w+)['"],\s*(.+)\)$)");
            int hat = -1, last = -1, skipped = 0;
            for (const std::string& raw : function.body) {
                const std::string line = importTrim(raw);
                std::smatch m;
                if (!std::regex_match(line, m, prop)) {
                    const bool structure = line.empty() || line == "end" || line.find("unspawnNotes") != std::string::npos;
                    if (!structure) ++skipped;
                    continue;
                }
                const std::string field = m[1].str();
                std::string value = importTrim(m[2].str());
                if (value.size() >= 2 && (value.front() == '\'' || value.front() == '"')) value = value.substr(1, value.size() - 2);
                const char* key = field == "hitHealth" ? "hit.health" : field == "missHealth" ? "miss.health" : field == "ignoreNote" ? "play.avoid"
                                : field == "hitCausesMiss" ? "hit.causesMiss" : field == "noAnimation" ? "hit.noAnim" : field == "noMissAnimation" ? "miss.noAnim"
                                : field == "animSuffix" ? "hit.animSuffix" : field == "gfNote" ? "hit.singer" : nullptr;
                const bool flag = field == "ignoreNote" || field == "hitCausesMiss" || field == "noAnimation" || field == "noMissAnimation" || field == "gfNote";
                if (field == "texture") {
                    out.notes.push_back({"Its texture «" + value + "»: give it a look with «Change its look…».",
                                         "Su textura «" + value + "»: dale aspecto con «Cambiar su aspecto…»."});
                    continue;
                }
                if (!key || (flag && value != "true")) {
                    ++skipped;
                    continue;
                }
                if (hat < 0) last = hat = newHat("event.create", nullptr);
                const int id = newBlock(program, key);
                if (!flag && !program.node(id)->args.empty()) program.node(id)->args[0].value = value;
                attachAfter(program, last, id);
                last = id;
                ++out.blocks;
            }
            if (skipped > 0)
                out.notes.push_back({"«" + name + "»: " + std::to_string(skipped) + " lines that aren't note properties stay in the original script.",
                                     "«" + name + "»: " + std::to_string(skipped) + " líneas que no son propiedades de la nota se quedan en el script original."});
            continue;
        }
        bool otherType = false;
        const std::vector<std::string> body = typeBody(function.body, otherType);
        if (otherType || body.empty()) continue;
        int last = newHat(hatKey, who);
        for (const std::string& line : body) statement(last, line, false);
    }
    if (!outside.empty())
        out.notes.push_back({std::to_string(outside.size()) + " lines outside any function stay in the original script.",
                             std::to_string(outside.size()) + " líneas fuera de las funciones se quedan en el script original."});
    return out;
}

}
