#include <iostream>
#include <cstdint>
#include <vector>
#include <chrono>
#include <cmath>
#include <thread>
#include <iomanip>
#include <atomic>
#include <algorithm>

using Numero = uint64_t;

// ============================================================
// Estructura de resultados para comparar los modelos
// ============================================================
struct ResultadoModelo {
    std::string nombre;
    uint64_t cantidadPrimos = 0;
    double segundos = 0.0;
    double speedup = 0.0;
    double eficiencia = 0.0;
    uint64_t eventosContexto = 0; // Conmutaciones o stalls registrados
};

// ============================================================
// Primos iniciales precalculados <= 100
// ============================================================
std::vector<Numero> primosIniciales() {
    return {
        2, 3, 5, 7, 11, 13, 17, 19, 23, 29,
        31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
        73, 79, 83, 89, 97
    };
}

// ============================================================
// Test de es primo? 
// ============================================================
inline bool esPrimo(Numero n, const std::vector<Numero>& primos) {
    if (n < 2) return false;
    for (Numero p : primos) {
        if (p * p > n) break;
        if (n % p == 0) return false;
    }
    return true;
}

// Versión que contabiliza divisiones (útil para detectar stalls en Coarse-Grained)
inline bool esPrimoConConteo(Numero n, const std::vector<Numero>& primos, uint32_t& divisiones) {
    if (n < 2) return false;
    divisiones = 0;
    for (Numero p : primos) {
        divisiones++;
        if (p * p > n) break;
        if (n % p == 0) return false;
    }
    return true;
}

// Comprueba la seguridad de la tanda
bool tandaEsSegura(Numero inicio, Numero fin) {
    Numero raiz = static_cast<Numero>(std::sqrt(static_cast<long double>(fin)));
    return raiz <= inicio - 1;
}

// Calcula el fin de tanda más grande y seguro posible según los primos conocidos
Numero calcularFinSeguro(Numero inicio, Numero N, Numero tamanoTandaDeseado) {
    Numero maxFinSeguro = (inicio - 1) * (inicio - 1);
    Numero finPropuesto = inicio + tamanoTandaDeseado - 1;
    Numero fin = std::min(finPropuesto, maxFinSeguro);
    if (fin > N) fin = N;
    return fin;
}

// ============================================================
// MODELO 0: SECUENCIAL (Sin hilos)
// ============================================================
ResultadoModelo ejecutarSecuencial(Numero N, Numero tamanoTanda) {
    std::vector<Numero> primos = primosIniciales();
    if (N <= 100) {
        uint64_t c = 0;
        for (Numero p : primos) if (p <= N) c++;
        return {"Secuencial", c, 0.0, 1.0, 100.0, 0};
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    Numero inicio = 101;

    while (inicio <= N) {
        Numero fin = calcularFinSeguro(inicio, N, tamanoTanda);
        if (!tandaEsSegura(inicio, fin)) {
            std::cerr << "ERROR: tanda no segura: " << inicio << "-" << fin << "\n";
            exit(1);
        }

        Numero n = (inicio % 2 == 0) ? inicio + 1 : inicio;
        for (; n <= fin; n += 2) {
            if (esPrimo(n, primos)) primos.push_back(n);
        }
        inicio = fin + 1;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double seg = std::chrono::duration<double>(t1 - t0).count();
    return {"Secuencial (Base)", static_cast<uint64_t>(primos.size()), seg, 1.0, 100.0, 0};
}

// ============================================================
// MODELO 1: CMP (Chip Multiprocessing - Paralelismo Real)
// Hilos nativos mapeados 1:1 a núcleos físicos.
// ============================================================
ResultadoModelo ejecutarCMP(Numero N, Numero tamanoTanda, unsigned int numHilos) {
    std::vector<Numero> primos = primosIniciales();
    auto t0 = std::chrono::high_resolution_clock::now();
    Numero inicio = 101;

    while (inicio <= N) {
        Numero fin = calcularFinSeguro(inicio, N, tamanoTanda);
        std::vector<std::vector<Numero>> localPrimos(numHilos);
        std::vector<std::thread> workers;

        Numero total = fin - inicio + 1;
        for (unsigned int t = 0; t < numHilos; ++t) {
            Numero sub_ini = inicio + t * total / numHilos;
            Numero sub_fin = (t == numHilos - 1) ? fin : (inicio + (t + 1) * total / numHilos - 1);

            workers.emplace_back([&, t, sub_ini, sub_fin]() {
                Numero cur = (sub_ini % 2 == 0) ? sub_ini + 1 : sub_ini;
                for (Numero n = cur; n <= sub_fin; n += 2) {
                    if (esPrimo(n, primos)) localPrimos[t].push_back(n);
                }
            });
        }

        for (auto& w : workers) w.join();

        for (unsigned int t = 0; t < numHilos; ++t) {
            primos.insert(primos.end(), localPrimos[t].begin(), localPrimos[t].end());
        }
        inicio = fin + 1;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double seg = std::chrono::duration<double>(t1 - t0).count();
    return {"CMP (Multinúcleo Real)", static_cast<uint64_t>(primos.size()), seg, 0.0, 0.0, 0};
}

// ============================================================
// MODELO 2: SMT (Simultaneous Multithreading)
// Aproximación por sobre-suscripción (2x hilos compitiendo).
// ============================================================
ResultadoModelo ejecutarSMT(Numero N, Numero tamanoTanda, unsigned int numHilosSMT) {
    std::vector<Numero> primos = primosIniciales();
    auto t0 = std::chrono::high_resolution_clock::now();
    Numero inicio = 101;

    while (inicio <= N) {
        Numero fin = calcularFinSeguro(inicio, N, tamanoTanda);
        std::vector<std::vector<Numero>> localPrimos(numHilosSMT);
        std::vector<std::thread> workers;

        Numero total = fin - inicio + 1;
        for (unsigned int t = 0; t < numHilosSMT; ++t) {
            Numero sub_ini = inicio + t * total / numHilosSMT;
            Numero sub_fin = (t == numHilosSMT - 1) ? fin : (inicio + (t + 1) * total / numHilosSMT - 1);

            workers.emplace_back([&, t, sub_ini, sub_fin]() {
                Numero cur = (sub_ini % 2 == 0) ? sub_ini + 1 : sub_ini;
                for (Numero n = cur; n <= sub_fin; n += 2) {
                    if (esPrimo(n, primos)) localPrimos[t].push_back(n);
                }
            });
        }

        for (auto& w : workers) w.join();

        for (unsigned int t = 0; t < numHilosSMT; ++t) {
            primos.insert(primos.end(), localPrimos[t].begin(), localPrimos[t].end());
        }
        inicio = fin + 1;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double seg = std::chrono::duration<double>(t1 - t0).count();
    return {"SMT (Sobre-suscripción 2x)", static_cast<uint64_t>(primos.size()), seg, 0.0, 0.0, 0};
}

// ============================================================
// MODELO 3: FINE-GRAINED (Grano Fino)
// Planificación cooperativa round-robin por cuantums (Q = 1 candidato).
// El cambio de hilo ocurre en cada ciclo/cuantum sin importar si hay stall.
// ============================================================
ResultadoModelo ejecutarFineGrained(Numero N, Numero tamanoTanda, unsigned int numHilosVirtuales) {
    std::vector<Numero> primos = primosIniciales();
    uint64_t conmutacionesContexto = 0;

    auto t0 = std::chrono::high_resolution_clock::now();
    Numero inicio = 101;

    while (inicio <= N) {
        Numero fin = calcularFinSeguro(inicio, N, tamanoTanda);

        // Vector de candidatos de la tanda
        std::vector<Numero> candidatos;
        Numero cur = (inicio % 2 == 0) ? inicio + 1 : inicio;
        for (Numero n = cur; n <= fin; n += 2) candidatos.push_back(n);

        size_t totalCandidatos = candidatos.size();
        // Simulación round-robin: cada hilo virtual ejecuta un quantum de 1 número
        for (size_t i = 0; i < totalCandidatos; ++i) {
            // Hilo actual en turno round-robin:
            // unsigned int hiloActual = i % numHilosVirtuales;
            Numero n = candidatos[i];

            if (esPrimo(n, primos)) {
                primos.push_back(n);
            }
            // En Grano Fino se conmuta obligatoriamente en cada cuantum
            conmutacionesContexto++;
        }

        inicio = fin + 1;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double seg = std::chrono::duration<double>(t1 - t0).count();
    return {"Fine-Grained (Round-Robin Q=1)", static_cast<uint64_t>(primos.size()), seg, 0.0, 0.0, conmutacionesContexto};
}

// ============================================================
// MODELO 4: COARSE-GRAINED (Grano Grueso)
// Un hilo ejecuta continuamente en ráfaga. El cambio de contexto ocurre
// ÚNICAMENTE ante un evento costoso de bloqueo/stall (evaluación de latencia alta).
// ============================================================
ResultadoModelo ejecutarCoarseGrained(Numero N, Numero tamanoTanda, unsigned int numHilosVirtuales) {
    std::vector<Numero> primos = primosIniciales();
    uint64_t stallsDetectados = 0;

    // Umbral de operaciones para considerar un "stall" (evento de alta latencia/fallo de caché)
    const uint32_t UMBRAL_STALL = 15;

    auto t0 = std::chrono::high_resolution_clock::now();
    Numero inicio = 101;

    while (inicio <= N) {
        Numero fin = calcularFinSeguro(inicio, N, tamanoTanda);

        Numero cur = (inicio % 2 == 0) ? inicio + 1 : inicio;
        unsigned int hiloActual = 0;

        for (Numero n = cur; n <= fin; n += 2) {
            uint32_t divisiones = 0;
            bool primo = esPrimoConConteo(n, primos, divisiones);
            if (primo) primos.push_back(n);

            // Condición de Coarse-Grained: solo se conmuta ante un evento de bloqueo/stall costoso
            if (divisiones >= UMBRAL_STALL) {
                stallsDetectados++;
                // Cambio de contexto al siguiente hilo disponible
                hiloActual = (hiloActual + 1) % numHilosVirtuales;
            }
        }

        inicio = fin + 1;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double seg = std::chrono::duration<double>(t1 - t0).count();
    return {"Coarse-Grained (Switch on Stall)", static_cast<uint64_t>(primos.size()), seg, 0.0, 0.0, stallsDetectados};
}

// ============================================================
// MAIN - BENCHMARK DE COMPARACIÓN DE MODELOS MULTITHREADING
// ============================================================
int main(int argc, char* argv[]) {
    Numero N = 3000000;
    Numero tamanoTanda = 200000;
    unsigned int hilosHW = 4; // Núcleos físicos estándar para pruebas

    if (argc >= 2) N = std::stoull(argv[1]);
    if (argc >= 3) tamanoTanda = std::stoull(argv[2]);
    if (argc >= 4) hilosHW = std::stoul(argv[3]);

    unsigned int hilosSMT = hilosHW * 2; // Sobre-suscripción para SMT

    std::cout << "\n";
    std::cout << "==============================================================================\n";
    std::cout << "     CE 4302 - FRAMEWORK DE MODELOS MULTITHREADING (DEMO 2)      \n";
    std::cout << "==============================================================================\n";
    std::cout << "Parámetros de Prueba:\n";
    std::cout << "  - Límite N:                 " << N << "\n";
    std::cout << "  - Tamaño de Tanda:          " << tamanoTanda << "\n";
    std::cout << "  - Núcleos / Hilos HW (CMP): " << hilosHW << "\n";
    std::cout << "  - Hilos SMT (2x sobre-susc): " << hilosSMT << "\n";
    std::cout << "------------------------------------------------------------------------------\n";
    std::cout << "Ejecutando modelos...\n";

    // 0. Base Secuencial
    ResultadoModelo rSec = ejecutarSecuencial(N, tamanoTanda);

    // 1. CMP (Chip Multiprocessing)
    ResultadoModelo rCMP = ejecutarCMP(N, tamanoTanda, hilosHW);
    rCMP.speedup = rSec.segundos / rCMP.segundos;
    rCMP.eficiencia = (rCMP.speedup / hilosHW) * 100.0;

    // 2. SMT (Simultaneous Multithreading)
    ResultadoModelo rSMT = ejecutarSMT(N, tamanoTanda, hilosSMT);
    rSMT.speedup = rSec.segundos / rSMT.segundos;
    rSMT.eficiencia = (rSMT.speedup / hilosSMT) * 100.0;

    // 3. Fine-Grained Multithreading
    ResultadoModelo rFine = ejecutarFineGrained(N, tamanoTanda, hilosHW);
    rFine.speedup = rSec.segundos / rFine.segundos;
    rFine.eficiencia = (rFine.speedup / hilosHW) * 100.0;

    // 4. Coarse-Grained Multithreading
    ResultadoModelo rCoarse = ejecutarCoarseGrained(N, tamanoTanda, hilosHW);
    rCoarse.speedup = rSec.segundos / rCoarse.segundos;
    rCoarse.eficiencia = (rCoarse.speedup / hilosHW) * 100.0;

    // ============================================================
    // REPORTE DE MÉTRICAS EXIGIDAS
    // ============================================================
    std::vector<ResultadoModelo> resultados = {rSec, rCMP, rSMT, rFine, rCoarse};

    std::cout << "\n";
    std::cout << "==============================================================================\n";
    std::cout << "                       TABLA DE RESULTADOS                        \n";
    std::cout << "==============================================================================\n";
    std::cout << std::left 
              << std::setw(34) << "Modelo de Ejecución"
              << std::setw(12) << "Primos"
              << std::setw(14) << "Tiempo (s)"
              << std::setw(11) << "Speedup"
              << std::setw(13) << "Eficiencia"
              << "Eventos/Stalls" << "\n";
    std::cout << "------------------------------------------------------------------------------\n";

    bool todosCoinciden = true;
    for (const auto& r : resultados) {
        if (r.cantidadPrimos != rSec.cantidadPrimos) todosCoinciden = false;
        std::cout << std::left 
                  << std::setw(34) << r.nombre
                  << std::setw(12) << r.cantidadPrimos
                  << std::fixed << std::setprecision(6) << std::setw(14) << r.segundos
                  << std::setprecision(2) << std::setw(11) << (std::to_string(r.speedup).substr(0,4) + "x")
                  << std::setw(13) << (std::to_string(r.eficiencia).substr(0,5) + "%")
                  << r.eventosContexto << "\n";
    }
    std::cout << "------------------------------------------------------------------------------\n";

    if (todosCoinciden) {
        std::cout << ">> VALIDACIÓN DE EXACTITUD: [CORRECTA] (Todos encontraron " 
                  << rSec.cantidadPrimos << " primos exactos).\n";
    } else {
        std::cout << ">> VALIDACIÓN DE EXACTITUD: [ERROR] Discrepancia en cantidad de primos.\n";
    }
    std::cout << "==============================================================================\n\n";

    return 0;
}