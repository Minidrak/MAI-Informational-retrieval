#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace wiki {

/**
 * Анализатор закона Ципфа
 */
class ZipfAnalyzer {
public:
    struct MandelbrotParams {
        double B = 1.0;    // Показатель степени
        double P = 1.0;    // Коэффициент
        double rho = 2.7;  // Сдвиг
    };
    
    /**
     * Рассчитывает теоретические частоты по закону Ципфа
     * f(r) = C / r^s
     */
    static std::vector<double> calculate_zipf(
        size_t num_ranks, 
        size_t total_tokens, 
        double s = 1.0
    );
    
    /**
     * Рассчитывает частоты по закону Мандельброта
     * f(r) = P / (r + rho)^B
     */
    static std::vector<double> calculate_mandelbrot(
        size_t num_ranks,
        size_t total_tokens,
        const MandelbrotParams& params
    );
    
    /**
     * Подбирает параметры Мандельброта
     */
    static MandelbrotParams fit_mandelbrot(
        const std::vector<size_t>& frequencies
    );
    
    /**
     * Сохраняет данные для построения графика (формат для gnuplot)
     */
    static void save_plot_data(
        const std::unordered_map<std::string, size_t>& freq_map,
        const std::string& output_path
    );
    
    /**
     * Генерирует gnuplot скрипт
     */
    static void generate_gnuplot_script(
        const std::string& data_path,
        const std::string& output_image,
        const std::string& title,
        size_t total_tokens,
        const MandelbrotParams& params
    );
    
    /**
     * Анализирует расхождение с законом Ципфа
     */
    static void analyze_deviation(
        const std::vector<size_t>& empirical,
        const std::vector<double>& theoretical
    );
};

} // namespace wiki