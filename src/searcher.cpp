#include "searcher.hpp"
#include <chrono>
#include <algorithm>

namespace search {

Searcher::Searcher(const std::string& index_path) {
    reader_ = std::make_unique<IndexReader>(index_path);
    
    Tokenizer::Config tok_config;
    tok_config.min_length = 2;
    tok_config.lowercase = true;
    tok_config.remove_stopwords = false;
    tokenizer_ = Tokenizer(tok_config);
}

Searcher::~Searcher() {
    close();
}

bool Searcher::open() {
    return reader_->open();
}

void Searcher::close() {
    if (reader_) {
        reader_->close();
    }
}

size_t Searcher::num_documents() const {
    return reader_->header().num_documents;
}

size_t Searcher::num_terms() const {
    return reader_->header().num_terms;
}

const std::set<uint32_t>& Searcher::get_all_doc_ids() {
    if (!all_docs_loaded_) {
        auto ids = reader_->get_all_doc_ids();
        all_doc_ids_.insert(ids.begin(), ids.end());
        all_docs_loaded_ = true;
    }
    return all_doc_ids_;
}

std::set<uint32_t> Searcher::evaluate(const QueryNode* node) {
    if (!node) return {};
    
    switch (node->type) {
        case NodeType::TERM: {
            auto* term_node = static_cast<const TermNode*>(node);
            std::string normalized = tokenizer_.normalize(term_node->term);
            if (normalized.empty()) {
                return {};
            }
            
            auto posting_list = reader_->get_posting_list(normalized);
            return std::set<uint32_t>(posting_list.begin(), posting_list.end());
        }
        
        case NodeType::NOT: {
            auto* not_node = static_cast<const NotNode*>(node);
            auto operand_result = evaluate(not_node->operand.get());
            
            std::set<uint32_t> result;
            const auto& all_docs = get_all_doc_ids();
            
            std::set_difference(
                all_docs.begin(), all_docs.end(),
                operand_result.begin(), operand_result.end(),
                std::inserter(result, result.begin())
            );
            
            return result;
        }
        
        case NodeType::AND: {
            auto* and_node = static_cast<const AndNode*>(node);
            
            if (and_node->operands.empty()) return {};
            
            auto result = evaluate(and_node->operands[0].get());
            
            for (size_t i = 1; i < and_node->operands.size(); ++i) {
                auto right = evaluate(and_node->operands[i].get());
                
                std::set<uint32_t> intersection;
                std::set_intersection(
                    result.begin(), result.end(),
                    right.begin(), right.end(),
                    std::inserter(intersection, intersection.begin())
                );
                
                result = std::move(intersection);
                
                if (result.empty()) break;
            }
            
            return result;
        }
        
        case NodeType::OR: {
            auto* or_node = static_cast<const OrNode*>(node);
            
            std::set<uint32_t> result;
            
            for (const auto& operand : or_node->operands) {
                auto operand_result = evaluate(operand.get());
                result.insert(operand_result.begin(), operand_result.end());
            }
            
            return result;
        }
    }
    
    return {};
}

SearchResponse Searcher::search(const std::string& query, size_t limit, size_t offset) {
    SearchResponse response;
    response.query = query;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    auto ast = parser_.parse(query);
    
    if (!ast) {
        response.total_count = 0;
        response.query_time_ms = 0;
        return response;
    }
    
    auto doc_ids = evaluate(ast.get());
    
    response.total_count = doc_ids.size();
    
    std::vector<uint32_t> sorted_ids(doc_ids.begin(), doc_ids.end());
    
    size_t start_idx = std::min(offset, sorted_ids.size());
    size_t end_idx = std::min(offset + limit, sorted_ids.size());
    
    for (size_t i = start_idx; i < end_idx; ++i) {
        uint32_t doc_id = sorted_ids[i];
        auto doc_info = reader_->get_document(doc_id);
        
        SearchResult result;
        result.doc_id = doc_id;
        result.title = doc_info.title;
        result.url = doc_info.url;
        
        response.results.push_back(result);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    response.query_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    return response;
}

}