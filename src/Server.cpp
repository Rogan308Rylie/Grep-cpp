#include <iostream>
#include <string>
#include <cctype>
#include <vector>
#include <stdexcept>
using namespace std;

// --- New Struct for Match State (for captures) ---
struct MatchState {
    string group1_capture = "";
};
// -------------------------------------------------

// Represents a single pattern component
struct PatternComponent {
    // Added BACKREFERENCE Type
    enum Type { LITERAL, DIGIT, WORD, POSITIVE_CLASS, NEGATIVE_CLASS, START_ANCHOR, END_ANCHOR, DOT, ALTERNATION, BACKREFERENCE };
    Type type;
    string value; // For literals and character classes
    vector<vector<PatternComponent>> alternatives; // For alternation groups
    bool has_plus; // Whether this component has a + quantifier
    bool has_question; // Whether this component has a ? quantifier
    
    // New flag to mark the first capturing group
    bool is_group1_capture; 
    
    PatternComponent(Type t, string v = "", bool plus = false, bool question = false, bool g1 = false) 
        : type(t), value(v), alternatives(), has_plus(plus), has_question(question), is_group1_capture(g1) {}
};

// Forward declarations
// Signatures updated to include MatchState& state
bool match_at_position_advanced(const string& input, int pos, const vector<struct PatternComponent>& components, int comp_idx, bool must_reach_end, MatchState& state);

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
            
        case PatternComponent::ALTERNATION:
        case PatternComponent::BACKREFERENCE:
            return false; // These are handled in the sequence matchers, not per character
    }
    return false;
}

// Helper function to match a sequence of components and return the new position.
// Returns the index in the input after the successful match, or -1 on failure.
// This is necessary to cleanly calculate the captured string span and must handle
// all component types, including nested ALTERNATION groups.
int match_component_sequence(const string& input, int pos, const vector<PatternComponent>& components, int comp_idx, const MatchState& state_in) {
    if (comp_idx >= components.size()) {
        return pos; // Finished matching the sequence
    }
    
    // If we run out of input, check if remaining required components can match
    if (pos >= input.length()) {
        for (int i = comp_idx; i < components.size(); ++i) {
            const PatternComponent& remaining = components[i];
            // Only required components (non-question, non-empty backref) cause failure
            if (!remaining.has_question && !(remaining.type == PatternComponent::BACKREFERENCE && state_in.group1_capture.empty())) {
                 return -1;
            }
        }
        return pos;
    }
    
    const PatternComponent& current = components[comp_idx];
    
    if (current.type == PatternComponent::BACKREFERENCE) {
        const string& capture = state_in.group1_capture;
        if (capture.empty()) {
            // Empty capture matches zero length, continue
            return match_component_sequence(input, pos, components, comp_idx + 1, state_in);
        }
        
        if (pos + capture.length() > input.length() || input.substr(pos, capture.length()) != capture) {
            return -1;
        }
        return match_component_sequence(input, pos + capture.length(), components, comp_idx + 1, state_in);
    } 
    
    // --- FIX: Added handling for nested Alternation groups ---
    if (current.type == PatternComponent::ALTERNATION) {
        for (const auto& alternative : current.alternatives) {
            // Match the current alternative components recursively
            int end_alt_match = match_component_sequence(input, pos, alternative, 0, state_in);
            
            if (end_alt_match != -1) {
                // If alternative matched, continue matching the rest of the main sequence
                int result = match_component_sequence(input, end_alt_match, components, comp_idx + 1, state_in);
                if (result != -1) return result;
            }
        }
        return -1; // All alternatives failed
    }

    // --- Quantifier Logic (applies to simple character components) ---
    
    if (current.has_plus) {
        if (!matches_component(input[pos], current)) {
            return -1;
        }
        
        // Greedy match: try to match as many as possible
        for (int repeat_count = 1; pos + repeat_count <= input.length(); repeat_count++) {
            bool all_match = true;
            for (int j = 0; j < repeat_count; j++) {
                if (!matches_component(input[pos + j], current)) {
                    all_match = false;
                    break;
                }
            }
            
            if (!all_match) {
                // Try matching the rest of the pattern after one less repetition
                int result = match_component_sequence(input, pos + repeat_count - 1, components, comp_idx + 1, state_in);
                if (result != -1) return result;
                return -1; // Backtrack failure
            }
            
            // Try matching the rest of the pattern after these repetitions
            int result = match_component_sequence(input, pos + repeat_count, components, comp_idx + 1, state_in);
            if (result != -1) return result;
        }
        return -1;
        
    } else if (current.has_question) {
        // Try matching zero times first (skip this component)
        int result_skip = match_component_sequence(input, pos, components, comp_idx + 1, state_in);
        if (result_skip != -1) return result_skip;
        
        // Try matching one time
        if (matches_component(input[pos], current)) {
            return match_component_sequence(input, pos + 1, components, comp_idx + 1, state_in);
        }
        
        return -1;
    } else {
        // Normal single character match
        if (matches_component(input[pos], current)) {
            return match_component_sequence(input, pos + 1, components, comp_idx + 1, state_in);
        }
        return -1;
    }
}


// Parse the pattern into components
vector<PatternComponent> parse_pattern(const string& pattern) {
    vector<PatternComponent> components;
    bool group1_found = false; // Flag to track the first capturing group
    
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
        } else if (pattern[i] == '(') {
            // Handle alternation groups (capturing group)
            int depth = 1;
            int start = i + 1;
            i++; // Move past opening paren
            
            while (i < pattern.length() && depth > 0) {
                if (pattern[i] == '(') depth++;
                else if (pattern[i] == ')') depth--;
                i++;
            }
            
            if (depth == 0) {
                // Found matching closing paren
                string group_content = pattern.substr(start, i - start - 1);
                
                // Split by | to get alternatives
                vector<string> alt_strings;
                string current_alt = "";
                int paren_depth = 0;
                
                for (char c : group_content) {
                    if (c == '(' ) paren_depth++;
                    else if (c == ')') paren_depth--;
                    else if (c == '|' && paren_depth == 0) {
                        alt_strings.push_back(current_alt);
                        current_alt = "";
                        continue;
                    }
                    current_alt += c;
                }
                alt_strings.push_back(current_alt);
                
                // Parse each alternative
                PatternComponent alt_component(PatternComponent::ALTERNATION);
                for (const string& alt : alt_strings) {
                    vector<PatternComponent> alt_pattern = parse_pattern(alt);
                    alt_component.alternatives.push_back(alt_pattern);
                }
                
                // --- Group 1 Capture Logic ---
                // Only the *first* ( ) group is Group 1
                if (!group1_found) {
                    alt_component.is_group1_capture = true;
                    group1_found = true;
                }
                // -----------------------------
                
                component = alt_component;
                component_created = true;
                i--; // Adjust since we'll increment at end of loop
            } else {
                // Unmatched paren, treat as literal
                component = PatternComponent(PatternComponent::LITERAL, string(1, pattern[i]));
                component_created = true;
            }
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
            } else if (next == '1') { // --- Handle \1 Backreference ---
                component = PatternComponent(PatternComponent::BACKREFERENCE);
                i++; // Skip '1'
                component_created = true;
            } else {
                // Not a special escaped character, treat as literal
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

// Advanced pattern matching with quantifier and backreference support
// Signature updated to include MatchState& state
bool match_at_position_advanced(const string& input, int pos, const vector<PatternComponent>& components, int comp_idx, bool must_reach_end, MatchState& state) {
    if (comp_idx >= components.size()) {
        return !must_reach_end || pos == input.length(); // If must_reach_end, we need to consume entire string
    }
    
    if (pos >= input.length()) {
        // Special case: if we ran out of input, check if remaining components are all optional
        for (int remaining_idx = comp_idx; remaining_idx < components.size(); remaining_idx++) {
            const PatternComponent& remaining = components[remaining_idx];
            if (!remaining.has_question) {
                // If it's a backreference to an empty string, it's optional
                if (remaining.type == PatternComponent::BACKREFERENCE && state.group1_capture.empty()) {
                    continue;
                }
                return false; // Required component but no input left
            }
        }
        return true; // All remaining components are optional
    }
    
    const PatternComponent& current = components[comp_idx];
    
    if (current.type == PatternComponent::BACKREFERENCE) {
        const string& capture = state.group1_capture;
        
        // Check for empty capture first (can match an empty string)
        if (capture.empty()) {
            return match_at_position_advanced(input, pos, components, comp_idx + 1, must_reach_end, state);
        }
        
        // Check if the captured string matches the input at the current position
        if (pos + capture.length() <= input.length() && input.substr(pos, capture.length()) == capture) {
            // Match successful, move past the capture
            return match_at_position_advanced(input, pos + capture.length(), components, comp_idx + 1, must_reach_end, state);
        }
        
        return false;
        
    } else if (current.type == PatternComponent::ALTERNATION) {
        
        // Try each alternative
        for (const auto& alternative : current.alternatives) {
            
            // 1. Create a temporary MatchState for backtracking/failure.
            MatchState temp_state = state; 
            
            // 2. Use the helper function `match_component_sequence` to find the span of the group match.
            int end_of_group_match = match_component_sequence(input, pos, alternative, 0, temp_state);
            
            if (end_of_group_match != -1) {
                // The alternative matched up to `end_of_group_match`.
                
                // --- Group 1 Capture Logic ---
                if (current.is_group1_capture) {
                    temp_state.group1_capture = input.substr(pos, end_of_group_match - pos);
                }
                // -----------------------------

                // 3. Now try to match the rest of the main pattern starting from the new position.
                if (match_at_position_advanced(input, end_of_group_match, components, comp_idx + 1, must_reach_end, temp_state)) {
                    // Match succeeded all the way through! Commit the captured state.
                    state = temp_state;
                    return true;
                }
            }
            // If it failed, loop continues to the next alternative with the original 'state'.
        }
        return false;
        
    } else if (current.has_plus) {
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
                return match_at_position_advanced(input, pos + repeat_count - 1, components, comp_idx + 1, must_reach_end, state);
            }
            
            // Try to match the rest of the pattern after these repetitions
            if (match_at_position_advanced(input, pos + repeat_count, components, comp_idx + 1, must_reach_end, state)) {
                return true;
            }
        }
        
        return false;
        
    } else if (current.has_question) {
        // Can match zero or one time
        // Try matching zero times first (skip this component)
        if (match_at_position_advanced(input, pos, components, comp_idx + 1, must_reach_end, state)) {
            return true;
        }
        
        // Try matching one time
        if (matches_component(input[pos], current)) {
            return match_at_position_advanced(input, pos + 1, components, comp_idx + 1, must_reach_end, state);
        }
        
        return false;
        
    } else {
        // Normal single character match
        if (matches_component(input[pos], current)) {
            return match_at_position_advanced(input, pos + 1, components, comp_idx + 1, must_reach_end, state);
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
    
    // Initialize MatchState
    MatchState state;
    
    if (has_start_anchor && has_end_anchor) {
        // Must match exactly the entire string
        return match_at_position_advanced(input_line, 0, actual_pattern, 0, true, state);
    } else if (has_start_anchor) {
        // Must match from the start
        return match_at_position_advanced(input_line, 0, actual_pattern, 0, false, state);
    } else if (has_end_anchor) {
        // Must match at the end - try different starting positions but must consume to end
        for (int start_pos = 0; start_pos <= (int)input_line.length(); start_pos++) {
            // Need a fresh state for each attempt
            MatchState attempt_state;
            
            if (match_at_position_advanced(input_line, start_pos, actual_pattern, 0, true, attempt_state)) {
                return true;
            }
        }
        return false;
    } else {
        // Try matching at every position
        for (int i = 0; i < input_line.length(); i++) {
            // Need a fresh state for each attempt
            MatchState attempt_state;
            
            if (match_at_position_advanced(input_line, i, actual_pattern, 0, false, attempt_state)) {
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
