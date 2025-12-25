#pragma once

#include <string>
#include <string_view>

namespace wiki {

/**
 * Стеммер Портера для русского языка
 * Основан на алгоритме Snowball Russian Stemmer
 */
class RussianStemmer {
public:
    RussianStemmer() = default;
    
    /**
     * Применяет стемминг к слову
     * @param word Слово в нижнем регистре (UTF-8)
     * @return Основа слова
     */
    std::string stem(const std::string& word) const;
    
private:
    // Регионы слова
    struct Regions {
        size_t rv = 0;  // После первой гласной
        size_t r1 = 0;  // После первой согласной после гласной
        size_t r2 = 0;  // R1 применённый к R1
    };
    
    Regions find_regions(const std::string& word) const;
    
    bool is_vowel(char32_t ch) const;
    bool ends_with(const std::string& word, const std::string& suffix) const;
    std::string remove_suffix(const std::string& word, const std::string& suffix) const;
    
    // Шаги алгоритма
    std::string step1(const std::string& word, const Regions& regions) const;
    std::string step2(const std::string& word, const Regions& regions) const;
    std::string step3(const std::string& word, const Regions& regions) const;
    std::string step4(const std::string& word, const Regions& regions) const;
    
    // Группы окончаний
    static const std::vector<std::string> PERFECTIVE_GERUND_1;
    static const std::vector<std::string> PERFECTIVE_GERUND_2;
    static const std::vector<std::string> ADJECTIVE;
    static const std::vector<std::string> PARTICIPLE_1;
    static const std::vector<std::string> PARTICIPLE_2;
    static const std::vector<std::string> REFLEXIVE;
    static const std::vector<std::string> VERB_1;
    static const std::vector<std::string> VERB_2;
    static const std::vector<std::string> NOUN;
    static const std::vector<std::string> SUPERLATIVE;
    static const std::vector<std::string> DERIVATIONAL;
};

} // namespace wiki