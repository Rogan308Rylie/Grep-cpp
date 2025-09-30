#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cctype>
#include <stdexcept>

using namespace std;

struct MatchState {
    map<int, string> captures;
};

struct PatternComponent {
    enum Type { LITERAL, DIGIT, WORD, POSITIVE_CLASS, NEGATIVE_CLASS, START_ANCHOR, END_ANCHOR, DOT, ALTERNATION, BACKREFERENCE };
    Type type;
    string value;
    vector<vector<PatternComponent>> alternatives;
    bool has_plus;
    bool has_question;
    int capture_group_id;
    int backref_id;
    
    PatternComponent(Type t, string v = "", bool plus = false, bool question = false, int cg_id = 0, int br_id = 0)
        : type(t), value(v), alternatives(), has_plus(plus), has_question(question),
          capture_group_id(cg_id), backref_id(br_id) {}
};

int match_component_sequence(const string& input, int pos, const vector<PatternComponent>& components, int comp_idx, MatchState& state);

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
                if (c == pattern_char) return true;
            }
            return false;
        case PatternComponent::NEGATIVE_CLASS:
            for (char pattern_char : component.value) {
                if (c == pattern_char) return false;
            }
            return true;
        case PatternComponent::DOT:
            return true;
        default:
            return false;
    }
}

int match_component_sequence(const string& input, int pos, const vector<PatternComponent>& components, int comp_idx, MatchState& state) {
    if (comp_idx >= components.size()) {
        return pos;
    }
    
    const PatternComponent& current = components[comp_idx];
    
    if (current.type == PatternComponent::BACKREFERENCE) {
        if (state.captures.count(current.backref_id) == 0) {
            return match_component_sequence(input, pos, components, comp_idx + 1, state);
        }
        
        const string& capture = state.captures[current.backref_id];
        
        if (pos + capture.length() > input.length()) {
            return -1;
        }
        
        if (input.substr(pos, capture.length()) != capture) {
            return -1;
        }
        
        return match_component_sequence(input, pos + capture.length(), components, comp_idx + 1, state);
    }
    
    if (current.type == PatternComponent::ALTERNATION) {
        for (const auto& alternative : current.alternatives) {
            int start_pos = pos;
            MatchState temp_state = state;
            
            int end_alt_match = match_component_sequence(input, pos, alternative, 0, temp_state);
            
            if (end_alt_match != -1) {
                if (current.capture_group_id > 0) {
                    temp_state.captures[current.capture_group_id] = input.substr(start_pos, end_alt_match - start_pos);
                }
                
                int result = match_component_sequence(input, end_alt_match, components, comp_idx + 1, temp_state);
                if (result != -1) {
                    state = temp_state;
                    return result;
                }
            }
        }
        return -1;
    }
    
    if (pos >= input.length()) {
        return -1;
    }
    
    if (current.has_plus) {
        if (!matches_component(input[pos], current)) {
            return -1;
        }
        
        int max_match_len = 0;
        while (pos + max_match_len < input.length() && matches_component(input[pos + max_match_len], current)) {
            max_match_len++;
        }
        
        for (int len = max_match_len; len >= 1; len--) {
            MatchState temp_state = state;
            int result = match_component_sequence(input, pos + len, components, comp_idx + 1, temp_state);
            if (result != -1) {
                state = temp_state;
                return result;
            }
        }
        return -1;
        
    } else if (current.has_question) {
        MatchState temp_state_skip = state;
        int result_skip = match_component_sequence(input, pos, components, comp_idx + 1, temp_state_skip);
        if (result_skip != -1) {
            state = temp_state_skip;
            return result_skip;
        }
        
        if (matches_component(input[pos], current)) {
            MatchState temp_state_match = state;
            int result_match = match_component_sequence(input, pos + 1, components, comp_idx + 1, temp_state_match);
            if (result_match != -1) {
                state = temp_state_match;
                return result_match;
            }
        }
        return -1;
        
    } else {
        if (matches_component(input[pos], current)) {
            return match_component_sequence(input, pos + 1, components, comp_idx + 1, state);
        }
        return -1;
    }
}

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
            int depth = 1;
            int start = i + 1;
            i++;
            
            while (i < pattern.length() && depth > 0) {
                if (pattern[i] == '(') depth++;
                else if (pattern[i] == ')') depth--;
                i++;
            }
            
            if (depth == 0) {
                string group_content = pattern.substr(start, i - start - 1);
                
                vector<string> alt_strings;
                string current_alt = "";
                int paren_depth = 0;
                
                for (char c : group_content) {
                    if (c == '(') paren_depth++;
                    else if (c == ')') paren_depth--;
                    else if (c == '|' && paren_depth == 0) {
                        alt_strings.push_back(current_alt);
                        current_alt = "";
                        continue;
                    }
                    current_alt += c;
                }
                alt_strings.push_back(current_alt);
                
                group_counter++;
                int this_group_id = group_counter;
                
                PatternComponent alt_component(PatternComponent::ALTERNATION);
                alt_component.capture_group_id = this_group_id;
                
                for (const string& alt : alt_strings) {
                    vector<PatternComponent> alt_pattern = parse_pattern_segment(alt, group_counter);
                    alt_component.alternatives.push_back(alt_pattern);
                }
                
                component = alt_component;
                component_created = true;
                i--;
            }
            
        } else if (pattern[i] == '\\' && i + 1 < pattern.length()) {
            char next = pattern[i + 1];
            
            if (next == 'd') {
                component = PatternComponent(PatternComponent::DIGIT);
                i++;
                component_created = true;
                
            } else if (next == 'w') {
                component = PatternComponent(PatternComponent::WORD);
                i++;
                component_created = true;
                
            } else if (isdigit(next) && (next - '0') >= 1) {
                int backref_id = next - '0';
                component = PatternComponent(PatternComponent::BACKREFERENCE, "", false, false, 0, backref_id);
                i++;
                component_created = true;
                
            } else {
                component = PatternComponent(PatternComponent::LITERAL, string(1, next));
                i++;
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
                
                i = end;
                component_created = true;
            }
            
        } else {
            component = PatternComponent(PatternComponent::LITERAL, string(1, pattern[i]));
            component_created = true;
        }
        
        if (component_created && i + 1 < pattern.length()) {
            if (pattern[i + 1] == '+') {
                component.has_plus = true;
                i++;
            } else if (pattern[i + 1] == '?') {
                component.has_question = true;
                i++;
            }
        }
        
        if (component_created) {
            components.push_back(component);
        }
    }
    
    return components;
}

vector<PatternComponent> parse_pattern(const string& pattern) {
    int group_counter = 0;
    return parse_pattern_segment(pattern, group_counter);
}

bool match_pattern(const string& input, const string& pattern) {
    vector<PatternComponent> components = parse_pattern(pattern);
    
    bool starts_with_anchor = !components.empty() && components.front().type == PatternComponent::START_ANCHOR;
    bool ends_with_anchor = !components.empty() && components.back().type == PatternComponent::END_ANCHOR;
    
    vector<PatternComponent> effective_components;
    
    int start_idx = starts_with_anchor ? 1 : 0;
    int end_idx = ends_with_anchor ? components.size() - 1 : components.size();
    
    if (start_idx < end_idx) {
        effective_components.insert(effective_components.end(), components.begin() + start_idx, components.begin() + end_idx);
    }
    
    if (effective_components.empty()) {
        if (starts_with_anchor && ends_with_anchor) {
            return input.empty();
        }
        return false;
    }
    
    int start_search_pos = 0;
    int end_search_pos = input.length();
    
    if (starts_with_anchor) {
        end_search_pos = 0;
    }
    
    for (int i = start_search_pos; i <= end_search_pos; ++i) {
        MatchState state;
        
        int end_match_pos = match_component_sequence(input, i, effective_components, 0, state);
        
        if (end_match_pos != -1) {
            if (!ends_with_anchor || end_match_pos == input.length()) {
                return true;
            }
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
