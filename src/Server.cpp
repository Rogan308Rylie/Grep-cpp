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
        // Handle positive character groups like [abc]
        string chars_to_match = pattern.substr(1, pattern.length()-2); // Extract characters between [ and ]
        
        for (char input_char : input_line) {
            for (char pattern_char : chars_to_match) {
                if (input_char == pattern_char) {
                    return true;
                }
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