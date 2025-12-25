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

std::string QueryParser::read_term() {
    skip_whitespace();
    
    std::string term;
    
    while (pos_ < query_.size()) {
        unsigned char c = query_[pos_];
        
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            term += std::tolower(c);
            ++pos_;
        }
        else if ((c == 0xD0 || c == 0xD1) && pos_ + 1 < query_.size()) {
            term += c;
            term += query_[++pos_];
            ++pos_;
        }
        else {
            break;
        }
    }
    
    std::string lower;
    for (size_t i = 0; i < term.size(); ++i) {
        unsigned char c = term[i];
        if (c < 128) {
            lower += std::tolower(c);
        } else if ((c == 0xD0) && i + 1 < term.size()) {
            unsigned char c2 = term[i + 1];
            if (c2 >= 0x90 && c2 <= 0xAF) {
                lower += c;
                lower += static_cast<char>(c2 + 0x20);
            }
            else if (c2 == 0x81) {
                lower += static_cast<char>(0xD1);
                lower += static_cast<char>(0x91);
            }
            else {
                lower += c;
                lower += c2;
            }
            ++i;
        } else {
            lower += c;
        }
    }
    
    return lower;
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