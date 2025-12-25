#include "searcher.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <index.bin> [options]\n"
                  << "\nOptions:\n"
                  << "  -q QUERY     Single query\n"
                  << "  -i           Interactive mode\n"
                  << "  -l LIMIT     Results limit (default: 10)\n"
                  << "  --stats      Show statistics\n";
        return 1;
    }
    
    std::string index_path = argv[1];
    std::string query;
    bool interactive = false;
    bool show_stats = false;
    size_t limit = 10;
    
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-q" && i + 1 < argc) {
            query = argv[++i];
        } else if (arg == "-i") {
            interactive = true;
        } else if (arg == "-l" && i + 1 < argc) {
            limit = std::stoul(argv[++i]);
        } else if (arg == "--stats") {
            show_stats = true;
        }
    }
    
    try {
        search::Searcher searcher(index_path);
        
        if (!searcher.open()) {
            std::cerr << "Error opening index\n";
            return 1;
        }
        
        if (show_stats) {
            std::cout << "Documents: " << searcher.num_documents() << "\n";
            std::cout << "Terms: " << searcher.num_terms() << "\n";
        }
        
        auto execute_query = [&](const std::string& q) {
            auto response = searcher.search(q, limit);
            
            std::cout << "\n=== Query: " << q << " ===\n";
            std::cout << "Found: " << response.total_count << " in " 
                      << response.query_time_ms << " ms\n\n";
            
            for (size_t i = 0; i < response.results.size(); ++i) {
                std::cout << (i + 1) << ". " << response.results[i].title << "\n";
                std::cout << "   " << response.results[i].url << "\n\n";
            }
        };
        
        if (!query.empty()) {
            execute_query(query);
        } else if (interactive) {
            std::cout << "Interactive mode. Ctrl+D to exit.\n\n";
            
            std::string line;
            while (std::cout << ">>> " && std::getline(std::cin, line)) {
                if (!line.empty()) {
                    execute_query(line);
                }
            }
        } else {
            std::string line;
            while (std::getline(std::cin, line)) {
                if (!line.empty()) {
                    execute_query(line);
                }
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}