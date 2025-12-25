#include "zipf_analyzer.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cmath>
#include <numeric>

namespace wiki {

std::vector<double> ZipfAnalyzer::calculate_zipf(
    size_t num_ranks, 
    size_t total_tokens, 
    double s
) {
    std::vector<double> result(num_ranks);
    
    // Вычисляем нормализующую константу: C = N / sum(1/r^s)
    double harmonic = 0.0;
    for (size_t r = 1; r <= num_ranks; ++r) {
        harmonic += 1.0 / std::pow(r, s);
    }
    
    double C = total_tokens / harmonic;
    
    for (size_t r = 1; r <= num_ranks; ++r) {
        result[r - 1] = C / std::pow(r, s);
    }
    
    return result;
}

std::vector<double> ZipfAnalyzer::calculate_mandelbrot(
    size_t num_ranks,
    size_t total_tokens,
    const MandelbrotParams& params
) {
    std::vector<double> result(num_ranks);
    
    double sum = 0.0;
    for (size_t r = 1; r <= num_ranks; ++r) {
        double val = params.P / std::pow(r + params.rho, params.B);
        result[r - 1] = val;
        sum += val;
    }
    
    // Нормализуем
    double scale = total_tokens / sum;
    for (auto& val : result) {
        val *= scale;
    }
    
    return result;
}

ZipfAnalyzer::MandelbrotParams ZipfAnalyzer::fit_mandelbrot(
    const std::vector<size_t>& frequencies
) {
    MandelbrotParams best_params;
    double best_error = std::numeric_limits<double>::max();
    
    // Простой grid search для подбора параметров
    for (double B = 0.8; B <= 1.5; B += 0.05) {
        for (double rho = 1.0; rho <= 5.0; rho += 0.2) {
            size_t total = std::accumulate(frequencies.begin(), frequencies.end(), size_t(0));
            auto theoretical = calculate_mandelbrot(frequencies.size(), total, {B, 1.0, rho});
            
            // Вычисляем ошибку (log-scale MSE)
            double error = 0.0;
            size_t n = std::min(size_t(1000), frequencies.size());
            
            for (size_t i = 0; i < n; ++i) {
                double log_emp = std::log(frequencies[i] + 1);
                double log_theo = std::log(theoretical[i] + 1);
                error += (log_emp - log_theo) * (log_emp - log_theo);
            }
            error /= n;
            
            if (error < best_error) {
                best_error = error;
                best_params.B = B;
                best_params.rho = rho;
                best_params.P = frequencies[0] * std::pow(1 + rho, B);
            }
        }
    }
    
    return best_params;
}

void ZipfAnalyzer::save_plot_data(
    const std::unordered_map<std::string, size_t>& freq_map,
    const std::string& output_path
) {
    // Сортируем по частоте
    std::vector<std::pair<std::string, size_t>> sorted_freq(freq_map.begin(), freq_map.end());
    std::sort(sorted_freq.begin(), sorted_freq.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Сохраняем данные
    std::ofstream file(output_path);
    if (!file.is_open()) {
        std::cerr << "Ошибка открытия файла: " << output_path << std::endl;
        return;
    }
    
    file << "# Rank\tFrequency\tTerm\n";
    
    for (size_t i = 0; i < sorted_freq.size(); ++i) {
        file << (i + 1) << "\t" << sorted_freq[i].second << "\t" << sorted_freq[i].first << "\n";
    }
    
    file.close();
    std::cout << "📊 Данные сохранены: " << output_path << std::endl;
}

void ZipfAnalyzer::generate_gnuplot_script(
    const std::string& data_path,
    const std::string& output_image,
    const std::string& title,
    size_t total_tokens,
    const MandelbrotParams& params
) {
    std::string script_path = data_path + ".gnuplot";
    
    std::ofstream script(script_path);
    if (!script.is_open()) {
        std::cerr << "Ошибка создания скрипта gnuplot" << std::endl;
        return;
    }
    
    // Вычисляем константу для закона Ципфа
    // Примерно: C ≈ f(1) * 1^s = f(1)
    
    script << "set terminal png size 1200,800 enhanced font 'Arial,12'\n";
    script << "set output '" << output_image << "'\n";
    script << "set title '" << title << "'\n";
    script << "set xlabel 'Ранг (log)'\n";
    script << "set ylabel 'Частота (log)'\n";
    script << "set logscale xy\n";
    script << "set grid\n";
    script << "set key top right\n";
    script << "\n";
    
    // Константа Ципфа (приближённо)
    script << "# Закон Ципфа: f(r) = C / r^s, s=1\n";
    script << "zipf(x) = " << total_tokens << " / (1.78 * x)\n";
    script << "\n";
    
    // Закон Мандельброта
    script << "# Закон Мандельброта: f(r) = P / (r + rho)^B\n";
    script << "B = " << params.B << "\n";
    script << "rho = " << params.rho << "\n";
    script << "P = " << params.P << "\n";
    script << "mandelbrot(x) = P / (x + rho)**B\n";
    script << "\n";
    
    script << "plot '" << data_path << "' using 1:2 with points pt 7 ps 0.3 lc rgb 'blue' title 'Эмпирические данные', \\\n";
    script << "     zipf(x) with lines lw 2 lc rgb 'red' title 'Закон Ципфа (s=1)', \\\n";
    script << "     mandelbrot(x) with lines lw 2 lc rgb 'green' title sprintf('Мандельброт (B=%.2f, rho=%.2f)', B, rho)\n";
    
    script.close();
    
    std::cout << "📝 Gnuplot скрипт: " << script_path << std::endl;
    std::cout << "   Для построения графика выполните: gnuplot " << script_path << std::endl;
}

void ZipfAnalyzer::analyze_deviation(
    const std::vector<size_t>& empirical,
    const std::vector<double>& theoretical
) {
    std::cout << "\n📈 Анализ расхождения с законом Ципфа:\n";
    std::cout << std::string(60, '=') << "\n";
    
    struct Zone {
        std::string name;
        size_t start;
        size_t end;
    };
    
    std::vector<Zone> zones = {
        {"Топ-10", 0, 10},
        {"Топ 10-100", 10, 100},
        {"Средние (100-1000)", 100, 1000},
        {"Редкие (1000+)", 1000, empirical.size()}
    };
    
    for (const auto& zone : zones) {
        if (zone.start >= empirical.size()) continue;
        
        size_t end = std::min(zone.end, empirical.size());
        
        double rel_error = 0.0;
        double sum_emp = 0.0;
        double sum_theo = 0.0;
        
        for (size_t i = zone.start; i < end; ++i) {
            double emp = static_cast<double>(empirical[i]);
            double theo = theoretical[i];
            
            if (theo > 0) {
                rel_error += std::abs(emp - theo) / theo;
            }
            sum_emp += emp;
            sum_theo += theo;
        }
        
        rel_error = rel_error / (end - zone.start) * 100;
        
        std::string direction = (sum_emp > sum_theo) ? "выше" : "ниже";
        
        std::cout << "  " << zone.name << ": отклонение " 
                  << std::fixed << std::setprecision(1) << rel_error 
                  << "%, " << direction << " теоретического\n";
    }
    
    std::cout << "\n  Причины расхождения:\n";
    std::cout << "  • Топ: частые слова (предлоги, союзы) встречаются чаще\n";
    std::cout << "  • Середина: хорошее соответствие закону\n";
    std::cout << "  • Хвост: редкие слова, опечатки, имена\n";
    std::cout << "  • Специфика корпуса: тематические термины (музыканты)\n";
}

} // namespace wiki