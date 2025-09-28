#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
int captured_group_count = 0;
std::unordered_map<int, std::string_view> captured_group_match;

std::size_t match_single_char(std::string_view input_line, char pattern) {
    return static_cast<std::size_t>(input_line[0] == pattern);
}
std::size_t match_digit(std::string_view input_line) {
    return static_cast<std::size_t>(std::ranges::find_if(input_line, isdigit) == input_line.begin());
}
std::size_t match_alphanum(std::string_view input_line) {
    return static_cast<std::size_t>(std::ranges::find_if(input_line, isalnum) == input_line.begin());
}

std::size_t match_group(std::string_view input_line, std::string_view pattern) {
    if (pattern[0] == '^') return static_cast<std::size_t>(input_line.find_first_not_of(pattern.substr(1)) == 0);
    return static_cast<std::size_t>(input_line.find_first_of(pattern) == 0);
}

std::size_t match_backreference(std::string_view input_line, int group_id) {
    if (!captured_group_match.contains(group_id)) return 0;
    if (input_line.starts_with(captured_group_match[group_id])) return captured_group_match[group_id].size();
    return 0;
}

enum class TokenType { character, digit, alphnum, wildcard, backreference, group, parenthesis, pattern };
std::string_view type_to_str(TokenType type) {
    switch (type) {
        case TokenType::character:
            return "character";
        case TokenType::digit:
            return "digit";
        case TokenType::alphnum:
            return "alphnum";
        case TokenType::wildcard:
            return "wildcard";
        case TokenType::backreference:
            return "backreference";
        case TokenType::group:
            return "group";
        case TokenType::parenthesis:
            return "parenthesis";
        case TokenType::pattern:
            return "pattern";
    }
    std::unreachable();
}

class Token {
   private:
    const TokenType m_type;
    const std::string_view m_pattern;
    int captured_group_idx{0};
    std::vector<Token> subtokens{};
    bool one_or_more{false};
    bool zero_or_one{false};

   public:
    Token(const TokenType type, const std::string_view pattern, int captured_group_idx = 0)
        : m_type{type}, m_pattern{pattern}, captured_group_idx{captured_group_idx} {
        if (type == TokenType::parenthesis) {
            using namespace std::literals;
            for (const auto word : std::views::split(pattern, "|"sv)) {
                std::cout << "hello " << std::string_view(word) << std::endl;
                subtokens.push_back(Token(TokenType::pattern, std::string_view(word)));
            }
        }
        if (type == TokenType::pattern) {
            std::string_view local_pattern{pattern};
            while (local_pattern.size()) {
                if (local_pattern.starts_with("\\d")) {
                    subtokens.push_back(Token(TokenType::digit, "\\d"));
                    local_pattern = local_pattern.substr(2);
                } else if (local_pattern.starts_with("\\w")) {
                    subtokens.push_back(Token(TokenType::alphnum, "\\w"));
                    local_pattern = local_pattern.substr(2);
                } else if (local_pattern.starts_with(".")) {
                    subtokens.push_back(Token(TokenType::wildcard, "."));
                    local_pattern = local_pattern.substr(1);
                } else if (local_pattern.starts_with("[")) {
                    const auto end = local_pattern.find("]");
                    subtokens.push_back(Token(TokenType::group, local_pattern.substr(1, end - 1)));
                    local_pattern = local_pattern.substr(end + 1);
                } else if (local_pattern.starts_with("(")) {
                    const auto end = local_pattern.find(")");
                    subtokens.push_back(
                        Token(TokenType::parenthesis, local_pattern.substr(1, end - 1), ++captured_group_count));
                    local_pattern = local_pattern.substr(end + 1);
                } else if (local_pattern.starts_with("\\") && isdigit(local_pattern[1])) {
                    subtokens.push_back(
                        Token(TokenType::backreference, local_pattern.substr(0, 2), local_pattern[1] - '0'));
                    local_pattern = local_pattern.substr(2);
                } else if (local_pattern.starts_with("?")) {
                    assert(subtokens.size());
                    subtokens.back().set_zero_or_one(true);
                    local_pattern = local_pattern.substr(1);
                } else if (local_pattern.starts_with("+")) {
                    assert(subtokens.size());
                    subtokens.back().set_one_or_more(true);
                    local_pattern = local_pattern.substr(1);
                } else {
                    subtokens.push_back(Token(TokenType::character, local_pattern.substr(0, 1)));
                    local_pattern = local_pattern.substr(1);
                }
            }
        }
    }

    TokenType type() const {
        return m_type;
    }

    std::string_view pattern() const {
        return m_pattern;
    }

    void set_one_or_more(bool x) {
        one_or_more = x;
    }
    void set_zero_or_one(bool x) {
        zero_or_one = x;
    }

    std::vector<std::size_t> match_sizes(std::string_view input) const {
        std::vector<std::size_t> res;
        if (zero_or_one) res.push_back(0);
        if (m_type == TokenType::pattern) {
            std::vector<std::size_t> current_match_sizes{0};
            for (const auto& tok : subtokens) {
                std::vector<std::size_t> next_match_sizes{};
                for (auto dim : current_match_sizes) {
                    auto add = tok.match_sizes(input.substr(dim));
                    for (auto& x : add) x += dim;
                    next_match_sizes.insert(next_match_sizes.end(), add.begin(), add.end());
                }
                current_match_sizes = std::move(next_match_sizes);
            }
            return current_match_sizes;
        }
        if (m_type == TokenType::parenthesis) {
            std::vector<std::size_t> current_match_sizes{};
            for (const auto& tok : subtokens) {
                const auto add = tok.match_sizes(input);
                current_match_sizes.insert(current_match_sizes.end(), add.begin(), add.end());
            }
            std::ranges::sort(current_match_sizes);
            current_match_sizes.resize(std::unique(current_match_sizes.begin(), current_match_sizes.end()) -
                                       current_match_sizes.begin());

            if (!current_match_sizes.empty()) {
                captured_group_match[captured_group_idx] = input.substr(0, current_match_sizes.back());
            }
            return current_match_sizes;
        }

        std::size_t current_match_size{0};
        while (input.size()) {
            std::size_t len{0};
            switch (m_type) {
                case TokenType::character:
                    len = match_single_char(input, m_pattern[0]);
                    break;
                case TokenType::digit:
                    len = match_digit(input);
                    break;
                case TokenType::alphnum:
                    len = match_alphanum(input);
                    break;
                case TokenType::wildcard:
                    len = 1;
                    break;
                case TokenType::backreference:
                    len = match_backreference(input, captured_group_idx);
                    break;
                case TokenType::group:
                    len = match_group(input, m_pattern);
                    break;
            }
            if (len) {
                current_match_size += len;
                res.push_back(current_match_size);
                input = input.substr(len);
            } else {
                break;
            }
            if (!one_or_more) break;
        }
        if (input.empty() && m_pattern == "$") {
            res.push_back(current_match_size);
        }
        return res;
    }
    friend std::ostream& operator<<(std::ostream& o, const Token& t) {
        o << type_to_str(t.m_type) << " :{";
        for (auto x : t.subtokens) {
            o << x << ",";
        }
        o << "}";
        return o;
    }
};

bool match_pattern_start(std::string_view input_line, std::span<Token> tokens) {
    if (tokens.empty()) return true;
    if (input_line.empty()) return tokens.size() == 1 && tokens[0].pattern() == "$";
    std::vector<std::size_t> match_sizes{tokens[0].match_sizes(input_line)};
    for (const auto dim : match_sizes) {
        if (match_pattern_start(input_line.substr(dim), tokens.subspan(1))) return true;
    }
    return false;
}
bool match_pattern(std::string_view input_line, std::string_view pattern) {
    bool line_start = pattern[0] == '^';
    if (line_start) pattern = pattern.substr(1);
    Token parser(TokenType::pattern, pattern);
    std::cout << parser << std::endl;
    if (line_start) return !parser.match_sizes(input_line).empty();
    for (int i = 0; i < input_line.size(); i++) {
        if (!parser.match_sizes(input_line.substr(i)).empty()) return true;
    }
    return false;
}

int main(int argc, char* argv[]) {
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    if (argc != 3) {
        std::cerr << "Expected two arguments" << std::endl;
        return 1;
    }

    std::string flag = argv[1];
    std::string pattern = argv[2];

    if (flag != "-E") {
        std::cerr << "Expected first argument to be '-E'" << std::endl;
        return 1;
    }

    std::string input_line;
    std::getline(std::cin, input_line);

    try {
        if (match_pattern(input_line, pattern)) {
            std::cout << "match" << std::endl;
            return 0;
        } else {
            std::cout << "no match" << std::endl;
            return 1;
        }
    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}