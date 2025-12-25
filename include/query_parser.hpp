#pragma once

#include <string>
#include <vector>
#include <memory>

namespace search {

enum class NodeType {
    TERM,
    AND,
    OR,
    NOT
};

struct QueryNode {
    NodeType type;
    explicit QueryNode(NodeType t) : type(t) {}
    virtual ~QueryNode() = default;
    virtual std::string to_string() const = 0;
};

struct TermNode : QueryNode {
    std::string term;
    explicit TermNode(const std::string& t) : QueryNode(NodeType::TERM), term(t) {}
    std::string to_string() const override { return term; }
};

struct NotNode : QueryNode {
    std::unique_ptr<QueryNode> operand;
    explicit NotNode(std::unique_ptr<QueryNode> op) 
        : QueryNode(NodeType::NOT), operand(std::move(op)) {}
    std::string to_string() const override;
};

struct AndNode : QueryNode {
    std::vector<std::unique_ptr<QueryNode>> operands;
    AndNode() : QueryNode(NodeType::AND) {}
    std::string to_string() const override;
};

struct OrNode : QueryNode {
    std::vector<std::unique_ptr<QueryNode>> operands;
    OrNode() : QueryNode(NodeType::OR) {}
    std::string to_string() const override;
};

class QueryParser {
public:
    std::unique_ptr<QueryNode> parse(const std::string& query);
    
private:
    std::string query_;
    size_t pos_ = 0;
    
    char peek() const;
    char get();
    void skip_whitespace();
    bool match(const std::string& s);
    std::string read_term();
    
    std::unique_ptr<QueryNode> parse_or();
    std::unique_ptr<QueryNode> parse_and();
    std::unique_ptr<QueryNode> parse_not();
    std::unique_ptr<QueryNode> parse_primary();
};

}