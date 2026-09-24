#include "fractal/codec.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace fractal {
namespace {
void dimensions(int w, int h, int r) {
    if (r < 2 || r > 64 || w < 2*r || h < 2*r || w > 4096 || h > 4096 || w%r || h%r)
        throw std::invalid_argument("Dimensiones invalidas: lados multiplos del range, >= 2*range y <=4096; range entre 2 y 64.");
}
void checkImage(const Image& a) {
    if (a.width <= 0 || a.height <= 0 || a.pixels.size() != static_cast<std::size_t>(a.width)*a.height)
        throw std::invalid_argument("Matriz de imagen invalida.");
    for (float v : a.pixels)
        if (!std::isfinite(v) || v < 0 || v > 1) throw std::invalid_argument("Intensidades fuera de [0,1].");
}
// Promedio de cada grupo 2x2. Lee un domain de lado 2*r.
std::vector<float> reduce(const Image& a, int x, int y, int r) {
    std::vector<float> out(r*r);
    for (int j=0; j<r; ++j) for (int i=0; i<r; ++i)
        out[j*r+i] = (a.at(x+2*i,y+2*j)+a.at(x+2*i+1,y+2*j)+
                      a.at(x+2*i,y+2*j+1)+a.at(x+2*i+1,y+2*j+1))*0.25f;
    return out;
}
// ID 0..3: rotaciones horarias. ID 4..7: reflexion horizontal y rotacion.
std::vector<float> transform(const std::vector<float>& a, int r, int symmetry) {
    std::vector<float> out(r*r);
    for (int y=0; y<r; ++y) for (int x=0; x<r; ++x) {
        int tx = symmetry >= 4 ? r-1-x : x, ty=y;
        for (int k=0; k<symmetry%4; ++k) { int old=tx; tx=r-1-ty; ty=old; }
        out[ty*r+tx]=a[y*r+x];
    }
    return out;
}
}
void validate(const Encoded& c) {
    dimensions(c.width,c.height,c.range);
    if (c.rules.size()!=static_cast<std::size_t>(c.width/c.range)*(c.height/c.range))
        throw std::invalid_argument("Cantidad de reglas incorrecta.");
    for (const auto& t:c.rules)
        if (t.x>static_cast<unsigned>(c.width-2*c.range) || t.y>static_cast<unsigned>(c.height-2*c.range) ||
            t.symmetry>7 || !std::isfinite(t.contrast) || std::abs(t.contrast)>0.900001f ||
            !std::isfinite(t.brightness) || std::abs(t.brightness)>2)
            throw std::invalid_argument("Regla invalida.");
}
Encoded encode(const Image& a, Config cfg) {
    checkImage(a); dimensions(a.width,a.height,cfg.range);
    if (cfg.stride<1 || cfg.stride>2*cfg.range) throw std::invalid_argument("Stride fuera de 1..2*range.");
    const int r=cfg.range, n=r*r;
    struct Domain { int x,y; std::vector<float> reduced; };
    std::vector<Domain> domains;
    for (int y=0; y<=a.height-2*r; y+=cfg.stride)
        for (int x=0; x<=a.width-2*r; x+=cfg.stride)
            domains.push_back({x,y,reduce(a,x,y,r)});
    Encoded result{a.width,a.height,r,{}};

    // PAR-01: cada range calcula una regla independiente.
    // Lectura compartida: imagen a, dominios precalculados y configuracion.
    // Privados por tarea: target, meanR, bestError, best y temporales de candidatos.

    for (int ry=0; ry<a.height; ry+=r) for (int rx=0; rx<a.width; rx+=r) {
        std::vector<float> target(n);
        double meanR=0;
        for (int y=0;y<r;++y) for(int x=0;x<r;++x) { target[y*r+x]=a.at(rx+x,ry+y); meanR+=target[y*r+x]; }
        meanR/=n;
        double bestError=std::numeric_limits<double>::infinity();
        Rule best;
        for (const auto& d:domains) for(int sym=0;sym<8;++sym) {
            auto block=transform(d.reduced,r,sym);
            double meanD=0;
            for(float v:block) meanD+=v;
            meanD/=n;
            double variance=0,covariance=0;
            for(int i=0;i<n;++i) { double delta=block[i]-meanD; variance+=delta*delta; covariance+=delta*(target[i]-meanR); }
            float s=static_cast<float>(variance<1e-12 ? 0 : std::clamp(covariance/variance,-0.9,0.9));
            float b=static_cast<float>(meanR-s*meanD);
            double error=0;
            for(int i=0;i<n;++i) { double e=target[i]-(s*block[i]+b); error+=e*e; }
            if(error<bestError) { bestError=error; best={static_cast<std::uint32_t>(d.x),static_cast<std::uint32_t>(d.y),static_cast<std::uint8_t>(sym),s,b}; }
        }

        // PAR-02: push_back no debe ejecutarse concurrentemente sobre el mismo vector.
        // Propuesta: resize(N_R) antes de iniciar hilos y escribir rules[rangeIndex].
        // rangeIndex = (ry / r) * (a.width / r) + (rx / r).

        result.rules.push_back(best);
    }
    return result;
}
Image decode(const Encoded& c, int iterations) {
    validate(c);
    if(iterations<1 || iterations>10000) throw std::invalid_argument("Iteraciones fuera de 1..10000.");
    Image current{c.width,c.height,std::vector<float>(static_cast<std::size_t>(c.width)*c.height,0.5f)};
    Image next=current;
    for(int t=0;t<iterations;++t) {
        std::size_t index=0;
        for(int y=0;y<c.height;y+=c.range) for(int x=0;x<c.width;x+=c.range) {
            const auto& rule=c.rules[index++];
            auto block=transform(reduce(current,rule.x,rule.y,c.range),c.range,rule.symmetry);
            for(int j=0;j<c.range;++j) for(int i=0;i<c.range;++i)
                next.at(x+i,y+j)=std::clamp(rule.contrast*block[j*c.range+i]+rule.brightness,0.0f,1.0f);
        }
        current.pixels.swap(next.pixels);
    }
    return current;
}
double mse(const Image& a,const Image& b) {
    checkImage(a); checkImage(b);
    if(a.width!=b.width || a.height!=b.height) throw std::invalid_argument("Dimensiones diferentes.");
    double error=0;
    for(std::size_t i=0;i<a.pixels.size();++i) { double d=a.pixels[i]-b.pixels[i]; error+=d*d; }
    return error/a.pixels.size();
}
}
