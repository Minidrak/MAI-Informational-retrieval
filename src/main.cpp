#include "tokenizer.hpp"
#include "zipf_analyzer.hpp"
#include "mongodb_client.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <fstream>

using namespace wiki;

void print_usage() {
    std::cout << "Использование:\n";
    std::cout << "  ./tokenizer <config.yaml>              - обработать весь корпус\n";
    std::cout << "  ./tokenizer <config.yaml> --limit 100  - обработать 100 документов\n";
    std::cout << "  ./tokenizer <config.yaml> --test       - тестовый режим (10 документов)\n";
}

void print_statistics(const TokenizerStats& stats) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "📊 СТАТИСТИКА ТОКЕНИЗАЦИИ\n";
    std::cout << std::string(60, '=') << "\n";
    
    std::cout << "\n📁 Документы:\n";
    std::cout << "   Обработано: " << stats.total_documents << "\n";
    std::cout << "   Размер: " << std::fixed << std::setprecision(2) 
              << (stats.total_bytes / 1024.0 / 1024.0) << " МБ\n";
    
    std::cout << "\n📝 Токены:\n";
    std::cout << "   Всего токенов: " << stats.total_tokens << "\n";
    std::cout << "   Уникальных токенов: " << stats.unique_tokens << "\n";
    std::cout << "   Уникальных стемов: " << stats.unique_stems << "\n";
    std::cout << "   Средняя длина: " << std::fixed << std::setprecision(2) 
              << stats.avg_token_length() << " символов\n";
    
    std::cout << "\n⏱️ Производительность:\n";
    std::cout << "   Время: " << std::fixed << std::setprecision(2) 
              << stats.processing_time_sec << " сек\n";
    std::cout << "   Скорость: " << std::fixed << std::setprecision(0) 
              << stats.tokens_per_second() << " токенов/сек\n";
    std::cout << "   Скорость: " << std::fixed << std::setprecision(2) 
              << stats.kb_per_second() << " КБ/сек\n";
    
    // Топ-20 токенов
    std::vector<std::pair<std::string, size_t>> sorted_tokens(
        stats.token_freq.begin(), stats.token_freq.end());
    std::sort(sorted_tokens.begin(), sorted_tokens.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::cout << "\n🔝 Топ-20 токенов:\n";
    for (size_t i = 0; i < std::min(size_t(20), sorted_tokens.size()); ++i) {
        std::cout << "   " << std::setw(2) << (i + 1) << ". " 
                  << sorted_tokens[i].first << ": " << sorted_tokens[i].second << "\n";
    }
    
    // Топ-20 стемов
    std::vector<std::pair<std::string, size_t>> sorted_stems(
        stats.stem_freq.begin(), stats.stem_freq.end());
    std::sort(sorted_stems.begin(), sorted_stems.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::cout << "\n🔝 Топ-20 стемов:\n";
    for (size_t i = 0; i < std::min(size_t(20), sorted_stems.size()); ++i) {
        std::cout << "   " << std::setw(2) << (i + 1) << ". " 
                  << sorted_stems[i].first << ": " << sorted_stems[i].second << "\n";
    }
    
    std::cout << std::string(60, '=') << "\n";
}

void save_statistics(const TokenizerStats& stats, const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Ошибка сохранения статистики в " << path << std::endl;
        return;
    }
    
    file << "СТАТИСТИКА ТОКЕНИЗАЦИИ\n";
    file << std::string(60, '=') << "\n\n";
    
    file << "ДОКУМЕНТЫ:\n";
    file << "  Обработано: " << stats.total_documents << "\n";
    file << "  Размер: " << (stats.total_bytes / 1024.0 / 1024.0) << " МБ\n\n";
    
    file << "ТОКЕНЫ:\n";
    file << "  Всего: " << stats.total_tokens << "\n";
    file << "  Уникальных токенов: " << stats.unique_tokens << "\n";
    file << "  Уникальных стемов: " << stats.unique_stems << "\n";
    file << "  Средняя длина: " << stats.avg_token_length() << "\n\n";
    
    file << "ПРОИЗВОДИТЕЛЬНОСТЬ:\n";
    file << "  Время: " << stats.processing_time_sec << " сек\n";
    file << "  Токенов/сек: " << stats.tokens_per_second() << "\n";
    file << "  КБ/сек: " << stats.kb_per_second() << "\n\n";
    
    // Топ-100 токенов
    std::vector<std::pair<std::string, size_t>> sorted_tokens(
        stats.token_freq.begin(), stats.token_freq.end());
    std::sort(sorted_tokens.begin(), sorted_tokens.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    file << "ТОП-100 ТОКЕНОВ:\n";
    for (size_t i = 0; i < std::min(size_t(100), sorted_tokens.size()); ++i) {
        file << "  " << (i + 1) << ". " << sorted_tokens[i].first 
             << ": " << sorted_tokens[i].second << "\n";
    }
    
    file.close();
    std::cout << "📄 Статистика сохранена: " << path << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    std::string config_path = argv[1];
    size_t limit = 0;
    
    // Парсим аргументы
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--limit" && i + 1 < argc) {
            limit = std::stoul(argv[++i]);
        } else if (arg == "--test") {
            limit = 10;
        }
    }
    
    std::cout << std::string(60, '=') << "\n";
    std::cout << "🔤 ТОКЕНИЗАЦИЯ И АНАЛИЗ КОРПУСА (C++)\n";
    std::cout << std::string(60, '=') << "\n";
    
    try {
        // Загружаем конфигурацию
        DbConfig db_config = load_config(config_path);
        
        // Подключаемся к MongoDB
        MongoDBClient db_client(db_config);
        if (!db_client.connect()) {
            return 1;
        }
        
        size_t total_docs = db_client.count_documents();
        if (limit > 0) {
            total_docs = std::min(total_docs, limit);
        }
        
        std::cout << "\n📚 Обработка " << total_docs << " документов...\n";
        std::cout << std::string(60, '=') << "\n";
        
        // Создаём токенизатор
        Tokenizer::Config tok_config;
        tok_config.min_length = 2;
        tok_config.remove_numbers = true;
        tok_config.remove_stopwords = true;
        tok_config.apply_stemming = true;
        
        Tokenizer tokenizer(tok_config);
        TokenizerStats stats;
        
        // Засекаем время
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Обрабатываем документы
        db_client.for_each_document([&](const Document& doc) {
            stats.total_documents++;
            
            if (doc.html_content.empty()) return;
            
            stats.total_bytes += doc.html_content.size();
            
            // Токенизация и стемминг
            auto [tokens, stems] = tokenizer.process_html(doc.html_content);
            
            stats.total_tokens += tokens.size();
            
            for (const auto& token : tokens) {
                stats.token_freq[token]++;
            }
            for (const auto& stem : stems) {
                stats.stem_freq[stem]++;
            }
            
            // Прогресс
            if (stats.total_documents % 100 == 0) {
                auto now = std::chrono::high_resolution_clock::now();
                double elapsed = std::chrono::duration<double>(now - start_time).count();
                double speed = stats.total_documents / elapsed;
                
                std::cout << "  [" << stats.total_documents << "/" << total_docs << "] "
                          << "токенов: " << stats.total_tokens << ", "
                          << "скорость: " << std::fixed << std::setprecision(1) 
                          << speed << " док/сек\n";
            }
        }, limit);
        
        // Завершаем замер времени
        auto end_time = std::chrono::high_resolution_clock::now();
        stats.processing_time_sec = std::chrono::duration<double>(end_time - start_time).count();
        stats.unique_tokens = stats.token_freq.size();
        stats.unique_stems = stats.stem_freq.size();
        
        // Выводим статистику
        print_statistics(stats);
        
        // Сохраняем статистику
        save_statistics(stats, "tokenization_stats.txt");
        
        // Строим данные для закона Ципфа
        if (!stats.stem_freq.empty()) {
            std::cout << "\n📈 Анализ закона Ципфа...\n";
            
            // Сохраняем данные для графика
            ZipfAnalyzer::save_plot_data(stats.stem_freq, "zipf_data.tsv");
            
            // Сортируем частоты
            std::vector<size_t> frequencies;
            for (const auto& [stem, count] : stats.stem_freq) {
                frequencies.push_back(count);
            }
            std::sort(frequencies.begin(), frequencies.end(), std::greater<>());
            
            // Подбираем параметры Мандельброта
            auto params = ZipfAnalyzer::fit_mandelbrot(frequencies);
            
            std::cout << "\n🔢 Параметры закона Мандельброта:\n";
            std::cout << "   B (показатель степени) = " << std::fixed << std::setprecision(3) << params.B << "\n";
            std::cout << "   P (коэффициент) = " << std::fixed << std::setprecision(3) << params.P << "\n";
            std::cout << "   ρ (rho, сдвиг) = " << std::fixed << std::setprecision(3) << params.rho << "\n";
            
            // Генерируем скрипт gnuplot
            ZipfAnalyzer::generate_gnuplot_script(
                "zipf_data.tsv",
                "zipf_plot.png",
                "Закон Ципфа (стемы)",
                stats.total_tokens,
                params
            );
            
            // Анализ расхождения
            auto zipf_theoretical = ZipfAnalyzer::calculate_zipf(
                frequencies.size(), stats.total_tokens);
            ZipfAnalyzer::analyze_deviation(frequencies, zipf_theoretical);
        }
        
        std::cout << "\n✅ Обработка завершена!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}