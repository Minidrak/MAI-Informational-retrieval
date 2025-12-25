#pragma once

#include <string>
#include <vector>
#include <unordered_set>

namespace search {

class Tokenizer {
public:
    struct Config {
        size_t min_length = 2;
        bool lowercase = true;
        bool remove_stopwords = true;
    };
    
    explicit Tokenizer(const Config& config = Config{});
    
    std::string extract_text(const std::string& html) const;
    std::string extract_title(const std::string& html) const;
    std::vector<std::string> tokenize(const std::string& text) const;
    std::string normalize(const std::string& term) const;
    
private:
    Config config_;
    std::unordered_set<std::string> stop_words_;
    
    void init_stop_words();
    std::string to_lower(const std::string& str) const;
};

}