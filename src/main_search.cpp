#include "searcher.hpp"
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <shellapi.h>

std::string utf16_to_utf8(const wchar_t* utf16_str) {
    if (!utf16_str || !*utf16_str) return "";
    
    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, utf16_str, -1, nullptr, 0, nullptr, nullptr);
    if (utf8_size <= 0) return "";
    
    std::vector<char> utf8(utf8_size);
    WideCharToMultiByte(CP_UTF8, 0, utf16_str, -1, utf8.data(), utf8_size, nullptr, nullptr);
    
    return std::string(utf8.data());
}

std::vector<std::string> get_utf8_args(int& argc, char* argv[]) {
    std::vector<std::string> utf8_args;
    
    int num_args = 0;
    LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &num_args);
    
    if (!argv_w || num_args == 0) {
        argc = argc;
        for (int i = 0; i < argc; ++i) {
            utf8_args.push_back(argv[i]);
        }
        return utf8_args;
    }
    
    argc = num_args;
    for (int i = 0; i < num_args; ++i) {
        utf8_args.push_back(utf16_to_utf8(argv_w[i]));
    }
    
    LocalFree(argv_w);
    return utf8_args;
}
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    
    std::vector<std::string> utf8_args = get_utf8_args(argc, argv);
    
    std::vector<char*> utf8_argv;
    for (auto& arg : utf8_args) {
        utf8_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    char** utf8_argv_ptr = utf8_argv.data();
#else
    char** utf8_argv_ptr = argv;
#endif
    
    if (argc < 2) {
        std::cerr << "Usage: " << utf8_argv_ptr[0] << " <index.bin> [options]\n"
                  << "\nOptions:\n"
                  << "  -q QUERY     Single query\n"
                  << "  -i           Interactive mode\n"
                  << "  -l LIMIT     Results limit (default: 10)\n"
                  << "  --stats      Show statistics\n";
        return 1;
    }
    
    std::string index_path = utf8_argv_ptr[1];
    std::string query;
    bool interactive = false;
    bool show_stats = false;
    size_t limit = 10;
    
    for (int i = 2; i < argc; ++i) {
        std::string arg = utf8_argv_ptr[i];
        if (arg == "-q" && i + 1 < argc) {
            query = utf8_argv_ptr[++i];
        } else if (arg == "-i") {
            interactive = true;
        } else if (arg == "-l" && i + 1 < argc) {
            limit = std::stoul(utf8_argv_ptr[++i]);
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
            
            if (response.total_count == 0) {
                std::cout << "No results found. Try checking:\n";
                std::cout << "  1. Index was built correctly\n";
                std::cout << "  2. Query term exists in the index\n";
                std::cout << "  3. Use --stats to see index statistics\n";
            }
            
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