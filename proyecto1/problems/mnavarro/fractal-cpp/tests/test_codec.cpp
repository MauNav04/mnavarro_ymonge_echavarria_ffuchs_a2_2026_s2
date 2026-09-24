#include "fractal/codec.hpp"
#include "file_io.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
void require(bool b) { if(!b) throw std::runtime_error("Prueba fallida"); }
int main() {
    try {
        fractal::Image a{32,32,std::vector<float>(1024,0.37f)};
        auto c=fractal::encode(a);
        require(fractal::mse(a,fractal::decode(c))<1e-12);
        for(int y=0;y<32;++y) for(int x=0;x<32;++x) a.at(x,y)=float(x+y)/62;
        c=fractal::encode(a,{4,4});
        auto b=fractal::decode(c,100);
        require(fractal::mse(a,b)<0.02);
        require(fractal::mse(b,fractal::decode(c,120))<1e-10);
        auto path=std::filesystem::temp_directory_path()/"fractal_test_roundtrip.frc";
        saveCode(path.string(),c);
        auto loaded=loadCode(path.string());
        require(fractal::mse(b,fractal::decode(loaded,100))==0);
        {std::ofstream f(path,std::ios::binary); f << "FRC1";}
        bool rejected=false;
        try {loadCode(path.string());} catch(const std::exception&) {rejected=true;}
        require(rejected); std::filesystem::remove(path);
        c.rules[0].x=9999; rejected=false;
        try {fractal::decode(c);} catch(const std::exception&) {rejected=true;}
        require(rejected);
        std::cout << "OK: constante, gradiente, convergencia, archivo, truncado y coordenadas.\n";
    } catch(const std::exception& e) {std::cerr << e.what() << '\n'; return 1;}
}
