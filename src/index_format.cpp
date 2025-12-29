#include "index_format.hpp"
#include <algorithm>
#include <stdexcept>

namespace search {

void IndexHeader::write(std::ostream& out) const {
    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&version_major), 2);
    out.write(reinterpret_cast<const char*>(&version_minor), 2);
    out.write(reinterpret_cast<const char*>(&flags), 4);
    out.write(reinterpret_cast<const char*>(&num_documents), 4);
    out.write(reinterpret_cast<const char*>(&num_terms), 4);
    out.write(reinterpret_cast<const char*>(&reserved), 4);
    out.write(reinterpret_cast<const char*>(&forward_offset), 8);
}

void IndexHeader::read(std::istream& in) {
    in.read(reinterpret_cast<char*>(&magic), 4);
    in.read(reinterpret_cast<char*>(&version_major), 2);
    in.read(reinterpret_cast<char*>(&version_minor), 2);
    in.read(reinterpret_cast<char*>(&flags), 4);
    in.read(reinterpret_cast<char*>(&num_documents), 4);
    in.read(reinterpret_cast<char*>(&num_terms), 4);
    in.read(reinterpret_cast<char*>(&reserved), 4);
    in.read(reinterpret_cast<char*>(&forward_offset), 8);
}

IndexWriter::IndexWriter(const std::string& path) : path_(path) {
    file_.open(path, std::ios::binary);
    if (!file_) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    char zeros[IndexHeader::SIZE] = {0};
    file_.write(zeros, IndexHeader::SIZE);
}

IndexWriter::~IndexWriter() {
    if (file_.is_open()) {
        file_.close();
    }
}

void IndexWriter::write_forward_index(const std::vector<DocumentInfo>& docs) {
    header_.forward_offset = static_cast<uint64_t>(file_.tellp());
    header_.num_documents = static_cast<uint32_t>(docs.size());
    
    for (const auto& doc : docs) {
        file_.write(reinterpret_cast<const char*>(&doc.doc_id), 4);
        
        uint16_t title_len = static_cast<uint16_t>(doc.title.size());
        file_.write(reinterpret_cast<const char*>(&title_len), 2);
        file_.write(doc.title.data(), title_len);
        
        uint16_t url_len = static_cast<uint16_t>(doc.url.size());
        file_.write(reinterpret_cast<const char*>(&url_len), 2);
        file_.write(doc.url.data(), url_len);
    }
}

void IndexWriter::write_inverted_index(
    const std::unordered_map<std::string, std::vector<uint32_t>>& index) {
    
    header_.num_terms = static_cast<uint32_t>(index.size());
    
    std::vector<std::string> terms;
    terms.reserve(index.size());
    for (const auto& p : index) {
        terms.push_back(p.first);
    }
    std::sort(terms.begin(), terms.end());
    
    uint32_t num_terms = static_cast<uint32_t>(terms.size());
    file_.write(reinterpret_cast<const char*>(&num_terms), 4);
    
    for (const auto& term : terms) {
        const auto& posting_list = index.at(term);
        
        uint8_t term_len = static_cast<uint8_t>(term.size());
        file_.write(reinterpret_cast<const char*>(&term_len), 1);
        file_.write(term.data(), term_len);
        
        uint32_t df = static_cast<uint32_t>(posting_list.size());
        file_.write(reinterpret_cast<const char*>(&df), 4);
        
        std::vector<uint32_t> sorted_list = posting_list;
        std::sort(sorted_list.begin(), sorted_list.end());
        
        for (uint32_t doc_id : sorted_list) {
            file_.write(reinterpret_cast<const char*>(&doc_id), 4);
        }
    }
}

void IndexWriter::finalize() {
    file_.seekp(0);
    header_.write(file_);
    file_.close();
}

IndexReader::IndexReader(const std::string& path) : path_(path) {}

IndexReader::~IndexReader() {
    close();
}

bool IndexReader::open() {
    file_.open(path_, std::ios::binary);
    if (!file_) {
        return false;
    }
    
    header_.read(file_);
    
    if (header_.magic != MAGIC_NUMBER) {
        file_.close();
        return false;
    }
    
    return true;
}

void IndexReader::close() {
    if (file_.is_open()) {
        file_.close();
    }
}

std::unordered_map<uint32_t, DocumentInfo> IndexReader::load_documents() {
    if (docs_loaded_) {
        return docs_cache_;
    }
    
    file_.seekg(header_.forward_offset);
    
    for (uint32_t i = 0; i < header_.num_documents; ++i) {
        DocumentInfo doc;
        
        file_.read(reinterpret_cast<char*>(&doc.doc_id), 4);
        
        uint16_t title_len;
        file_.read(reinterpret_cast<char*>(&title_len), 2);
        doc.title.resize(title_len);
        file_.read(doc.title.data(), title_len);
        
        uint16_t url_len;
        file_.read(reinterpret_cast<char*>(&url_len), 2);
        doc.url.resize(url_len);
        file_.read(doc.url.data(), url_len);
        
        docs_cache_[doc.doc_id] = doc;
    }
    
    docs_loaded_ = true;
    return docs_cache_;
}

std::unordered_map<std::string, std::vector<uint32_t>> IndexReader::load_inverted_index() {
    if (inverted_loaded_) {
        return inverted_cache_;
    }
    
    load_documents();
    
    uint32_t num_terms;
    file_.read(reinterpret_cast<char*>(&num_terms), 4);
    
    for (uint32_t i = 0; i < num_terms; ++i) {
        uint8_t term_len;
        file_.read(reinterpret_cast<char*>(&term_len), 1);
        
        std::string term(term_len, '\0');
        file_.read(term.data(), term_len);
        
        uint32_t df;
        file_.read(reinterpret_cast<char*>(&df), 4);
        
        std::vector<uint32_t> posting_list(df);
        for (uint32_t j = 0; j < df; ++j) {
            file_.read(reinterpret_cast<char*>(&posting_list[j]), 4);
        }
        
        inverted_cache_[term] = std::move(posting_list);
    }
    
    inverted_loaded_ = true;
    return inverted_cache_;
}

std::vector<uint32_t> IndexReader::get_posting_list(const std::string& term) {
    load_inverted_index();
    
    auto it = inverted_cache_.find(term);
    if (it != inverted_cache_.end()) {
        return it->second;
    }
    return {};
}

std::vector<uint32_t> IndexReader::get_all_doc_ids() {
    load_documents();
    
    std::vector<uint32_t> ids;
    ids.reserve(docs_cache_.size());
    for (const auto& p : docs_cache_) {
        ids.push_back(p.first);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

DocumentInfo IndexReader::get_document(uint32_t doc_id) {
    load_documents();
    
    auto it = docs_cache_.find(doc_id);
    if (it != docs_cache_.end()) {
        return it->second;
    }
    return DocumentInfo{};
}

}