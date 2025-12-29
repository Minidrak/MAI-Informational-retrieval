#include "indexer.hpp"
#include <iostream>
#include <chrono>
#include <set>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <bsoncxx/json.hpp>

namespace search {

double IndexStats::docs_per_second() const {
    if (indexing_time_sec <= 0) return 0;
    return total_documents / indexing_time_sec;
}

double IndexStats::kb_per_second() const {
    if (indexing_time_sec <= 0) return 0;
    return (total_text_bytes / 1024.0) / indexing_time_sec;
}

double IndexStats::avg_term_length(
    const std::unordered_map<std::string, std::vector<uint32_t>>& index) const {
    if (index.empty()) return 0;
    
    size_t total_len = 0;
    for (const auto& p : index) {
        total_len += p.first.size();
    }
    return static_cast<double>(total_len) / index.size();
}

Indexer::Indexer(const Config& config) : config_(config) {
    Tokenizer::Config tok_config;
    tok_config.min_length = 2;
    tok_config.lowercase = true;
    tok_config.remove_stopwords = false;
    tokenizer_ = Tokenizer(tok_config);
}

void Indexer::build(const std::string& output_path, size_t limit) {
    
    std::cout << "========================================\n";
    std::cout << "BUILDING BOOLEAN INDEX\n";
    std::cout << "========================================\n";
    
    mongocxx::instance instance{};
    
    std::string uri_str = "mongodb://" + config_.mongo_host + ":" + 
                          std::to_string(config_.mongo_port);
    mongocxx::uri uri(uri_str);
    mongocxx::client client(uri);
    
    auto db = client[config_.mongo_db];
    auto collection = db[config_.mongo_collection];
    
    size_t total_docs = collection.count_documents({});
    if (limit > 0 && limit < total_docs) {
        total_docs = limit;
    }
    
    std::cout << "\nDocuments to index: " << total_docs << "\n";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    mongocxx::options::find opts;
    opts.projection(bsoncxx::builder::basic::make_document(
        bsoncxx::builder::basic::kvp("url", 1),
        bsoncxx::builder::basic::kvp("html_content", 1)
    ));
    
    if (limit > 0) {
        opts.limit(static_cast<int64_t>(limit));
    }
    
    auto cursor = collection.find({}, opts);
    
    uint32_t doc_id = 0;
    
    for (auto&& doc : cursor) {
        std::string url;
        std::string html;
        
        if (doc["url"]) {
            auto url_view = doc["url"].get_string().value;
            url = std::string(url_view.data(), url_view.length());
        }
        if (doc["html_content"]) {
            auto html_view = doc["html_content"].get_string().value;
            html = std::string(html_view.data(), html_view.length());
        }
        
        if (html.empty()) continue;
        
        std::string title = tokenizer_.extract_title(html);
        std::string text = tokenizer_.extract_text(html);
        
        auto tokens = tokenizer_.tokenize(text);
        
        DocumentInfo doc_info;
        doc_info.doc_id = doc_id;
        doc_info.title = title;
        doc_info.url = url;
        documents_.push_back(doc_info);
        
        std::set<std::string> unique_terms(tokens.begin(), tokens.end());
        for (const auto& term : unique_terms) {
            inverted_index_[term].push_back(doc_id);
        }
        
        stats_.total_tokens += tokens.size();
        stats_.total_text_bytes += text.size();
        
        ++doc_id;
        
        if (doc_id % 500 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - start_time).count();
            double speed = doc_id / elapsed;
            
            std::cout << "   [" << doc_id << "/" << total_docs << "] "
                      << speed << " docs/sec, terms: " << inverted_index_.size() << "\n";
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    stats_.indexing_time_sec = std::chrono::duration<double>(end_time - start_time).count();
    stats_.total_documents = documents_.size();
    stats_.unique_terms = inverted_index_.size();
    
    for (const auto& p : inverted_index_) {
        stats_.total_postings += p.second.size();
    }
    
    std::cout << "\nIndexing complete in " << stats_.indexing_time_sec << " sec\n";
    
    std::cout << "\nWriting index to " << output_path << "...\n";
    
    IndexWriter writer(output_path);
    writer.write_forward_index(documents_);
    writer.write_inverted_index(inverted_index_);
    writer.finalize();
    
    std::cout << "\n========================================\n";
    std::cout << "INDEXING STATISTICS\n";
    std::cout << "========================================\n";
    std::cout << "Documents: " << stats_.total_documents << "\n";
    std::cout << "Unique terms: " << stats_.unique_terms << "\n";
    std::cout << "Total tokens: " << stats_.total_tokens << "\n";
    std::cout << "Avg term length: " << stats_.avg_term_length(inverted_index_) << "\n";
    std::cout << "Total postings: " << stats_.total_postings << "\n";
    std::cout << "Speed: " << stats_.docs_per_second() << " docs/sec\n";
    std::cout << "Speed: " << stats_.kb_per_second() << " KB/sec\n";
    std::cout << "========================================\n";
}

}