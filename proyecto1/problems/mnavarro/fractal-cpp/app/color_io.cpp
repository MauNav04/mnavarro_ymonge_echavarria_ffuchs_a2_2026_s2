#include "color_io.hpp"
#include "file_io.hpp"
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
namespace {
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"IEEE754 binary32 requerido");
void put(std::ostream& f,std::uint32_t n) { for(int k=0;k<4;++k) f.put(static_cast<char>((n>>(k*8))&255)); }
std::uint32_t get(std::istream& f) {
    std::uint32_t n=0;
    for(int k=0;k<4;++k) { int b=f.get(); if(b==EOF) throw std::runtime_error("FRC2 truncado."); n|=static_cast<std::uint32_t>(b)<<(k*8); }
    return n;
}
void putFloat(std::ostream& f,float v) {std::uint32_t b; std::memcpy(&b,&v,4); put(f,b);}
float getFloat(std::istream& f) {auto b=get(f); float v; std::memcpy(&v,&b,4); return v;}
}
void saveMultiCode(const std::string& path,const fractal::MultiEncoded& c) {
    fractal::validate(c);
    // Preserva FRC1 para gris. RGB usa FRC2.
    if(c.planes.size()==1) {saveCode(path,c.planes[0]); return;}
    std::ofstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error("No se pudo abrir la salida FRC2.");
    const auto& first=c.planes[0];
    f.write("FRC2",4); put(f,first.width); put(f,first.height); put(f,first.range);
    put(f,3); put(f,static_cast<std::uint32_t>(first.rules.size()));
    for(const auto& p:c.planes) for(const auto& r:p.rules) {
        put(f,r.x); put(f,r.y); f.put(static_cast<char>(r.symmetry)); putFloat(f,r.contrast); putFloat(f,r.brightness);
    }
    f.flush(); if(!f) throw std::runtime_error("Error escribiendo FRC2.");
}
fractal::MultiEncoded loadMultiCode(const std::string& path) {
    std::ifstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error("No se pudo abrir el archivo.");
    char magic[4]; f.read(magic,4);
    if(!f) throw std::runtime_error("Cabecera truncada.");
    if(std::memcmp(magic,"FRC1",4)==0) {f.close(); return {{loadCode(path)}};}
    if(std::memcmp(magic,"FRC2",4)!=0) throw std::runtime_error("Formato desconocido; se espera FRC1 o FRC2.");
    auto w=get(f),h=get(f),r=get(f),channels=get(f),n=get(f);
    if(r<2 || r>64 || w<2*r || h<2*r || w>4096 || h>4096 || w%r || h%r || channels!=3 || n!=(w/r)*(h/r))
        throw std::runtime_error("Cabecera FRC2 invalida.");
    const auto start=f.tellg(); f.seekg(0,std::ios::end);
    if(f.tellg()!=std::streampos(24ULL+17ULL*channels*n)) throw std::runtime_error("Longitud FRC2 incorrecta.");
    f.seekg(start);
    fractal::MultiEncoded c;
    for(unsigned channel=0;channel<channels;++channel) {
        fractal::Encoded p{static_cast<int>(w),static_cast<int>(h),static_cast<int>(r),{}};
        p.rules.reserve(n);
        for(unsigned i=0;i<n;++i) {
            fractal::Rule rule; rule.x=get(f); rule.y=get(f);
            int s=f.get(); if(s==EOF) throw std::runtime_error("Regla truncada.");
            rule.symmetry=static_cast<std::uint8_t>(s); rule.contrast=getFloat(f); rule.brightness=getFloat(f);
            p.rules.push_back(rule);
        }
        c.planes.push_back(std::move(p));
    }
    fractal::validate(c); return c;
}
