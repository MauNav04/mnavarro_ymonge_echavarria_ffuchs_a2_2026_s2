#include "fractal/color.hpp"
#include <stdexcept>
namespace fractal {
namespace {
// Coordinacion de un plano gris o tres planos R, G, B, sin alfa.
// La conversion BGR/RGB y la serializacion FRC1/FRC2 se realizan en app/.
// Las marcas PAR son propuestas; los recorridos actuales son secuenciales.
// Rechazar cero planos tambien hace seguro acceder a planes[0] al validar.
void count(std::size_t n) {
    if(n!=1 && n!=3) throw std::invalid_argument("Se requieren 1 canal gris o 3 canales RGB.");
}
// Exige igual geometria y muestras normalizadas validas en todos los planos.
// mse(p,p) reutiliza la validacion monocanal; su resultado se descarta, aunque
// la llamada recorre los pixeles. La geometria fractal se valida al codificar.
void check(const MultiImage& a) {
    count(a.planes.size());
    for(const auto& p:a.planes) {
        if(p.width!=a.planes[0].width || p.height!=a.planes[0].height)
            throw std::invalid_argument("Los canales deben tener las mismas dimensiones.");
        (void)fractal::mse(p,p); // Valida dimensiones, tamaño y valores finitos [0,1].
    }
}
}
// Valida cada conjunto de reglas y exige igual ancho, alto y range.
// Los dominios, simetrias, contrastes y brillos pueden diferir entre canales.
void validate(const MultiEncoded& c) {
    count(c.planes.size());
    for(const auto& p:c.planes) {
        fractal::validate(p);
        if(p.width!=c.planes[0].width || p.height!=c.planes[0].height || p.range!=c.planes[0].range)
            throw std::invalid_argument("Geometria inconsistente entre canales.");
    }
}
// Conserva el orden de los planos y aplica la misma configuracion a cada uno.
// Cada plano construye su propio catalogo de dominios porque tiene muestras
// distintas. En modo gris solo se realiza una busqueda.
MultiEncoded encode(const MultiImage& a,Config cfg) {
    check(a);
    MultiEncoded c;
    // Tres busquedas completas, secuenciales e independientes: R, G y B.
    // Cada canal puede elegir un dominio y una simetria diferentes.

    // =========[Punto de paralelizacion 05]=========
    // los canales RGB son independientes; este nivel ofrece hasta 3 tareas.
    // El diseno principal reparte ranges dentro de cada canal y procesa canales en orden.
    // Si se paraleliza PAR-03 como alternativa, dimensionar c.planes antes
    // y escribir por indice exclusivo; push_back concurrente no seria seguro.
    // ========================================

    for(const auto& p:a.planes) c.planes.push_back(fractal::encode(p,cfg));
    return c;
}
// Reconstruye cada canal con el mismo numero de iteraciones y conserva su orden.
// Cada llamada monocanal mantiene sus propios buffers de reconstruccion.
MultiImage decode(const MultiEncoded& c,int iterations) {
    validate(c); MultiImage a;
    for(const auto& p:c.planes) a.planes.push_back(fractal::decode(p,iterations));
    return a;
}
// Promedia los MSE de canales correspondientes. Al tener igual cantidad de
// pixeles, equivale al MSE global de todas las muestras normalizadas.
// La sobrecarga monocanal comprueba tambien las dimensiones entre a y b.
double mse(const MultiImage& a,const MultiImage& b) {
    check(a); check(b);
    if(a.planes.size()!=b.planes.size()) throw std::invalid_argument("Numero de canales diferente.");
    double e=0;
    for(std::size_t i=0;i<a.planes.size();++i) e+=fractal::mse(a.planes[i],b.planes[i]);
    return e/a.planes.size();
}
}
