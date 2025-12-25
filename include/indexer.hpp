#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

#include "index_format.hpp"
#include "tokenizer.hpp"

namespace search {

struct IndexStats {
    size_t total_documents = 0;
    size_t total_tokens = 0;
    size_t unique_terms = 0;
    size_t total_postings = 0;
    size_t total_text_bytes = 0;
    double indexing_time_sec = 0.0;
    
    double docs_per_second() const;
    double kb_per_second() const;
    double avg_term_length(const std::unordered_map<std::string, std::vector<uint32_t>>& index) const;
};

class Indexer {
public:
    struct Config {
        std::string mongo_host = "localhost";
        int mongo_port = 27017;
        std::string mongo_db;
        std::string mongo_collection;
    };
    
    explicit Indexer(const Config& config);
    
    void build(const std::string& output_path, size_t limit = 0);
    const IndexStats& stats() const { return stats_; }
    
private:
    Config config_;
    Tokenizer tokenizer_;
    IndexStats stats_;
    
    std::vector<DocumentInfo> documents_;
    std::unordered_map<std::string, std::vector<uint32_t>> inverted_index_;
};

}