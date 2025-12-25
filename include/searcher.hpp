#pragma once

#include <string>
#include <vector>
#include <set>
#include <memory>
#include <cstdint>

#include "index_format.hpp"
#include "query_parser.hpp"
#include "tokenizer.hpp"

namespace search {

struct SearchResult {
    uint32_t doc_id;
    std::string title;
    std::string url;
};

struct SearchResponse {
    std::string query;
    std::vector<SearchResult> results;
    size_t total_count;
    double query_time_ms;
};

class Searcher {
public:
    explicit Searcher(const std::string& index_path);
    ~Searcher();
    
    bool open();
    void close();
    
    SearchResponse search(const std::string& query, size_t limit = 50, size_t offset = 0);
    
    size_t num_documents() const;
    size_t num_terms() const;
    
private:
    std::unique_ptr<IndexReader> reader_;
    Tokenizer tokenizer_;
    QueryParser parser_;
    
    std::set<uint32_t> all_doc_ids_;
    bool all_docs_loaded_ = false;
    
    std::set<uint32_t> evaluate(const QueryNode* node);
    const std::set<uint32_t>& get_all_doc_ids();
};

}