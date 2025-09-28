#include <iostream>
#include <string>
#include <cctype>

using namespace std;

bool match_pattern(const string& input_line, const string& pattern) {
    if (pattern.length() == 1) {
        return input_line.find(pattern) != std::string::npos;
    }
    else if (pattern == "\\d") {
        // Check if input contains any digit
        for (char c : input_line) {
            if (isdigit(c)) {
                return true;
            }
        }
        return false;
    }
    else if (pattern == "\\w") {
        // Check if input contains any word character (alphanumeric or underscore)
        for (char c : input_line) {
            if (isalnum(c) || c == '_') {
                return true;
            }
        }
        return false;
    }
    else if (pattern.length() > 2 && pattern[0] == '[' && pattern[pattern.length()-1] == ']') {
        // Handle character groups (both positive and negative)
        string chars_in_brackets = pattern.substr(1, pattern.length()-2); // Extract characters between [ and ]
        
        bool is_negative = false;
        string chars_to_check = chars_in_brackets;
        
        // Check if it's a negative character group (starts with ^)
        if (!chars_in_brackets.empty() && chars_in_brackets[0] == '^') {
            is_negative = true;
            chars_to_check = chars_in_brackets.substr(1); // Remove the ^ symbol
        }
        
        for (char input_char : input_line) {
            bool char_found_in_set = false;
            
            // Check if current input character is in the character set
            for (char pattern_char : chars_to_check) {
                if (input_char == pattern_char) {
                    char_found_in_set = true;
                    break;
                }
            }
            
            // For negative groups: return true if we find a char NOT in the set
            // For positive groups: return true if we find a char IN the set
            if (is_negative && !char_found_in_set) {
                return true;
            } else if (!is_negative && char_found_in_set) {
                return true;
            }
        }
        return false;
    }
    else {
        throw runtime_error("Unhandled pattern " + pattern);
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

    // Uncomment this block to pass the first stage
    
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