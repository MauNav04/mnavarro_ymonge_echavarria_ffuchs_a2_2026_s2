#include "file_io.hpp"
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
namespace {
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"Requiere float IEEE754 de 32 bits");
void put(std::ostream& f,std::uint32_t v) { for(int i=0;i<4;++i) f.put(static_cast<char>((v>>(8*i))&255)); }
std::uint32_t get(std::istream& f) {
    std::uint32_t v=0;
    for(int i=0;i<4;++i) { int c=f.get(); if(c==EOF) throw std::runtime_error("Archivo truncado."); v|=static_cast<std::uint32_t>(c)<<(8*i); }
    return v;
}
void putFloat(std::ostream& f,float v) { std::uint32_t bits; std::memcpy(&bits,&v,4); put(f,bits); }
float getFloat(std::istream& f) { auto bits=get(f); float v; std::memcpy(&v,&bits,4); return v; }
}
void saveCode(const std::string& path,const fractal::Encoded& c) {
    fractal::validate(c);
    std::ofstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error("No se pudo abrir la salida.");
    f.write("FRC1",4); put(f,c.width); put(f,c.height); put(f,c.range); put(f,static_cast<std::uint32_t>(c.rules.size()));
    for(const auto& r:c.rules) { put(f,r.x); put(f,r.y); f.put(static_cast<char>(r.symmetry)); putFloat(f,r.contrast); putFloat(f,r.brightness); }
    f.flush(); if(!f) throw std::runtime_error("Error escribiendo archivo.");
}
fractal::Encoded loadCode(const std::string& path) {
    std::ifstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error("No se pudo abrir el archivo comprimido.");
    char magic[4]; f.read(magic,4);
    if(!f || std::memcmp(magic,"FRC1",4)) throw std::runtime_error("Formato incorrecto.");
    auto w=get(f),h=get(f),r=get(f),count=get(f);
    if(r<2 || r>64 || w<2*r || h<2*r || w>4096 || h>4096 || w%r || h%r || count!=(w/r)*(h/r))
        throw std::runtime_error("Cabecera invalida.");
    fractal::Encoded c{static_cast<int>(w),static_cast<int>(h),static_cast<int>(r),{}};
    for(std::uint32_t i=0;i<count;++i) {
        fractal::Rule t; t.x=get(f); t.y=get(f);
        int s=f.get(); if(s==EOF) throw std::runtime_error("Archivo truncado.");
        t.symmetry=static_cast<std::uint8_t>(s); t.contrast=getFloat(f); t.brightness=getFloat(f); c.rules.push_back(t);
    }
    if(f.peek()!=EOF) throw std::runtime_error("Datos extra al final del archivo.");
    fractal::validate(c); return c;
}
