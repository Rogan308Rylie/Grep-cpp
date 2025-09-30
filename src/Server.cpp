#include <ctype.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <span>
#include <vector>

std::vector<std::string> find_files_recursively(std::filesystem::path directory) {
    std::vector<std::string> filenames;
    if(!std::filesystem::is_directory(directory))
        return {directory};
    std::filesystem::recursive_directory_iterator iter(directory);
    for(const std::filesystem::directory_entry &entry : iter) {
        if(!std::filesystem::is_directory(entry)) {
            filenames.push_back(entry.path());
        }
    }
    return filenames;
}

struct State {
    std::shared_ptr<State> out, out1;
    int c = -1;
    std::vector<int> match_set;
    int lastlist = -1;
    std::vector<int> capture_ids;
};
int capture_id_counter = 0;

enum {
    Default = -1,
    Split = 256,
    MatchAny = 257,
    MatchWord = 258,
    MatchDigit = 259,
    MatchChoice = 260,
    MatchAntiChoice = 261,
    MatchStart = 262,
    MatchEnd = 263,

    Epsilon = 299,
    BackRefStart = 300,
    Matched = 1000,
};

struct Fragment { 
    std::shared_ptr<State> start; 
    std::vector<std::shared_ptr<State>*> out; 
};

std::shared_ptr<State> regex2nfa(std::string_view regex);
Fragment parse(std::string_view &regex, Fragment lhs, int min_prec);

void capture_fragment(std::shared_ptr<State> start) {
    while(start && start->c >= 0) {
        if(start->c == Split && start->out1)
            capture_fragment(start->out1);
        if(std::find(start->capture_ids.begin(), start->capture_ids.end(), capture_id_counter) != start->capture_ids.end())
            break;
        start->capture_ids.insert(start->capture_ids.begin(), capture_id_counter);
        start = start->out;
    }
}

Fragment parse_primary(std::string_view &regex) {
    std::shared_ptr<State> state = std::make_shared<State>();
    Fragment frag{state, {&state->out}};
    char c = regex.front();
    regex.remove_prefix(1);
    switch(c) {
        case '.':
            state->c = MatchAny;
            break;
        case '^':
            state->c = MatchStart;
            break;
        case '$':
            state->c = MatchEnd;
            break;
        case '\\':
            c = regex.front();
            regex.remove_prefix(1);
            switch(c) {
                case 'd':
                    state->c = MatchDigit;
                    break;
                case 'w':
                    state->c = MatchWord;
                    break;
                case '\\': case '(': case '[':
                case ')': case ']': case '*':
                case '+': case '?': case '.':
                case '^': case '$': case '|':
                    state->c = c;
                    break;
                case '1': case '2': case '3': 
                case '4': case '5': case '6': 
                case '7': case '8': case '9':
                    state->c = BackRefStart + c - '0';
                    break;
                default:
                    throw std::runtime_error(std::string("Unrecognized escaped character '")+c+'\'');
            }
            break;
        case '[': {
                bool neg = regex.front() == '^';
                if(neg) regex.remove_prefix(1);
                state->c = neg ? MatchAntiChoice : MatchChoice;
                while(regex.front() != ']') {
                    state->match_set.push_back(regex.front());
                    regex.remove_prefix(1);
                    if(!regex.size())
                        throw std::runtime_error("Expected closing ']'.");
                }
                regex.remove_prefix(1);
            }
            break;
        case '(':
            frag = parse_primary(regex);
            frag = parse(regex, frag, 0);
            if(regex.front() != ')')
                throw std::runtime_error("Expected ')' to close expression");
            regex.remove_prefix(1);
            capture_fragment(frag.start);
            capture_id_counter++;
            break;
        default:
            state->c = c;
            break;
    }
    if(regex.size()) {
        switch(regex.front()) {
            case '*': {
                    std::shared_ptr<State> s = std::make_shared<State>();
                    s->c = Split;
                    s->out = frag.start;
                    for(auto o : frag.out)
                        *o = s;
                    frag = Fragment{s, {&s->out1}};
                    regex.remove_prefix(1);
                }
                break;
            case '+': {
                    std::shared_ptr<State> s = std::make_shared<State>();
                    s->c = Split;
                    s->out = frag.start;
                    for(auto o : frag.out)
                        *o = s;
                    //frag = Fragment{frag.start, {&s->out1}};
                    frag.out = {&s->out1};
                    regex.remove_prefix(1);
                }
                break;
            case '?': {
                    std::shared_ptr<State> s = std::make_shared<State>();
                    s->c = Split;
                    s->out = frag.start;
                    frag.start = s;
                    frag.out.push_back(&s->out1);
                    regex.remove_prefix(1);
                }
                break;
        }
    }
    return frag;
}

Fragment parse(std::string_view &regex, Fragment lhs, int min_prec) {
    auto prec = [](char c) { return c == '|' ? 0 : c == ']' || c == ')' ? -1 : 1; };
    char lookahead = regex.front();
    while(regex.size() && prec(lookahead) >= min_prec) {
        char op = lookahead;
        if(op == '|') regex.remove_prefix(1);
        Fragment rhs = parse_primary(regex);
        if(regex.size()) lookahead = regex.front();
        //std::cout << "Regex size " << regex.size() << std::endl;
        while(prec(lookahead) > prec(op)) {
            rhs = parse(regex, rhs, prec(op) + 1);
            if(!regex.size()) break;
            lookahead = regex.front();
        }
        if(op == '|') {
            std::shared_ptr<State> state = std::make_shared<State>();
            state->c = Split;
            state->out = lhs.start;
            state->out1 = rhs.start;
            lhs.start = state;
            lhs.out.insert(lhs.out.end(), rhs.out.begin(), rhs.out.end());
        } else {
            for(auto o : lhs.out)
                *o = rhs.start;
            lhs.out = rhs.out;
        }
        if(!regex.size()) break;
    }
    return lhs;
}

std::shared_ptr<State> regex2nfa(std::string_view regex) {
    std::shared_ptr<State> matched = std::make_shared<State>();
    matched->c = Matched;
    if(regex.size() == 0) {
        return matched;
    }
    Fragment frag = parse_primary(regex);
    Fragment nfa = parse(regex, frag, 0);
    for(auto o : nfa.out)
        *o = matched;
    return nfa.start;
}

int listid = 0;

struct CaptureInfo {
    std::vector<std::string> capture_groups;
    std::map<int, int> active_groups;
    int capture_index;
};
using List = std::vector<std::pair<CaptureInfo, std::shared_ptr<State>>>;

int ismatch(const List &list) {
    return std::any_of(list.begin(), list.end(), [](auto s) {
        return s.second->c == Matched;
    });
}

void addstate(std::shared_ptr<State> s, CaptureInfo &cap, List &l) {
    if(s->lastlist == listid) return;

    s->lastlist = listid;
    if(s->c == Split) {
        addstate(s->out, cap, l);
        addstate(s->out1, cap, l);
        return;
    }
    l.push_back({cap, s});
}

void startlist(std::shared_ptr<State> s, List &l) {
    listid++;
    CaptureInfo cap{};
    addstate(s, cap, l);
}

void capture(char c, CaptureInfo &caps, std::shared_ptr<State> s) {
    for(int id : s->capture_ids) {
        if(caps.active_groups.find(id) == caps.active_groups.end()) {
            caps.active_groups[id] = caps.capture_groups.size();
            caps.capture_groups.emplace_back();
        }
        caps.capture_groups[caps.active_groups.at(id)] += c;
    } 
}


void match_step(List &clist, char c, List &nlist) {
    listid++;
    nlist.clear();
    for(auto [cap, s] : clist) {
        switch(s->c) {
            case MatchAny:
                capture(c, cap, s);
                addstate(s->out, cap, nlist);
                break;
            case MatchDigit:
                if(isdigit(c)) {
                    capture(c, cap, s);
                    addstate(s->out, cap, nlist);
                }
                break;
            case MatchWord:
                if(isalnum(c) || c == '_') {
                    capture(c, cap, s);
                    addstate(s->out, cap, nlist);
                }
                break;
            case MatchChoice:
                if(std::find(s->match_set.begin(), s->match_set.end(), c) != s->match_set.end()) {
                    capture(c, cap, s);
                    addstate(s->out, cap, nlist);
                }
                break;
            case MatchAntiChoice:
                if(std::find(s->match_set.begin(), s->match_set.end(), c) == s->match_set.end()) {
                    capture(c, cap, s);
                    addstate(s->out, cap, nlist);
                }
                break;
            default:
                if(s->c == c) {
                    capture(c, cap, s);
                    addstate(s->out, cap, nlist);
                } else if(s->c > BackRefStart && cap.capture_groups[s->c - BackRefStart - 1][cap.capture_index] == c) {
                    cap.capture_index++;
                    capture(c, cap, s);
                    if(cap.capture_index >= (int)cap.capture_groups[s->c - BackRefStart - 1].size()) {
                        cap.capture_index = 0;
                        addstate(s->out, cap, nlist);
                    } else {
                        addstate(s, cap, nlist);
                    }
                }
                break;
        }
    }
}

int matchEpsilonNFA(std::shared_ptr<State> start, std::string_view text) {
    List clist, nlist;
    startlist(start, clist);
    bool started = false;
    if(start->c == MatchStart) {
        listid++;
        nlist.clear();
        CaptureInfo cap{};
        addstate(start->out, cap, nlist);
        std::swap(clist, nlist);
        started = true;
    }
restart:
    for(char c : text) {
        match_step(clist, c, nlist);
        std::swap(clist, nlist);
        if(!started && clist.empty()) {
            startlist(start, clist);
            text.remove_prefix(1);
            goto restart;
        }
        if(ismatch(clist)) return 1;
    }
    for(auto [cap, s] : clist) {
        if(s->c == MatchEnd) {
            listid++;
            nlist.clear();
            addstate(s->out, cap, nlist);
            std::swap(clist, nlist);
        }
    }
    return ismatch(clist);
}

int match_recursive(std::string_view input, std::string_view regex) {
    if(regex.size() == 0 || input.size() == 0) return 1;

    if(regex.front() == '$') return 0; // Already covered end above

    if(regex.front() == '[') {

    } else if(regex.front() == '(') {

    } else if(regex.front() == '\\') {

    } else if(regex.size() > 1 && regex[1] == '*') {

    } else if(regex.size() > 1 && regex[1] == '+') {

    } else if(regex.size() > 1 && regex[1] == '?') {

    }

    if(regex.front() != '.' && regex.front() != input.front()) return 0;
    input.remove_prefix(1);
    regex.remove_prefix(1);
    return match_recursive(input, regex);
}

int backtracking_matcher(std::string_view input, std::string_view regex) {
    if(regex.size() == 0) return 1;
    std::string start_regex = ".*";
    if(regex.front() == '^')
        regex.remove_prefix(1);
    else {
        start_regex += regex;
        regex = start_regex;
    }
    return match_recursive(input, regex);
}

int main(int argc, char* argv[]) {
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    std::cerr << "Logs from your program will appear here" << std::endl;

    bool use_stdin = argc == 3;

    if (argc  < 3) {
        std::cerr << "Expected at least 3 arguments" << std::endl;
        return 1;
    }

    bool foundE = false;
    bool recursive = false;

    std::string pattern;
    std::vector<std::string> filenames;

    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(arg[0] == '-') {
            if(arg.size() < 2 || (arg[1] != 'E' && arg[1] != 'r')) 
                throw std::runtime_error("Unrecognized flag.");
            if(arg[1] == 'E')
                foundE = true;
            else if(arg[1] == 'r')
                recursive = true;
        } else if(pattern == "") {
            pattern = arg;
        } else {
            filenames.push_back(arg);
        }
    }

    if(!foundE) {
        std::cerr << "Expected to find -E flag" << std::endl;
        return 1;
    }

    if(recursive) {
        std::vector<std::string> tmp = filenames;
        filenames.clear();
        for(const auto &dir : tmp) {
            std::vector<std::string> dirfiles = find_files_recursively(dir);
            filenames.insert(filenames.end(), dirfiles.begin(), dirfiles.end());
        }
    }

     std::string input_line;
     bool found_match = false;
     if(filenames.empty()) {
        while(std::getline(std::cin, input_line)) {
            try {
                std::shared_ptr<State> start = regex2nfa(pattern);
                if(matchEpsilonNFA(start, input_line)) {
                    std::cout << input_line << std::endl;
                    found_match = true;
                }
            } catch (const std::runtime_error& e) {
                std::cerr << e.what() << std::endl;
                return 1;
            }
        }
     } else {
        for(const auto &name : filenames) {
            std::ifstream f(name);
            while(std::getline(f, input_line)) {
                try {
                    std::shared_ptr<State> start = regex2nfa(pattern);
                    if(matchEpsilonNFA(start, input_line)) {
                        if(filenames.size() > 1)
                            std::cout << name << ":";
                        std::cout << input_line << std::endl;
                        found_match = true;
                    }
                } catch (const std::runtime_error& e) {
                    std::cerr << e.what() << std::endl;
                    return 1;
                }
            }
        }
     }
     return !found_match;
}