#include "fractal/codec.hpp"
#include "file_io.hpp"
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
namespace
{

    // Conierte cadena de texto a entero.
    int number(const char *arg)
    {
        std::string s(arg);
        std::size_t used;
        int v = std::stoi(s, &used);
        if (used != s.size())
            throw std::invalid_argument("Numero invalido.");
        return v;
    }

    // Carga una imagen en escala de grises usando OpenCV, crea la estructura de datos fractal::Image y convierte los píxeles de 0..255 (bytes) a flotantes normalizados entre 0.0f y 1.0f
    fractal::Image readImage(const std::string &path)
    {
        auto mat = cv::imread(path, cv::IMREAD_GRAYSCALE);
        if (mat.empty())
            throw std::runtime_error("No se pudo leer la imagen.");
        fractal::Image a{mat.cols, mat.rows, std::vector<float>(mat.total())};
        for (int y = 0; y < mat.rows; ++y)
            for (int x = 0; x < mat.cols; ++x)
                a.at(x, y) = mat.at<unsigned char>(y, x) / 255.0f;
        return a;
    }

    void writeImage(const std::string &path, const fractal::Image &a)
    {
        cv::Mat mat(a.height, a.width, CV_8UC1);
        for (int y = 0; y < a.height; ++y)
            for (int x = 0; x < a.width; ++x)
                mat.at<unsigned char>(y, x) = cv::saturate_cast<unsigned char>(a.at(x, y) * 255);
        if (!cv::imwrite(path, mat))
            throw std::runtime_error("No se pudo guardar la imagen.");
    }

    void usage()
    {
        std::cerr << "Uso:\n  fractal compress entrada.png salida.frc [range=8] [stride=8]\n"
                  << "  fractal decompress entrada.frc salida.png [iteraciones=40]\n"
                  << "  fractal compare original.png reconstruida.png\n";
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc < 4)
        {
            usage();
            return 1;
        }
        std::string command = argv[1];
        if (std::string(argv[2]) == argv[3])
            throw std::invalid_argument("Entrada y salida deben ser diferentes.");
        auto start = std::chrono::steady_clock::now();
        if (command == "compress" && argc <= 6)
        {
            fractal::Config cfg;
            if (argc >= 5)
                cfg.range = number(argv[4]);
            if (argc >= 6)
                cfg.stride = number(argv[5]);
            auto a = readImage(argv[2]);
            auto c = fractal::encode(a, cfg);
            saveCode(argv[3], c);
            std::cout << "Reglas: " << c.rules.size() << "\nBytes FRC: " << 20 + 17 * c.rules.size()
                      << "\nRelacion respecto a gris sin comprimir: " << double(a.pixels.size()) / (20 + 17 * c.rules.size()) << ":1\n";
        }
        else if (command == "decompress" && argc <= 5)
        {
            auto c = loadCode(argv[2]);
            writeImage(argv[3], fractal::decode(c, argc == 5 ? number(argv[4]) : 40));
        }
        else if (command == "compare" && argc == 4)
        {
            double e = fractal::mse(readImage(argv[2]), readImage(argv[3]));
            std::cout << "MSE [0,1]: " << e << "\nPSNR: " << (e == 0 ? std::numeric_limits<double>::infinity() : 10 * std::log10(1 / e)) << " dB\n";
        }
        else
        {
            usage();
            return 1;
        }
        std::cout << "Tiempo: " << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
