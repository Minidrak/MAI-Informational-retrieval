#include "query_parser.hpp"
#include <sstream>
#include <cctype>

namespace search {

std::string NotNode::to_string() const {
    return "NOT(" + operand->to_string() + ")";
}

std::string AndNode::to_string() const {
    std::ostringstream ss;
    ss << "AND(";
    for (size_t i = 0; i < operands.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << operands[i]->to_string();
    }
    ss << ")";
    return ss.str();
}

std::string OrNode::to_string() const {
    std::ostringstream ss;
    ss << "OR(";
    for (size_t i = 0; i < operands.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << operands[i]->to_string();
    }
    ss << ")";
    return ss.str();
}

char QueryParser::peek() const {
    if (pos_ >= query_.size()) return '\0';
    return query_[pos_];
}

char QueryParser::get() {
    if (pos_ >= query_.size()) return '\0';
    return query_[pos_++];
}

void QueryParser::skip_whitespace() {
    while (pos_ < query_.size() && std::isspace(static_cast<unsigned char>(query_[pos_]))) {
        ++pos_;
    }
}

bool QueryParser::match(const std::string& s) {
    skip_whitespace();
    if (query_.compare(pos_, s.size(), s) == 0) {
        pos_ += s.size();
        return true;
    }
    return false;
}

static size_t get_utf8_char(const std::string& str, size_t pos, std::string& out_char) {
    if (pos >= str.size()) return 0;
    
    unsigned char c = static_cast<unsigned char>(str[pos]);
    
    if (c < 128) {
        out_char = str.substr(pos, 1);
        return 1;
    }
    
    if ((c & 0xE0) == 0xC0 && pos + 1 < str.size()) {
        out_char = str.substr(pos, 2);
        return 2;
    }
    
    out_char.clear();
    return 1;
}

static bool is_letter_or_digit(const std::string& ch) {
    if (ch.empty()) return false;
    
    if (ch.size() == 1) {
        unsigned char c = static_cast<unsigned char>(ch[0]);
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }
    
    if (ch.size() == 2) {
        unsigned char c1 = static_cast<unsigned char>(ch[0]);
        unsigned char c2 = static_cast<unsigned char>(ch[1]);
        
        if (c1 == 0xD0 && c2 >= 0x90 && c2 <= 0xAF) return true;
        if (c1 == 0xD0 && c2 >= 0xB0 && c2 <= 0xBF) return true;
        if (c1 == 0xD1 && c2 >= 0x80 && c2 <= 0x8F) return true;
        if (c1 == 0xD0 && c2 == 0x81) return true;
        if (c1 == 0xD1 && c2 == 0x91) return true;
    }
    
    return false;
}

std::string QueryParser::read_term() {
    skip_whitespace();
    
    std::string term;
    
    while (pos_ < query_.size()) {
        std::string ch;
        size_t bytes = get_utf8_char(query_, pos_, ch);
        
        if (bytes == 0) break;
        
        if (is_letter_or_digit(ch) || 
            (ch.size() == 1 && (ch[0] == '-' || ch[0] == '_'))) {
            term += ch;
            pos_ += bytes;
        } else {
            break;
        }
    }
    
    return term;
}

std::unique_ptr<QueryNode> QueryParser::parse(const std::string& query) {
    query_ = query;
    pos_ = 0;
    
    skip_whitespace();
    if (pos_ >= query_.size()) {
        return nullptr;
    }
    
    return parse_or();
}

std::unique_ptr<QueryNode> QueryParser::parse_or() {
    auto left = parse_and();
    if (!left) return nullptr;
    
    auto or_node = std::make_unique<OrNode>();
    or_node->operands.push_back(std::move(left));
    
    while (match("||")) {
        auto right = parse_and();
        if (right) {
            or_node->operands.push_back(std::move(right));
        }
    }
    
    if (or_node->operands.size() == 1) {
        return std::move(or_node->operands[0]);
    }
    
    return or_node;
}

std::unique_ptr<QueryNode> QueryParser::parse_and() {
    auto left = parse_not();
    if (!left) return nullptr;
    
    auto and_node = std::make_unique<AndNode>();
    and_node->operands.push_back(std::move(left));
    
    while (true) {
        if (match("&&")) {
            auto right = parse_not();
            if (right) {
                and_node->operands.push_back(std::move(right));
            }
        }
        else {
            skip_whitespace();
            char c = peek();
            
            if (c == '!' || c == '(' || std::isalnum(static_cast<unsigned char>(c)) ||
                (unsigned char)c >= 0x80) {
                
                if (query_.compare(pos_, 2, "||") != 0) {
                    auto right = parse_not();
                    if (right) {
                        and_node->operands.push_back(std::move(right));
                        continue;
                    }
                }
            }
            break;
        }
    }
    
    if (and_node->operands.size() == 1) {
        return std::move(and_node->operands[0]);
    }
    
    return and_node;
}

std::unique_ptr<QueryNode> QueryParser::parse_not() {
    skip_whitespace();
    
    if (peek() == '!') {
        ++pos_;
        auto operand = parse_not();
        if (operand) {
            return std::make_unique<NotNode>(std::move(operand));
        }
        return nullptr;
    }
    
    return parse_primary();
}

std::unique_ptr<QueryNode> QueryParser::parse_primary() {
    skip_whitespace();
    
    if (peek() == '(') {
        ++pos_;
        auto expr = parse_or();
        skip_whitespace();
        if (peek() == ')') {
            ++pos_;
        }
        return expr;
    }
    
    std::string term = read_term();
    if (!term.empty()) {
        return std::make_unique<TermNode>(term);
    }
    
    return nullptr;
}

}