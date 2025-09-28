#include <iostream>
#include <string>
#include <cctype>
#include <vector>

using namespace std;

// Represents a single pattern component
struct PatternComponent {
    enum Type { LITERAL, DIGIT, WORD, POSITIVE_CLASS, NEGATIVE_CLASS, START_ANCHOR, END_ANCHOR, DOT };
    Type type;
    string value; // For literals and character classes
    bool has_plus; // Whether this component has a + quantifier
    bool has_question; // Whether this component has a ? quantifier
    
    PatternComponent(Type t, string v = "", bool plus = false, bool question = false) : type(t), value(v), has_plus(plus), has_question(question) {}
};

// Parse the pattern into components
vector<PatternComponent> parse_pattern(const string& pattern) {
    vector<PatternComponent> components;
    
    for (int i = 0; i < pattern.length(); i++) {
        PatternComponent component(PatternComponent::LITERAL);
        bool component_created = false;
        
        if (pattern[i] == '^') {
            component = PatternComponent(PatternComponent::START_ANCHOR);
            component_created = true;
        } else if (pattern[i] == '$') {
            component = PatternComponent(PatternComponent::END_ANCHOR);
            component_created = true;
        } else if (pattern[i] == '.') {
            component = PatternComponent(PatternComponent::DOT);
            component_created = true;
        } else if (pattern[i] == '\\' && i + 1 < pattern.length()) {
            char next = pattern[i + 1];
            if (next == 'd') {
                component = PatternComponent(PatternComponent::DIGIT);
                i++; // Skip the next character
                component_created = true;
            } else if (next == 'w') {
                component = PatternComponent(PatternComponent::WORD);
                i++; // Skip the next character
                component_created = true;
            } else {
                component = PatternComponent(PatternComponent::LITERAL, string(1, pattern[i]));
                component_created = true;
            }
        } else if (pattern[i] == '[') {
            int end = i;
            while (end < pattern.length() && pattern[end] != ']') {
                end++;
            }
            if (end < pattern.length()) {
                string class_content = pattern.substr(i + 1, end - i - 1);
                if (!class_content.empty() && class_content[0] == '^') {
                    component = PatternComponent(PatternComponent::NEGATIVE_CLASS, class_content.substr(1));
                } else {
                    component = PatternComponent(PatternComponent::POSITIVE_CLASS, class_content);
                }
                i = end; // Move past the closing bracket
                component_created = true;
            } else {
                component = PatternComponent(PatternComponent::LITERAL, string(1, pattern[i]));
                component_created = true;
            }
        } else {
            component = PatternComponent(PatternComponent::LITERAL, string(1, pattern[i]));
            component_created = true;
        }
        
        // Check for quantifiers after the component
        if (component_created && i + 1 < pattern.length()) {
            if (pattern[i + 1] == '+') {
                component.has_plus = true;
                i++; // Skip the + character
            } else if (pattern[i + 1] == '?') {
                component.has_question = true;
                i++; // Skip the ? character
            }
        }
        
        if (component_created) {
            components.push_back(component);
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
            
        case PatternComponent::DOT:
            return true; // Dot matches any character
    }
    return false;
}

// Advanced pattern matching with quantifier support
bool match_at_position_advanced(const string& input, int pos, const vector<PatternComponent>& components, int comp_idx, bool must_reach_end = false) {
    if (comp_idx >= components.size()) {
        return !must_reach_end || pos == input.length(); // If must_reach_end, we need to consume entire string
    }
    
    if (pos >= input.length()) {
        return false; // Ran out of input
    }
    
    const PatternComponent& current = components[comp_idx];
    
    if (current.has_plus) {
        // Must match at least once, then try to match as many as possible
        if (!matches_component(input[pos], current)) {
            return false; // Doesn't match even once
        }
        
        // Try matching different numbers of repetitions (greedy approach)
        for (int repeat_count = 1; pos + repeat_count <= input.length(); repeat_count++) {
            // Check if all characters in this repetition match
            bool all_match = true;
            for (int j = 0; j < repeat_count; j++) {
                if (!matches_component(input[pos + j], current)) {
                    all_match = false;
                    break;
                }
            }
            
            if (!all_match) {
                // Try with one fewer repetition
                return match_at_position_advanced(input, pos + repeat_count - 1, components, comp_idx + 1, must_reach_end);
            }
            
            // Try to match the rest of the pattern after these repetitions
            if (match_at_position_advanced(input, pos + repeat_count, components, comp_idx + 1, must_reach_end)) {
                return true;
            }
        }
        
        return false;
    } else if (current.has_question) {
        // Can match zero or one time
        // Try matching zero times first (skip this component)
        if (match_at_position_advanced(input, pos, components, comp_idx + 1, must_reach_end)) {
            return true;
        }
        
        // Try matching one time
        if (matches_component(input[pos], current)) {
            return match_at_position_advanced(input, pos + 1, components, comp_idx + 1, must_reach_end);
        }
        
        return false;
    } else {
        // Normal single character match
        if (matches_component(input[pos], current)) {
            return match_at_position_advanced(input, pos + 1, components, comp_idx + 1, must_reach_end);
        }
        return false;
    }
}

// Main pattern matching function
bool match_pattern(const string& input_line, const string& pattern) {
    vector<PatternComponent> components = parse_pattern(pattern);
    
    // Check for anchors
    bool has_start_anchor = !components.empty() && components[0].type == PatternComponent::START_ANCHOR;
    bool has_end_anchor = !components.empty() && components.back().type == PatternComponent::END_ANCHOR;
    
    // Remove anchors from the actual pattern to match
    vector<PatternComponent> actual_pattern = components;
    if (has_start_anchor) {
        actual_pattern.erase(actual_pattern.begin());
    }
    if (has_end_anchor) {
        actual_pattern.pop_back();
    }
    
    if (has_start_anchor && has_end_anchor) {
        // Must match exactly the entire string
        return match_at_position_advanced(input_line, 0, actual_pattern, 0, true);
    } else if (has_start_anchor) {
        // Must match from the start
        return match_at_position_advanced(input_line, 0, actual_pattern, 0, false);
    } else if (has_end_anchor) {
        // Must match at the end - try different starting positions but must consume to end
        for (int start_pos = 0; start_pos <= input_line.length(); start_pos++) {
            if (match_at_position_advanced(input_line, start_pos, actual_pattern, 0, true)) {
                return true;
            }
        }
        return false;
    } else {
        // Try matching at every position
        for (int i = 0; i < input_line.length(); i++) {
            if (match_at_position_advanced(input_line, i, actual_pattern, 0, false)) {
                return true;
            }
        }
        return false;
    }
}

int main(int argc, char* argv[]) {
    cout << unitbuf;
    cerr << unitbuf;

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