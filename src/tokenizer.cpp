#include "tokenizer.hpp"
#include <algorithm>
#include <cctype>

namespace search {

Tokenizer::Tokenizer(const Config& config) : config_(config) {
    init_stop_words();
}

void Tokenizer::init_stop_words() {
    stop_words_ = {
        "и", "в", "во", "не", "что", "он", "на", "я", "с", "со", "как", "а", "то", "все",
        "она", "так", "его", "но", "да", "ты", "к", "у", "же", "вы", "за", "бы", "по",
        "только", "её", "мне", "было", "вот", "от", "меня", "ещё", "нет", "о", "из", "ему",
        "для", "при", "без", "до", "под", "над", "об", "про", "это", "этот", "эта", "эти",
        "был", "была", "были", "быть", "есть", "или", "также", "году", "года", "лет",
        "который", "которая", "которое", "которые", "где", "когда", "если", "чем",
        "the", "a", "an", "and", "or", "but", "in", "on", "at", "to", "for", "of", "with",
        "is", "was", "are", "were", "been", "be", "have", "has", "had", "it", "its"
    };
}

std::string Tokenizer::to_lower(const std::string& str) const {
    std::string result;
    result.reserve(str.size());
    
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = str[i];
        
        if (c < 128) {
            result += std::tolower(c);
        } else if ((c & 0xE0) == 0xC0 && i + 1 < str.size()) {
            unsigned char c2 = str[i + 1];
            
            if (c == 0xD0 && c2 >= 0x90 && c2 <= 0xAF) {
                result += static_cast<char>(c);
                result += static_cast<char>(c2 + 0x20);
            }
            else if (c == 0xD0 && c2 == 0x81) {
                result += static_cast<char>(0xD1);
                result += static_cast<char>(0x91);
            }
            else {
                result += static_cast<char>(c);
                result += static_cast<char>(c2);
            }
            ++i;
        } else {
            result += c;
        }
    }
    
    return result;
}

std::string Tokenizer::extract_text(const std::string& html) const {
    std::string result;
    result.reserve(html.size());
    
    bool in_tag = false;
    bool in_script = false;
    bool in_style = false;
    
    for (size_t i = 0; i < html.size(); ++i) {
        char c = html[i];
        
        if (c == '<') {
            in_tag = true;
            
            std::string lower;
            for (size_t j = i; j < std::min(i + 10, html.size()); ++j) {
                lower += std::tolower(html[j]);
            }
            
            if (lower.find("<script") == 0) in_script = true;
            else if (lower.find("</script") == 0) in_script = false;
            else if (lower.find("<style") == 0) in_style = true;
            else if (lower.find("</style") == 0) in_style = false;
            
            continue;
        }
        
        if (c == '>') {
            in_tag = false;
            result += ' ';
            continue;
        }
        
        if (!in_tag && !in_script && !in_style) {
            result += c;
        }
    }
    
    std::string normalized;
    bool last_space = true;
    for (char c : result) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!last_space) {
                normalized += ' ';
                last_space = true;
            }
        } else {
            normalized += c;
            last_space = false;
        }
    }
    
    return normalized;
}

std::string Tokenizer::extract_title(const std::string& html) const {
    std::string lower_html = html;
    std::transform(lower_html.begin(), lower_html.end(), lower_html.begin(), ::tolower);
    
    size_t start = lower_html.find("<title>");
    if (start == std::string::npos) {
        start = lower_html.find("<title ");
    }
    
    if (start != std::string::npos) {
        start = html.find('>', start) + 1;
        size_t end = lower_html.find("</title>", start);
        
        if (end != std::string::npos) {
            std::string title = html.substr(start, end - start);
            
            size_t wiki_pos = title.find(" — ");
            if (wiki_pos != std::string::npos) {
                title = title.substr(0, wiki_pos);
            }
            wiki_pos = title.find(" - ");
            if (wiki_pos != std::string::npos) {
                title = title.substr(0, wiki_pos);
            }
            
            return title;
        }
    }
    
    start = lower_html.find("<h1");
    if (start != std::string::npos) {
        start = html.find('>', start) + 1;
        size_t end = lower_html.find("</h1>", start);
        if (end != std::string::npos) {
            return extract_text(html.substr(start, end - start));
        }
    }
    
    return "Untitled";
}

std::string Tokenizer::normalize(const std::string& term) const {
    if (config_.lowercase) {
        return to_lower(term);
    }
    return term;
}

std::vector<std::string> Tokenizer::tokenize(const std::string& text) const {
    std::string normalized = config_.lowercase ? to_lower(text) : text;
    
    std::vector<std::string> tokens;
    std::string current_token;
    
    for (size_t i = 0; i < normalized.size(); ++i) {
        unsigned char c = normalized[i];
        
        bool is_letter = false;
        
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            is_letter = true;
            current_token += c;
        }
        else if ((c == 0xD0 || c == 0xD1) && i + 1 < normalized.size()) {
            is_letter = true;
            current_token += c;
            current_token += normalized[++i];
        }
        
        if (!is_letter) {
            if (!current_token.empty()) {
                if (current_token.size() >= config_.min_length) {
                    if (!config_.remove_stopwords || stop_words_.find(current_token) == stop_words_.end()) {
                        tokens.push_back(current_token);
                    }
                }
                current_token.clear();
            }
        }
    }
    
    if (!current_token.empty() && current_token.size() >= config_.min_length) {
        if (!config_.remove_stopwords || stop_words_.find(current_token) == stop_words_.end()) {
            tokens.push_back(current_token);
        }
    }
    
    return tokens;
}

}