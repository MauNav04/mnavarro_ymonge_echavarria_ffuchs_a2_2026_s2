#include "fractal/color.hpp"
#include "color_io.hpp"
#include "file_io.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
void require(bool ok) {if(!ok) throw std::runtime_error("Prueba RGB fallida.");}
template<class F> void rejects(F f) {bool failed=false; try{f();}catch(const std::exception&){failed=true;} require(failed);}
int main() {
 try {
    fractal::MultiImage a;
    for(float v:{0.8f,0.3f,0.1f}) a.planes.push_back({32,32,std::vector<float>(1024,v)});
    auto c=fractal::encode(a,{4,4}); auto b=fractal::decode(c,80);
    require(fractal::mse(a,b)<1e-12); // Incluye distincion rojo/azul.
    for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
        a.planes[0].at(x,y)=x/31.0f;
        a.planes[1].at(x,y)=y/31.0f;
        a.planes[2].at(x,y)=(x+y)/62.0f;
    }
    c=fractal::encode(a,{4,4}); b=fractal::decode(c,100);
    require(fractal::mse(a,b)<0.02);
    require(fractal::mse(b,fractal::decode(c,120))<1e-10);
    for(int channel=0;channel<3;++channel) {
        auto mono=fractal::encode(a.planes[channel],{4,4});
        require(fractal::mse(fractal::decode(mono,100),b.planes[channel])==0);
    }
    auto path=std::filesystem::temp_directory_path()/"fractal_rgb_test.frc";
    saveMultiCode(path.string(),c);
    require(std::filesystem::file_size(path)==24+51*c.planes[0].rules.size());
    require(fractal::mse(b,fractal::decode(loadMultiCode(path.string()),100))==0);
    {std::ofstream f(path,std::ios::binary|std::ios::app); f.put('x');}
    rejects([&]{loadMultiCode(path.string());});
    {std::ofstream f(path,std::ios::binary); f<<"FRC2";}
    rejects([&]{loadMultiCode(path.string());});
    saveCode(path.string(),c.planes[0]);
    auto gray=loadMultiCode(path.string()); require(gray.planes.size()==1);
    require(fractal::mse(fractal::decode(gray,100).planes[0],b.planes[0])==0);
    std::filesystem::remove(path);
    c.planes[0].rules[0].symmetry=8; rejects([&]{fractal::decode(c);});
    a.planes.pop_back(); rejects([&]{fractal::encode(a);});
    std::cout<<"OK RGB: canales distintos, gradientes, convergencia, busquedas independientes, FRC2, FRC1, archivos invalidos.\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
