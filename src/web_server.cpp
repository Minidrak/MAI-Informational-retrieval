#include "web_server.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT 0
#include "httplib.h"

#include <sstream>
#include <iostream>
#include <iomanip>

namespace search {

WebServer::WebServer(const Config& config) : config_(config) {
    searcher_ = std::make_unique<Searcher>(config_.index_path);
}

WebServer::~WebServer() = default;

std::string WebServer::html_escape(const std::string& s) const {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '&': result += "&amp;"; break;
            case '"': result += "&quot;"; break;
            default: result += c;
        }
    }
    return result;
}

std::string WebServer::url_decode(const std::string& s) const {
    std::string result;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int value;
            std::istringstream is(s.substr(i + 1, 2));
            if (is >> std::hex >> value) {
                result += static_cast<char>(value);
                i += 2;
            } else {
                result += s[i];
            }
        } else if (s[i] == '+') {
            result += ' ';
        } else {
            result += s[i];
        }
    }
    return result;
}

std::string WebServer::render_index_page() const {
    return R"(<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Search</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#f5f5f5;min-height:100vh;display:flex;align-items:center;justify-content:center}
.container{text-align:center;padding:20px}
h1{font-size:3rem;margin-bottom:30px}
.search-form{display:flex;max-width:600px;margin:0 auto 30px}
input[type="text"]{flex:1;padding:15px 20px;font-size:18px;border:2px solid #ddd;border-radius:25px 0 0 25px;outline:none}
input[type="text"]:focus{border-color:#4a90d9}
button{padding:15px 30px;font-size:18px;background:#4a90d9;color:white;border:none;border-radius:0 25px 25px 0;cursor:pointer}
button:hover{background:#357abd}
.hints{background:white;padding:25px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);max-width:600px;margin:0 auto;text-align:left}
.hints h3{margin:15px 0 10px;color:#555}
.hints h3:first-child{margin-top:0}
.hints ul{padding-left:20px}
.hints li{margin:5px 0}
.hints code{background:#f0f0f0;padding:2px 6px;border-radius:3px}
.hints a{color:#4a90d9;text-decoration:none}
.hints a:hover{text-decoration:underline}
</style>
</head>
<body>
<div class="container">
<h1>Search</h1>
<form action="/search" method="get" class="search-form">
<input type="text" name="q" placeholder="Enter search query..." autofocus>
<button type="submit">Search</button>
</form>
<div class="hints">
<h3>Query syntax:</h3>
<ul>
<li><code>word1 word2</code> - both words (AND)</li>
<li><code>word1 || word2</code> - any word (OR)</li>
<li><code>!word</code> - exclude word (NOT)</li>
<li><code>(word1 || word2) word3</code> - grouping</li>
</ul>
</div>
</div>
</body>
</html>)";
}

std::string WebServer::render_results_page(const std::string& query,
                                           const SearchResponse& response,
                                           size_t page) const {
    std::ostringstream html;
    
    size_t limit = 50;
    size_t total_pages = (response.total_count + limit - 1) / limit;
    if (total_pages == 0) total_pages = 1;
    bool has_next = page < total_pages;
    bool has_prev = page > 1;
    
    html << R"(<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>)" << html_escape(query) << R"( - Search Results</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#f5f5f5;line-height:1.6}
.container{max-width:900px;margin:0 auto;padding:20px}
header{display:flex;align-items:center;gap:20px;margin-bottom:20px;padding-bottom:20px;border-bottom:1px solid #ddd}
header h1{font-size:1.5rem}
header h1 a{color:inherit;text-decoration:none}
.search-form{display:flex;flex:1;max-width:500px}
input[type="text"]{flex:1;padding:10px 15px;font-size:16px;border:2px solid #ddd;border-radius:20px 0 0 20px;outline:none}
button{padding:10px 20px;font-size:16px;background:#4a90d9;color:white;border:none;border-radius:0 20px 20px 0;cursor:pointer}
.stats{color:#666;margin-bottom:20px}
.result{background:white;padding:20px;margin-bottom:15px;border-radius:8px;box-shadow:0 1px 5px rgba(0,0,0,0.1)}
.result h3{margin-bottom:5px}
.result h3 a{color:#1a0dab;text-decoration:none}
.result h3 a:hover{text-decoration:underline}
.result cite{color:#006621;font-style:normal;font-size:14px;word-break:break-all}
.pagination{display:flex;justify-content:center;align-items:center;gap:20px;padding:20px 0}
.pagination a{color:#4a90d9;text-decoration:none;padding:10px 20px;border:1px solid #4a90d9;border-radius:5px}
.pagination a:hover{background:#4a90d9;color:white}
.no-results{text-align:center;padding:50px;background:white;border-radius:10px}
</style>
</head>
<body>
<div class="container">
<header>
<h1><a href="/">Search</a></h1>
<form action="/search" method="get" class="search-form">
<input type="text" name="q" value=")" << html_escape(query) << R"(">
<button type="submit">Search</button>
</form>
</header>
<div class="stats">
Found: <strong>)" << response.total_count << R"(</strong> documents
in <strong>)" << std::fixed << std::setprecision(2) << response.query_time_ms << R"(</strong> ms
</div>
)";

    if (!response.results.empty()) {
        html << "<div class=\"results\">\n";
        
        for (const auto& result : response.results) {
            html << "<div class=\"result\">\n";
            html << "<h3><a href=\"" << html_escape(result.url) 
                 << "\" target=\"_blank\">" << html_escape(result.title) << "</a></h3>\n";
            html << "<cite>" << html_escape(result.url) << "</cite>\n";
            html << "</div>\n";
        }
        
        html << "</div>\n";
        
        html << "<div class=\"pagination\">\n";
        
        if (has_prev) {
            html << "<a href=\"/search?q=" << html_escape(query) 
                 << "&page=" << (page - 1) << "\">Previous</a>\n";
        }
        
        html << "<span>Page " << page << " of " << total_pages << "</span>\n";
        
        if (has_next) {
            html << "<a href=\"/search?q=" << html_escape(query) 
                 << "&page=" << (page + 1) << "\">Next</a>\n";
        }
        
        html << "</div>\n";
    } else {
        html << R"(<div class="no-results">
<p>No results found for <strong>)" << html_escape(query) << R"(</strong></p>
</div>
)";
    }

    html << R"(</div>
</body>
</html>)";

    return html.str();
}

void WebServer::run() {
    if (!searcher_->open()) {
        std::cerr << "Error opening index\n";
        return;
    }
    
    std::cout << "Starting web server on http://" << config_.host 
              << ":" << config_.port << "\n";
    std::cout << "Index: " << searcher_->num_documents() << " documents, "
              << searcher_->num_terms() << " terms\n";
    
    httplib::Server server;
    
    server.Get("/", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(render_index_page(), "text/html; charset=utf-8");
    });
    
    server.Get("/search", [this](const httplib::Request& req, httplib::Response& res) {
        std::string query;
        size_t page = 1;
        
        if (req.has_param("q")) {
            query = url_decode(req.get_param_value("q"));
        }
        
        if (req.has_param("page")) {
            try {
                page = std::stoul(req.get_param_value("page"));
            } catch (...) {}
        }
        
        if (query.empty()) {
            res.set_redirect("/");
            return;
        }
        
        size_t limit = 50;
        size_t offset = (page - 1) * limit;
        
        auto response = searcher_->search(query, limit, offset);
        
        res.set_content(render_results_page(query, response, page), "text/html; charset=utf-8");
    });
    
    server.Get("/api/search", [this](const httplib::Request& req, httplib::Response& res) {
        std::string query;
        size_t limit = 50;
        size_t page = 1;
        
        if (req.has_param("q")) query = url_decode(req.get_param_value("q"));
        if (req.has_param("limit")) {
            try { limit = std::stoul(req.get_param_value("limit")); } catch (...) {}
        }
        if (req.has_param("page")) {
            try { page = std::stoul(req.get_param_value("page")); } catch (...) {}
        }
        
        size_t offset = (page - 1) * limit;
        auto response = searcher_->search(query, limit, offset);
        
        std::ostringstream json;
        json << "{\"query\":\"" << query << "\","
             << "\"total\":" << response.total_count << ","
             << "\"time_ms\":" << response.query_time_ms << ","
             << "\"results\":[";
        
        for (size_t i = 0; i < response.results.size(); ++i) {
            if (i > 0) json << ",";
            json << "{\"title\":\"" << response.results[i].title << "\","
                 << "\"url\":\"" << response.results[i].url << "\"}";
        }
        
        json << "]}";
        
        res.set_content(json.str(), "application/json; charset=utf-8");
    });
    
    server.listen(config_.host.c_str(), config_.port);
}

}