#include "indexer.hpp"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    search::Indexer::Config config;
    config.mongo_host = "localhost";
    config.mongo_port = 27017;
    config.mongo_db = "search_engine_db";
    config.mongo_collection = "documents";
    
    std::string output_path = "index.bin";
    size_t limit = 0;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--host" && i + 1 < argc) {
            config.mongo_host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.mongo_port = std::atoi(argv[++i]);
        } else if (arg == "--db" && i + 1 < argc) {
            config.mongo_db = argv[++i];
        } else if (arg == "--collection" && i + 1 < argc) {
            config.mongo_collection = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--limit" && i + 1 < argc) {
            limit = std::stoul(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n\n"
                      << "Options:\n"
                      << "  --host HOST        MongoDB host (default: localhost)\n"
                      << "  --port PORT        MongoDB port (default: 27017)\n"
                      << "  --db NAME          Database name\n"
                      << "  --collection NAME  Collection name\n"
                      << "  --output PATH      Output file (default: index.bin)\n"
                      << "  --limit N          Limit documents\n";
            return 0;
        }
    }
    
    try {
        search::Indexer indexer(config);
        indexer.build(output_path, limit);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}