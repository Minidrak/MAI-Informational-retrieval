#include "stemmer.hpp"
#include <algorithm>
#include <codecvt>
#include <locale>

namespace wiki {

// Группы окончаний для русского языка
const std::vector<std::string> RussianStemmer::PERFECTIVE_GERUND_1 = {
    "вшись", "вши", "в"
};

const std::vector<std::string> RussianStemmer::PERFECTIVE_GERUND_2 = {
    "ившись", "ывшись", "ивши", "ывши", "ив", "ыв"
};

const std::vector<std::string> RussianStemmer::ADJECTIVE = {
    "ими", "ыми", "его", "ого", "ему", "ому", "ее", "ие", "ые", "ое",
    "ей", "ий", "ый", "ой", "ем", "им", "ым", "ом", "их", "ых",
    "ую", "юю", "ая", "яя", "ою", "ею"
};

const std::vector<std::string> RussianStemmer::PARTICIPLE_1 = {
    "ем", "нн", "вш", "ющ", "щ"
};

const std::vector<std::string> RussianStemmer::PARTICIPLE_2 = {
    "ивш", "ывш", "ующ"
};

const std::vector<std::string> RussianStemmer::REFLEXIVE = {
    "ся", "сь"
};

const std::vector<std::string> RussianStemmer::VERB_1 = {
    "ете", "йте", "ешь", "нно", "ла", "на", "ли", "ем", "ло",
    "но", "ет", "ют", "ны", "ть", "й", "л", "н"
};

const std::vector<std::string> RussianStemmer::VERB_2 = {
    "ейте", "уйте", "ила", "ыла", "ена", "ите", "или", "ыли", "ило",
    "ыло", "ено", "ует", "уют", "ены", "ить", "ыть", "ишь",
    "ую", "ей", "уй", "ил", "ыл", "им", "ым", "ен", "ят", "ит", "ыт",
    "ую", "ю"
};

const std::vector<std::string> RussianStemmer::NOUN = {
    "иями", "ями", "ами", "ией", "иям", "ием", "иях", "ев", "ов",
    "ие", "ье", "е|", "ьи", "ей", "ой", "ий", "ям", "ем", "ам",
    "ом", "ах", "ях", "ию", "ью", "ия", "ья", "и", "ы", "ь",
    "ю", "у", "о", "а", "е", "й"
};

const std::vector<std::string> RussianStemmer::SUPERLATIVE = {
    "ейше", "ейш"
};

const std::vector<std::string> RussianStemmer::DERIVATIONAL = {
    "ость", "ост"
};

bool RussianStemmer::is_vowel(char32_t ch) const {
    static const std::u32string vowels = U"аеиоуыэюяё";
    return vowels.find(ch) != std::u32string::npos;
}

bool RussianStemmer::ends_with(const std::string& word, const std::string& suffix) const {
    if (suffix.size() > word.size()) return false;
    return word.compare(word.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string RussianStemmer::remove_suffix(const std::string& word, const std::string& suffix) const {
    if (ends_with(word, suffix)) {
        return word.substr(0, word.size() - suffix.size());
    }
    return word;
}

RussianStemmer::Regions RussianStemmer::find_regions(const std::string& word) const {
    Regions regions;
    
    // Конвертируем в UTF-32 для правильной работы с кириллицей
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
    std::u32string word32;
    
    try {
        word32 = converter.from_bytes(word);
    } catch (...) {
        return regions;
    }
    
    size_t len = word32.size();
    
    // Находим RV - после первой гласной
    for (size_t i = 0; i < len; ++i) {
        if (is_vowel(word32[i])) {
            regions.rv = i + 1;
            break;
        }
    }
    
    // Находим R1 - после первой согласной после первой гласной
    bool found_vowel = false;
    for (size_t i = 0; i < len; ++i) {
        if (is_vowel(word32[i])) {
            found_vowel = true;
        } else if (found_vowel) {
            regions.r1 = i + 1;
            break;
        }
    }
    
    // Находим R2 - R1 применённый к R1
    found_vowel = false;
    for (size_t i = regions.r1; i < len; ++i) {
        if (is_vowel(word32[i])) {
            found_vowel = true;
        } else if (found_vowel) {
            regions.r2 = i + 1;
            break;
        }
    }
    
    // Конвертируем позиции обратно в байты
    // (упрощённо - для кириллицы каждый символ = 2 байта в UTF-8)
    regions.rv *= 2;
    regions.r1 *= 2;
    regions.r2 *= 2;
    
    return regions;
}

std::string RussianStemmer::step1(const std::string& word, const Regions& regions) const {
    std::string result = word;
    
    // Пробуем удалить PERFECTIVE GERUND
    for (const auto& suffix : PERFECTIVE_GERUND_2) {
        if (ends_with(result, suffix) && result.size() - suffix.size() >= regions.rv) {
            return remove_suffix(result, suffix);
        }
    }
    
    // Группа 1 требует предшествующую 'а' или 'я'
    for (const auto& suffix : PERFECTIVE_GERUND_1) {
        std::string test1 = "а" + suffix;
        std::string test2 = "я" + suffix;
        if (ends_with(result, test1) && result.size() - test1.size() >= regions.rv) {
            return remove_suffix(result, suffix);
        }
        if (ends_with(result, test2) && result.size() - test2.size() >= regions.rv) {
            return remove_suffix(result, suffix);
        }
    }
    
    // Удаляем REFLEXIVE
    for (const auto& suffix : REFLEXIVE) {
        if (ends_with(result, suffix) && result.size() - suffix.size() >= regions.rv) {
            result = remove_suffix(result, suffix);
            break;
        }
    }
    
    // Пробуем ADJECTIVE + PARTICIPLE
    bool found_adj = false;
    for (const auto& suffix : ADJECTIVE) {
        if (ends_with(result, suffix) && result.size() - suffix.size() >= regions.rv) {
            result = remove_suffix(result, suffix);
            found_adj = true;
            
            // Пробуем PARTICIPLE
            for (const auto& p_suffix : PARTICIPLE_2) {
                if (ends_with(result, p_suffix)) {
                    result = remove_suffix(result, p_suffix);
                    break;
                }
            }
            break;
        }
    }
    
    if (!found_adj) {
        // Пробуем VERB
        bool found = false;
        for (const auto& suffix : VERB_2) {
            if (ends_with(result, suffix) && result.size() - suffix.size() >= regions.rv) {
                result = remove_suffix(result, suffix);
                found = true;
                break;
            }
        }
        
        if (!found) {
            // VERB группа 1 (требует а/я перед)
            for (const auto& suffix : VERB_1) {
                std::string test1 = "а" + suffix;
                std::string test2 = "я" + suffix;
                if ((ends_with(result, test1) || ends_with(result, test2)) && 
                    result.size() - suffix.size() - 2 >= regions.rv) {
                    result = remove_suffix(result, suffix);
                    found = true;
                    break;
                }
            }
        }
        
        if (!found) {
            // Пробуем NOUN
            for (const auto& suffix : NOUN) {
                if (ends_with(result, suffix) && result.size() - suffix.size() >= regions.rv) {
                    result = remove_suffix(result, suffix);
                    break;
                }
            }
        }
    }
    
    return result;
}

std::string RussianStemmer::step2(const std::string& word, const Regions& regions) const {
    // Удаляем 'и' если в RV
    if (ends_with(word, "и") && word.size() - 2 >= regions.rv) {
        return remove_suffix(word, "и");
    }
    return word;
}

std::string RussianStemmer::step3(const std::string& word, const Regions& regions) const {
    std::string result = word;
    
    // Удаляем DERIVATIONAL из R2
    for (const auto& suffix : DERIVATIONAL) {
        if (ends_with(result, suffix) && result.size() - suffix.size() >= regions.r2) {
            result = remove_suffix(result, suffix);
            break;
        }
    }
    
    return result;
}

std::string RussianStemmer::step4(const std::string& word, const Regions& regions) const {
    std::string result = word;
    
    // Удаляем SUPERLATIVE
    for (const auto& suffix : SUPERLATIVE) {
        if (ends_with(result, suffix) && result.size() - suffix.size() >= regions.rv) {
            result = remove_suffix(result, suffix);
            break;
        }
    }
    
    // Удаляем 'нн' -> 'н' или 'ь'
    if (ends_with(result, "нн") && result.size() - 2 >= regions.rv) {
        result = remove_suffix(result, "н");
    } else if (ends_with(result, "ь") && result.size() - 2 >= regions.rv) {
        result = remove_suffix(result, "ь");
    }
    
    return result;
}

std::string RussianStemmer::stem(const std::string& word) const {
    if (word.empty() || word.size() < 4) {
        return word;  // Слишком короткое слово
    }
    
    Regions regions = find_regions(word);
    
    std::string result = word;
    result = step1(result, regions);
    result = step2(result, regions);
    result = step3(result, regions);
    result = step4(result, regions);
    
    return result;
}

} // namespace wiki