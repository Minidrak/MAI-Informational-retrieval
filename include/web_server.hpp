#pragma once

#include <string>
#include <memory>

#include "searcher.hpp"

namespace search {

class WebServer {
public:
    struct Config {
        std::string host = "0.0.0.0";
        int port = 8080;
        std::string index_path;
    };
    
    explicit WebServer(const Config& config);
    ~WebServer();
    
    void run();
    
private:
    Config config_;
    std::unique_ptr<Searcher> searcher_;
    
    std::string render_index_page() const;
    std::string render_results_page(const std::string& query, 
                                    const SearchResponse& response,
                                    size_t page) const;
    
    std::string html_escape(const std::string& s) const;
    std::string url_decode(const std::string& s) const;
};

}