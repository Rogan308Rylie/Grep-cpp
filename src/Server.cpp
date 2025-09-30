#include <iostream>
#include <string>
#include <cctype>
#include <vector>
#include <map>
#include <stdexcept>
using namespace std;

// --- Match State for Multiple Captures ---
struct MatchState {
    // Key: Group number (1, 2, 3...), Value: captured string
    map<int, string> captures; 
};
// -----------------------------------------

// Represents a single pattern component
struct PatternComponent {
    enum Type { LITERAL, DIGIT, WORD, POSITIVE_CLASS, NEGATIVE_CLASS, START_ANCHOR, END_ANCHOR, DOT, ALTERNATION, BACKREFERENCE };
    Type type;
    string value; // For literals and character classes
    vector<vector<PatternComponent>> alternatives; // For alternation groups
    bool has_plus; // Whether this component has a + quantifier
    bool has_question; // Whether this component has a ? quantifier
    
    // Group IDs for multiple backreferences
    int capture_group_id = 0; // 0 for non-capturing, 1 for group 1, 2 for group 2, etc.
    int backref_id = 0;       // 0 for not a backref, 1 for \1, 2 for \2, etc.

    // Updated constructor
    PatternComponent(Type t, string v = "", bool plus = false, bool question = false, int cg_id = 0, int br_id = 0) 
        : type(t), value(v), alternatives(), has_plus(plus), has_question(question), 
          capture_group_id(cg_id), backref_id(br_id) {}
};

// Forward declarations
bool match_at_position_advanced(const string& input, int pos, const vector<struct PatternComponent>& components, int comp_idx, bool must_reach_end, MatchState& state);
bool match_pattern(const string& input, const string& pattern); // Forward declaration added
// NOTE: match_component_sequence now takes a mutable MatchState& to allow nested groups to capture.
int match_component_sequence(const string& input, int pos, const vector<PatternComponent>& components, int comp_idx, MatchState& state);

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
            return true; 
            
        case PatternComponent::ALTERNATION:
        case PatternComponent::BACKREFERENCE:
            return false; // Handled in sequence matchers
    }
    return false;
}

// Helper function to match a sequence of components and return the new position.
// Returns the index in the input after the successful match, or -1 on failure.
// NOTE: This helper is used to find the match position for a sub-sequence, and it
// updates the MatchState for any nested groups (e.g., G2 inside G1) found.
int match_component_sequence(const string& input, int pos, const vector<PatternComponent>& components, int comp_idx, MatchState& state) {
    if (comp_idx >= components.size()) {
        return pos; // Finished matching the sequence
    }
    
    // If we run out of input, check if remaining required components can match
    if (pos >= input.length()) {
        for (int i = comp_idx; i < components.size(); ++i) {
            const PatternComponent& remaining = components[i];
            
            // Required component if non-question and not an empty backref
            if (!remaining.has_question) {
                if (remaining.type != PatternComponent::BACKREFERENCE || state.captures.count(remaining.backref_id) == 0 || state.captures.at(remaining.backref_id).empty()) {
                    return -1; 
                }
            }
        }
        return pos;
    }
    
    const PatternComponent& current = components[comp_idx];
    
    if (current.type == PatternComponent::BACKREFERENCE) {
        // Retrieve capture. If not found, it's considered an empty match.
        const string capture = (state.captures.count(current.backref_id)) ? state.captures.at(current.backref_id) : "";
        
        if (capture.empty()) {
            // Empty capture matches zero length, continue
            return match_component_sequence(input, pos, components, comp_idx + 1, state);
        }
        
        if (pos + capture.length() > input.length() || input.substr(pos, capture.length()) != capture) {
            return -1;
        }
        return match_component_sequence(input, pos + capture.length(), components, comp_idx + 1, state);
    } 
    
    if (current.type == PatternComponent::ALTERNATION) {
        // This is a nested capturing group. It must calculate its match span,
        // capture its content, and update the state before continuing.
        
        for (const auto& alternative : current.alternatives) {
            int start_pos = pos;
            
            // 1. Create a temporary state for the alternative attempt (for backtracking).
            MatchState temp_state = state; 

            // 2. Recursively match the alternative sequence. This updates temp_state 
            // with captures for any *deeper* nested groups.
            int end_alt_match = match_component_sequence(input, pos, alternative, 0, temp_state);
            
            if (end_alt_match != -1) {
                
                // 3. The alternative matched. Capture the content for the *current* group.
                if (current.capture_group_id > 0) {
                    temp_state.captures[current.capture_group_id] = input.substr(start_pos, end_alt_match - start_pos);
                }
                
                // 4. Continue matching the rest of the main sequence (outside this group).
                int result = match_component_sequence(input, end_alt_match, components, comp_idx + 1, temp_state);
                
                if (result != -1) {
                    // Success! Commit the captured state and return.
                    state = temp_state; 
                    return result; 
                }
            }
        }
        return -1; // All alternatives failed
    }

    // --- Quantifier Logic (applies to simple character components) ---
    
    if (current.has_plus) {
        // Must match at least once.
        if (pos >= input.length() || !matches_component(input[pos], current)) {
            return -1;
        }
        
        // 1. Find the maximum possible match length (greedy).
        int max_match_len = 0;
        while (pos + max_match_len < input.length() && matches_component(input[pos + max_match_len], current)) {
            max_match_len++;
        }
        
        // 2. Backtrack loop: Try matching the rest of the pattern from longest match down to 1.
        for (int len = max_match_len; len >= 1; len--) {
            // Create a temporary state for the recursive call to allow proper backtracking.
            MatchState temp_state = state; 
            
            // Match the rest of the sequence (comp_idx + 1) starting after the match of length 'len'.
            int result = match_component_sequence(input, pos + len, components, comp_idx + 1, temp_state);
            if (result != -1) {
                state = temp_state; // Commit state on success
                return result; // Success!
            }
        }
        return -1; // No repetition count worked
        
    } else if (current.has_question) {
        // Try matching zero times first (skip this component)
        MatchState temp_state_skip = state;
        int result_skip = match_component_sequence(input, pos, components, comp_idx + 1, temp_state_skip);
        if (result_skip != -1) {
            state = temp_state_skip; // Commit state on success
            return result_skip;
        }
        
        // Try matching one time
        if (matches_component(input[pos], current)) {
             MatchState temp_state_match = state;
            int result_match = match_component_sequence(input, pos + 1, components, comp_idx + 1, temp_state_match);
            if (result_match != -1) {
                state = temp_state_match; // Commit state on success
                return result_match;
            }
        }
        
        return -1;
    } else {
        // Normal single character match
        if (matches_component(input[pos], current)) {
            return match_component_sequence(input, pos + 1, components, comp_idx + 1, state);
        }
        return -1;
    }
}

// Function to parse a regex pattern segment.
// It takes `group_counter` by reference to maintain sequential group numbering across recursion.
vector<PatternComponent> parse_pattern_segment(const string& pattern, int& group_counter) {
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
                
                // Parse each alternative recursively, updating group_counter
                PatternComponent alt_component(PatternComponent::ALTERNATION);
                for (const string& alt : alt_strings) {
                    // *** RECURSIVE CALL HERE ***
                    vector<PatternComponent> alt_pattern = parse_pattern_segment(alt, group_counter);
                    alt_component.alternatives.push_back(alt_pattern);
                }
                
                // --- Capture Group Logic ---
                group_counter++;
                alt_component.capture_group_id = group_counter;
                // ---------------------------
                
                component = alt_component;
                component_created = true;
                i--; // Adjust since we'll increment at end of loop
            } else {
                // Unmatched paren, treat as literal (fallback)
                component = PatternComponent(PatternComponent::LITERAL, string(1, pattern[i]));
                component_created = true;
            }
        } else if (pattern[i] == '\\' && i + 1 < pattern.length()) {
            char next = pattern[i + 1];
            if (next == 'd') {
                component = PatternComponent(PatternComponent::DIGIT);
                i++; // Skip 'd'
                component_created = true;
            } else if (next == 'w') {
                component = PatternComponent(PatternComponent::WORD);
                i++; // Skip 'w'
                component_created = true;
            } else if (isdigit(next) && (next - '0') >= 1) { // Handle \1 to \9 Backreferences
                int backref_id = next - '0';
                component = PatternComponent(PatternComponent::BACKREFERENCE, "", false, false, 0, backref_id);
                i++; // Skip the digit
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

// Main pattern parsing function (wrapper for recursive calls)
vector<PatternComponent> parse_pattern(const string& pattern) {
    int group_counter = 0; // Initialize global group counter
    return parse_pattern_segment(pattern, group_counter);
}


// Advanced pattern matching with quantifier and backreference support
bool match_at_position_advanced(const string& input, int pos, const vector<PatternComponent>& components, int comp_idx, bool must_reach_end, MatchState& state) {
    if (comp_idx >= components.size()) {
        return !must_reach_end || pos == input.length(); 
    }
    
    if (pos >= input.length()) {
        // Special case: if we ran out of input, check if remaining components are all optional
        for (int remaining_idx = comp_idx; remaining_idx < components.size(); remaining_idx++) {
            const PatternComponent& remaining = components[remaining_idx];
            
            // Required component if non-question and not an empty backref
            if (!remaining.has_question) {
                if (remaining.type != PatternComponent::BACKREFERENCE || state.captures.count(remaining.backref_id) == 0 || state.captures.at(remaining.backref_id).empty()) {
                    return false; 
                }
            }
        }
        return true; 
    }
    
    const PatternComponent& current = components[comp_idx];
    
    if (current.type == PatternComponent::BACKREFERENCE) {
        // Retrieve capture. If not found, it's considered an empty match.
        const string capture = (state.captures.count(current.backref_id)) ? state.captures.at(current.backref_id) : "";
        
        if (capture.empty()) {
            // Empty capture matches zero length, continue
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
            // This call now updates temp_state with any nested captures (like G2).
            int end_of_group_match = match_component_sequence(input, pos, alternative, 0, temp_state); 
            
            if (end_of_group_match != -1) {
                // The alternative matched up to `end_of_group_match`.
                
                // --- Capture Logic: Store the matched content in the state (for the top-level group, G1) ---
                if (current.capture_group_id > 0) {
                    temp_state.captures[current.capture_group_id] = input.substr(pos, end_of_group_match - pos);
                }
                // -----------------------------------------------------------

                // 3. Now try to match the rest of the main pattern starting from the new position.
                // We pass `temp_state` and update the original `state` on success.
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
        // Must match at least once.
        if (pos >= input.length() || !matches_component(input[pos], current)) {
            return false;
        }
        
        // 1. Find the maximum possible match length (greedy).
        int max_match_len = 0;
        while (pos + max_match_len < input.length() && matches_component(input[pos + max_match_len], current)) {
            max_match_len++;
        }
        
        // 2. Backtrack loop: Try matching the rest of the pattern from longest match down to 1.
        for (int len = max_match_len; len >= 1; len--) {
            // Create a temporary state for the recursive call to maintain consistency
            MatchState temp_state = state; 

            if (match_at_position_advanced(input, pos + len, components, comp_idx + 1, must_reach_end, temp_state)) {
                // If the rest of the pattern matches, commit the state and return true.
                state = temp_state;
                return true;
            }
        }
        
        return false; // No repetition count worked

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
        // Normal single character match (no quantifier, no backreference, no group)
        if (matches_component(input[pos], current)) {
            return match_at_position_advanced(input, pos + 1, components, comp_idx + 1, must_reach_end, state);
        }
        return false;
    }
}

// High-level pattern matching function to be called from main
bool match_pattern(const string& input, const string& pattern) {
    // 1. Parse the entire pattern
    vector<PatternComponent> components = parse_pattern(pattern);

    // 2. Extract anchor flags and non-anchor components
    bool starts_with_anchor = !components.empty() && components.front().type == PatternComponent::START_ANCHOR;
    bool ends_with_anchor = !components.empty() && components.back().type == PatternComponent::END_ANCHOR;

    vector<PatternComponent> effective_components;
    
    // Find the range of components that are not anchors
    int start_idx = starts_with_anchor ? 1 : 0;
    int end_idx = ends_with_anchor ? components.size() - 1 : components.size();
    
    // Copy components excluding anchors
    if (start_idx < end_idx) {
        effective_components.insert(effective_components.end(), components.begin() + start_idx, components.begin() + end_idx);
    } 
    
    // Handle special case: pattern is exactly "^$" (matches only empty string)
    if (effective_components.empty()) {
        if (starts_with_anchor && ends_with_anchor) {
            return input.empty();
        }
        // For any other empty pattern (e.g., just `()`, `^`, or `$`), we return false.
        return false;
    }


    // Determine the starting position(s) for the match attempt
    int start_search_pos = 0;
    // Search up to the last position where the pattern can start (including the end position for zero-width matches)
    int end_search_pos = input.length(); 

    if (starts_with_anchor) {
        // If anchored to start, only check position 0
        start_search_pos = 0;
        end_search_pos = 0; // Only one attempt at pos 0
    }
    
    // Try matching at every possible start position
    for (int i = start_search_pos; i <= end_search_pos; ++i) {
        MatchState state;
        // The `must_reach_end` flag is set to true only if '$' was present.
        if (match_at_position_advanced(input, i, effective_components, 0, ends_with_anchor, state)) {
            return true;
        }
    }

    return false;
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
