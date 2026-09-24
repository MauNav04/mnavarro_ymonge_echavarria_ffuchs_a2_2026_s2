#include "fractal/color.hpp"
#include <stdexcept>
namespace fractal {
namespace {
void count(std::size_t n) {
    if(n!=1 && n!=3) throw std::invalid_argument("Se requieren 1 canal gris o 3 canales RGB.");
}
void check(const MultiImage& a) {
    count(a.planes.size());
    for(const auto& p:a.planes) {
        if(p.width!=a.planes[0].width || p.height!=a.planes[0].height)
            throw std::invalid_argument("Los canales deben tener las mismas dimensiones.");
        (void)fractal::mse(p,p); // Valida dimensiones, tamaño y valores finitos [0,1].
    }
}
}
void validate(const MultiEncoded& c) {
    count(c.planes.size());
    for(const auto& p:c.planes) {
        fractal::validate(p);
        if(p.width!=c.planes[0].width || p.height!=c.planes[0].height || p.range!=c.planes[0].range)
            throw std::invalid_argument("Geometria inconsistente entre canales.");
    }
}
MultiEncoded encode(const MultiImage& a,Config cfg) {
    check(a);
    MultiEncoded c;
    // Tres busquedas completas, secuenciales e independientes: R, G y B.
    // Cada canal puede elegir un dominio y una simetria diferentes.

    // PAR-03: los canales RGB son independientes; este nivel ofrece hasta 3 tareas.
    // El diseno principal reparte ranges dentro de cada canal y procesa canales en orden.
    for(const auto& p:a.planes) c.planes.push_back(fractal::encode(p,cfg));
    return c;
}
MultiImage decode(const MultiEncoded& c,int iterations) {
    validate(c); MultiImage a;

    // PAR-04: leer solo current y escribir ranges disjuntos de next.
    // Completar todos los ranges antes de intercambiar buffers e iniciar otra iteracion.
    for(const auto& p:c.planes) a.planes.push_back(fractal::decode(p,iterations));
    return a;
}
double mse(const MultiImage& a,const MultiImage& b) {
    check(a); check(b);
    if(a.planes.size()!=b.planes.size()) throw std::invalid_argument("Numero de canales diferente.");
    double e=0;
    for(std::size_t i=0;i<a.planes.size();++i) e+=fractal::mse(a.planes[i],b.planes[i]);
    return e/a.planes.size();
}
}
