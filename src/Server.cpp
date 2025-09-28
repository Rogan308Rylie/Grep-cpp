#include <iostream>
#include <string>
#include <cctype>
#include <vector>

using namespace std;

// Represents a single pattern component
struct PatternComponent {
    enum Type { LITERAL, DIGIT, WORD, POSITIVE_CLASS, NEGATIVE_CLASS, START_ANCHOR };
    Type type;
    string value; // For literals and character classes
    
    PatternComponent(Type t, string v = "") : type(t), value(v) {}
};

// Parse the pattern into components
vector<PatternComponent> parse_pattern(const string& pattern) {
    vector<PatternComponent> components;
    
    for (int i = 0; i < pattern.length(); i++) {
        if (pattern[i] == '^') {
            // Handle start anchor
            components.push_back(PatternComponent(PatternComponent::START_ANCHOR));
        } else if (pattern[i] == '\\' && i + 1 < pattern.length()) {
            // Handle escape sequences
            char next = pattern[i + 1];
            if (next == 'd') {
                components.push_back(PatternComponent(PatternComponent::DIGIT));
                i++; // Skip the next character
            } else if (next == 'w') {
                components.push_back(PatternComponent(PatternComponent::WORD));
                i++; // Skip the next character
            } else {
                // If it's not a recognized escape, treat as literal
                components.push_back(PatternComponent(PatternComponent::LITERAL, string(1, pattern[i])));
            }
        } else if (pattern[i] == '[') {
            // Handle character classes
            int end = i;
            while (end < pattern.length() && pattern[end] != ']') {
                end++;
            }
            if (end < pattern.length()) {
                string class_content = pattern.substr(i + 1, end - i - 1);
                if (!class_content.empty() && class_content[0] == '^') {
                    components.push_back(PatternComponent(PatternComponent::NEGATIVE_CLASS, class_content.substr(1)));
                } else {
                    components.push_back(PatternComponent(PatternComponent::POSITIVE_CLASS, class_content));
                }
                i = end; // Move past the closing bracket
            } else {
                // No closing bracket found, treat as literal
                components.push_back(PatternComponent(PatternComponent::LITERAL, string(1, pattern[i])));
            }
        } else {
            // Regular literal character
            components.push_back(PatternComponent(PatternComponent::LITERAL, string(1, pattern[i])));
        }
    }
    
    return components;
}

// Check if a character matches a single pattern component
bool matches_component(char c, const PatternComponent& component) {
    switch (component.type) {
        case PatternComponent::LITERAL:
            return c == component.value[0];
            
        case PatternComponent::DIGIT:
            return isdigit(c);
            
        case PatternComponent::WORD:
            return isalnum(c) || c == '_';
            
        case PatternComponent::POSITIVE_CLASS:
            for (char pattern_char : component.value) {
                if (c == pattern_char) {
                    return true;
                }
            }
            return false;
            
        case PatternComponent::NEGATIVE_CLASS:
            for (char pattern_char : component.value) {
                if (c == pattern_char) {
                    return false;
                }
            }
            return true;
    }
    return false;
}

// Try to match the full pattern starting at a specific position in the input
bool match_at_position(const string& input, int start_pos, const vector<PatternComponent>& components) {
    if (start_pos + components.size() > input.length()) {
        return false; // Not enough characters left
    }
    
    for (int i = 0; i < components.size(); i++) {
        if (!matches_component(input[start_pos + i], components[i])) {
            return false;
        }
    }
    
    return true;
}

// Main pattern matching function
bool match_pattern(const string& input_line, const string& pattern) {
    vector<PatternComponent> components = parse_pattern(pattern);
    
    // Check if pattern starts with start anchor
    bool has_start_anchor = !components.empty() && components[0].type == PatternComponent::START_ANCHOR;
    
    if (has_start_anchor) {
        // If we have start anchor, only try matching from position 0
        // Skip the start anchor component in matching
        vector<PatternComponent> actual_pattern(components.begin() + 1, components.end());
        return match_at_position(input_line, 0, actual_pattern);
    } else {
        // Try matching at every position in the input (original behavior)
        for (int i = 0; i <= (int)input_line.length() - (int)components.size(); i++) {
            if (match_at_position(input_line, i, components)) {
                return true;
            }
        }
        return false;
    }
}

int main(int argc, char* argv[]) {
    // Flush after every cout / cerr
    cout << unitbuf;
    cerr << unitbuf;

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    cerr << "Logs from your program will appear here" << endl;

    if (argc != 3) {
        cerr << "Expected two arguments" << endl;
        return 1;
    }

    string flag = argv[1];
    string pattern = argv[2];

    if (flag != "-E") {
        cerr << "Expected first argument to be '-E'" << endl;
        return 1;
    }

    string input_line;
    getline(cin, input_line);

    try {
        if (match_pattern(input_line, pattern)) {
            return 0;
        } else {
            return 1;
        }
    } catch (const runtime_error& e) {
        cerr << e.what() << endl;
        return 1;
    }
}