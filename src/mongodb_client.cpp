#include "mongodb_client.hpp"
#include <yaml-cpp/yaml.h>
#include <iostream>

namespace wiki {

DbConfig load_config(const std::string& config_path) {
    DbConfig config;
    
    try {
        YAML::Node yaml = YAML::LoadFile(config_path);
        
        if (yaml["db"]) {
            auto db = yaml["db"];
            config.host = db["host"].as<std::string>("localhost");
            config.port = db["port"].as<int>(27017);
            config.database = db["database"].as<std::string>();
            config.collection = db["collection"].as<std::string>();
            config.username = db["username"].as<std::string>("");
            config.password = db["password"].as<std::string>("");
        }
    } catch (const std::exception& e) {
        std::cerr << "Ошибка загрузки конфигурации: " << e.what() << std::endl;
        throw;
    }
    
    return config;
}

MongoDBClient::MongoDBClient(const DbConfig& config) : config_(config) {
    instance_ = std::make_unique<mongocxx::instance>();
}

MongoDBClient::~MongoDBClient() = default;

bool MongoDBClient::connect() {
    try {
        std::string uri_str;
        
        if (!config_.username.empty() && !config_.password.empty()) {
            uri_str = "mongodb://" + config_.username + ":" + config_.password + "@" +
                      config_.host + ":" + std::to_string(config_.port);
        } else {
            uri_str = "mongodb://" + config_.host + ":" + std::to_string(config_.port);
        }
        
        mongocxx::uri uri(uri_str);
        client_ = std::make_unique<mongocxx::client>(uri);
        
        db_ = (*client_)[config_.database];
        collection_ = db_[config_.collection];
        
        std::cout << "✓ Подключение к MongoDB: " << config_.host << ":" << config_.port << std::endl;
        std::cout << "  База: " << config_.database << ", коллекция: " << config_.collection << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Ошибка подключения к MongoDB: " << e.what() << std::endl;
        return false;
    }
}

size_t MongoDBClient::count_documents() const {
    return collection_.count_documents({});
}

void MongoDBClient::for_each_document(
    std::function<void(const Document&)> callback,
    size_t limit
) const {
    auto opts = mongocxx::options::find{};
    opts.projection(bsoncxx::builder::basic::make_document(
        bsoncxx::builder::basic::kvp("url", 1),
        bsoncxx::builder::basic::kvp("html_content", 1)
    ));
    
    if (limit > 0) {
        opts.limit(static_cast<int64_t>(limit));
    }
    
    auto cursor = collection_.find({}, opts);
    
    for (auto&& doc : cursor) {
        Document document;
        
        if (doc["url"]) {
            auto url_view = doc["url"].get_string().value;
            document.url = std::string(url_view.data(), url_view.length());
        }
        
        if (doc["html_content"]) {
            auto html_view = doc["html_content"].get_string().value;
            document.html_content = std::string(html_view.data(), html_view.length());
        }
        
        callback(document);
    }
}

}