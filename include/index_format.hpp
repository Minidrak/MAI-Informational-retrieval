#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <memory>
#include <cstdint>

namespace search {

constexpr uint32_t MAGIC_NUMBER = 0x49445831;
constexpr uint16_t VERSION_MAJOR = 1;
constexpr uint16_t VERSION_MINOR = 0;

struct DocumentInfo {
    uint32_t doc_id;
    std::string title;
    std::string url;
};

struct IndexHeader {
    uint32_t magic = MAGIC_NUMBER;
    uint16_t version_major = VERSION_MAJOR;
    uint16_t version_minor = VERSION_MINOR;
    uint32_t flags = 0;
    uint32_t num_documents = 0;
    uint32_t num_terms = 0;
    uint32_t reserved = 0;
    uint64_t forward_offset = 0;
    
    static constexpr size_t SIZE = 32;
    
    void write(std::ostream& out) const;
    void read(std::istream& in);
};

class IndexWriter {
public:
    explicit IndexWriter(const std::string& path);
    ~IndexWriter();
    
    void write_forward_index(const std::vector<DocumentInfo>& docs);
    void write_inverted_index(const std::unordered_map<std::string, std::vector<uint32_t>>& index);
    void finalize();
    
private:
    std::string path_;
    std::ofstream file_;
    IndexHeader header_;
};

class IndexReader {
public:
    explicit IndexReader(const std::string& path);
    ~IndexReader();
    
    bool open();
    void close();
    
    const IndexHeader& header() const { return header_; }
    
    std::unordered_map<uint32_t, DocumentInfo> load_documents();
    std::unordered_map<std::string, std::vector<uint32_t>> load_inverted_index();
    std::vector<uint32_t> get_posting_list(const std::string& term);
    std::vector<uint32_t> get_all_doc_ids();
    DocumentInfo get_document(uint32_t doc_id);
    
private:
    std::string path_;
    std::ifstream file_;
    IndexHeader header_;
    
    std::unordered_map<uint32_t, DocumentInfo> docs_cache_;
    std::unordered_map<std::string, std::vector<uint32_t>> inverted_cache_;
    bool docs_loaded_ = false;
    bool inverted_loaded_ = false;
};

}