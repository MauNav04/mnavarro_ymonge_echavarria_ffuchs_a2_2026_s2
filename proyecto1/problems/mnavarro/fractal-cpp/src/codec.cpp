#include "fractal/codec.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace fractal {
namespace {
// Nucleo secuencial para un canal normalizado [0,1]. Las marcas PAR son propuestas.
// Un range es un destino r*r; un domain es una fuente de lado 2*r.
// Exige ranges completos y espacio para al menos un dominio. Las primeras
// condiciones descartan un r invalido antes de evaluar los modulos.
void dimensions(int w, int h, int r) {
    if (r < 2 || r > 64 || w < 2*r || h < 2*r || w > 4096 || h > 4096 || w%r || h%r)
        throw std::invalid_argument("Dimensiones invalidas: lados multiplos del range, >= 2*range y <=4096; range entre 2 y 64.");
}
// Valida almacenamiento e intensidades; la geometria fractal se comprueba aparte.
void checkImage(const Image& a) {
    if (a.width <= 0 || a.height <= 0 || a.pixels.size() != static_cast<std::size_t>(a.width)*a.height)
        throw std::invalid_argument("Matriz de imagen invalida.");
    for (float v : a.pixels)
        if (!std::isfinite(v) || v < 0 || v > 1) throw std::invalid_argument("Intensidades fuera de [0,1].");
}
// Promedio de cada grupo 2x2. Lee un domain de lado 2*r.
// Devuelve r*r muestras por filas, adaptando la fuente al tamano del range.
// El llamador garantiza que el dominio completo cabe en la imagen.
std::vector<float> reduce(const Image& a, int x, int y, int r) {
    std::vector<float> out(r*r);
    for (int j=0; j<r; ++j) for (int i=0; i<r; ++i)
        out[j*r+i] = (a.at(x+2*i,y+2*j)+a.at(x+2*i+1,y+2*j)+
                      a.at(x+2*i,y+2*j+1)+a.at(x+2*i+1,y+2*j+1))*0.25f;
    return out;
}
// ID 0..3: rotaciones horarias. ID 4..7: reflexion horizontal y rotacion.
// Con y creciendo hacia abajo, (x,y) -> (r-1-y,x) gira 90 grados horarios.
// Para ID >= 4 se invierte primero x y luego se aplican ID%4 giros.
// Conserva la entrada y devuelve un bloque nuevo con la misma geometria.
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
// Exige una regla por range y fuentes 2*range completamente dentro de la imagen.
// El destino es implicito en el indice de la regla, en orden por filas.
// El contraste contractivo admite una pequena tolerancia numerica sobre 0.9.
// El stride solo interviene en la busqueda; no hace falta para reconstruir.
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
// Busca exhaustivamente una regla afin R ~= s*D+b por range. Por canal:
// N_R=(W/r)*(H/r), N_D=(floor((W-2*r)/stride)+1)*(floor((H-2*r)/stride)+1).
// Costo de busqueda: O(N_R*N_D*8*r*r); RGB repite el trabajo en tres planos.
Encoded encode(const Image& a, Config cfg) {
    checkImage(a); dimensions(a.width,a.height,cfg.range);
    if (cfg.stride<1 || cfg.stride>2*cfg.range) throw std::invalid_argument("Stride fuera de 1..2*range.");
    const int r=cfg.range, n=r*r;
    struct Domain { int x,y; std::vector<float> reduced; };
    std::vector<Domain> domains;
    // Precalcula dominios reducidos y conserva sus coordenadas originales.
    // Pueden solaparse. El catalogo terminado se comparte solo para lectura
    // entre los equipos que procesen ranges del mismo canal.
    for (int y=0; y<=a.height-2*r; y+=cfg.stride)
        for (int x=0; x<=a.width-2*r; x+=cfg.stride)
            domains.push_back({x,y,reduce(a,x,y,r)});
    Encoded result{a.width,a.height,r,{}};

    // =========[Punto de paralelizacion 01]=========
    // Cada range calcula una regla independiente.
    // Lectura compartida: imagen a, dominios precalculados y configuracion.
    // Privados por tarea: target, meanR, bestError, best y temporales de candidatos.

    // En el reparto por equipos del README, target y meanR pertenecen al range
    // y pueden compartirse para lectura dentro del equipo tras prepararlos.
    // Cada hilo que explore dominios necesita su propio minimo local.
    // ========================================
    for (int ry=0; ry<a.height; ry+=r) for (int rx=0; rx<a.width; rx+=r) {
        // Copia el destino y calcula su media una sola vez para todos los candidatos.
        std::vector<float> target(n);
        double meanR=0;
        for (int y=0;y<r;++y) for(int x=0;x<r;++x) { target[y*r+x]=a.at(rx+x,ry+y); meanR+=target[y*r+x]; }
        meanR/=n;
        double bestError=std::numeric_limits<double>::infinity();
        Rule best;
        // =========[Punto de paralelizacion 02]=========
        // Repartir dominios entre hilos del equipo y combinar minimos locales.
        // block, meanD, variance, covariance, s, b, error y la pareja (bestError, best)
        // deben ser privados por hilo. En empates, conservar el primer candidato en
        // orden dominio/simetria, como hace el operador < en la busqueda secuencial.
        // ========================================
        for (const auto& d:domains) for(int sym=0;sym<8;++sym) {
            auto block=transform(d.reduced,r,sym);
            double meanD=0;
            for(float v:block) meanD+=v;
            meanD/=n;
            // Sumas centradas sin dividir por n: ese factor se cancela en cov/var.
            double variance=0,covariance=0;
            for(int i=0;i<n;++i) { double delta=block[i]-meanD; variance+=delta*delta; covariance+=delta*(target[i]-meanR); }
            // Minimos cuadrados con contraste limitado a [-0.9,0.9] para mantener
            // contractividad. Un dominio casi constante usa s=0 y b=meanR.
            float s=static_cast<float>(variance<1e-12 ? 0 : std::clamp(covariance/variance,-0.9,0.9));
            float b=static_cast<float>(meanR-s*meanD);
            // SSE sin recorte a [0,1]; minimizarla equivale a minimizar MSE porque
            // todos los bloques tienen n muestras. decode si recorta la salida.
            double error=0;
            for(int i=0;i<n;++i) { double e=target[i]-(s*block[i]+b); error+=e*e; }
            if(error<bestError) { bestError=error; best={static_cast<std::uint32_t>(d.x),static_cast<std::uint32_t>(d.y),static_cast<std::uint8_t>(sym),s,b}; }
        }

        // =========[Punto de paralelizacion 03]=========
        // push_back no debe ejecutarse concurrentemente sobre el mismo vector.
        // Propuesta: resize(N_R) antes de iniciar hilos y escribir rules[rangeIndex].
        // rangeIndex = (ry / r) * (a.width / r) + (rx / r).

        // reserve no crea elementos: se requiere resize para asignar por indice.
        // Cada equipo publicaria una regla tras reducir sus minimos locales.
        // ========================================
        result.rules.push_back(best);
    }
    return result;
}
// Reconstruye desde gris uniforme (0.5) sin necesitar la imagen original.
// Ejecuta un numero fijo de iteraciones; no comprueba un umbral de convergencia.
Image decode(const Encoded& c, int iterations) {
    validate(c);
    if(iterations<1 || iterations>10000) throw std::invalid_argument("Iteraciones fuera de 1..10000.");
    Image current{c.width,c.height,std::vector<float>(static_cast<std::size_t>(c.width)*c.height,0.5f)};
    Image next=current;
    // Doble buffer: todas las fuentes se leen de la iteracion anterior.
    // Cada iteracion sobrescribe por completo next con ranges sin solapamiento.
    for(int t=0;t<iterations;++t) {
        std::size_t index=0;
        // =========[Punto de paralelizacion 04]=========
        // (punto efectivo; ver color.cpp): current y reglas compartidos para
        // lectura; cada tarea escribe un range disjunto de next. Sustituir index++
        // por (y/c.range)*(c.width/c.range)+(x/c.range). block y los indices de
        // recorrido deben ser privados por tarea.
        // ========================================
        for(int y=0;y<c.height;y+=c.range) for(int x=0;x<c.width;x+=c.range) {
            const auto& rule=c.rules[index++];
            auto block=transform(reduce(current,rule.x,rule.y,c.range),c.range,rule.symmetry);
            for(int j=0;j<c.range;++j) for(int i=0;i<c.range;++i)
                next.at(x+i,y+j)=std::clamp(rule.contrast*block[j*c.range+i]+rule.brightness,0.0f,1.0f);
        }
        // En paralelo: barrera o join antes del swap. Un solo hilo intercambia los
        // buffers y todos deben ver el cambio antes de la siguiente iteracion.
        current.pixels.swap(next.pixels);
    }
    return current;
}
// Error cuadratico medio de muestras normalizadas (no bytes 0..255).
// Devuelve un valor en [0,1]; cero indica igualdad de las muestras.
double mse(const Image& a,const Image& b) {
    checkImage(a); checkImage(b);
    if(a.width!=b.width || a.height!=b.height) throw std::invalid_argument("Dimensiones diferentes.");
    double error=0;
    for(std::size_t i=0;i<a.pixels.size();++i) { double d=a.pixels[i]-b.pixels[i]; error+=d*d; }
    return error/a.pixels.size();
}
}
