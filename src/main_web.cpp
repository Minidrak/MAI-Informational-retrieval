#include "web_server.hpp"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    search::WebServer::Config config;
    config.index_path = "index.bin";
    config.host = "0.0.0.0";
    config.port = 8080;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--index" && i + 1 < argc) {
            config.index_path = argv[++i];
        } else if (arg == "--host" && i + 1 < argc) {
            config.host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.port = std::atoi(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n\n"
                      << "Options:\n"
                      << "  --index PATH  Index file (default: index.bin)\n"
                      << "  --host HOST   Host (default: 0.0.0.0)\n"
                      << "  --port PORT   Port (default: 8080)\n";
            return 0;
        } else if (i == 1 && arg[0] != '-') {
            config.index_path = arg;
        }
    }
    
    try {
        search::WebServer server(config);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}