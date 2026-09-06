#include <iostream>
#include <cstdint> // Necesario para usar uint64_t
#include <vector>
#include <chrono> // Necesario para medir tiempos
#include <cmath>
#include <thread>


using Numero = uint64_t; //tipo de dato entero sin signo ocupa 8 bytes de memoria.

// ============================================================
// Lista precalculada de primos <= 100
// ============================================================

//paquete::nombre_funcion
//Busca el nombre_funcion dentro de paquete
//:: [operador resolvente de ambito]
//std:: (Espacio de nombres) este es el namespace

std::vector<Numero> primosIniciales()
{
    return {
        2, 3, 5, 7,
        11, 13, 17, 19,
        23, 29,
        31, 37,
        41, 43, 47,
        53, 59,
        61, 67,
        71, 73, 79,
        83, 89,
        97
    };
}

// ============================================================
// Etapa 1
// Verificar si un numero es primo.
//
// Solamente se divide entre primos <= sqrt(n).
// ============================================================

bool esPrimo(Numero n, const std::vector<Numero>& primos)
{
    if (n < 2)
        return false;

    for (Numero p : primos)
    {
        // Si p > sqrt(n), ya no hace falta seguir.
        if (p * p > n)
            break;

        if (n % p == 0)
            return false;
    }

    return true;
}

// ============================================================
// Comprueba la dependencia de una tanda.
//
// Para poder procesar [inicio, fin] en paralelo usando solamente
// los primos calculados anteriormente necesitamos:
//
// sqrt(fin) <= inicio - 1
//
// porque conocemos todos los primos hasta inicio-1.
// ============================================================

bool tandaEsSegura(Numero inicio, Numero fin)
{
    Numero raiz = static_cast<Numero>(
        std::sqrt(static_cast<long double>(fin))
    );

    return raiz <= inicio - 1;
}

// ============================================================
// Procesamiento SECUENCIAL de una tanda
// ============================================================

std::vector<Numero> procesarTandaSecuencial(
    Numero inicio,
    Numero fin,
    const std::vector<Numero>& primosConocidos)
{
    std::vector<Numero> encontrados;

    // Como empezamos después de 100, podemos ignorar pares.
    if (inicio % 2 == 0)
        ++inicio;

    for (Numero n = inicio; n <= fin; n += 2)
    {
        if (esPrimo(n, primosConocidos))
            encontrados.push_back(n);
    }

    return encontrados;
}

// ============================================================
// Trabajo que realizará cada thread
// ============================================================

void trabajoThread(
    Numero inicio,
    Numero fin,
    const std::vector<Numero>& primosConocidos,
    std::vector<Numero>& resultado)
{
    //Igualmente acá se busca optimizar ignorando pares.
    if (inicio % 2 == 0)
        ++inicio;

    for (Numero n = inicio; n <= fin; n += 2)
    {
        if (esPrimo(n, primosConocidos))
            resultado.push_back(n);
    }
}

// ============================================================
// Procesamiento PARALELO de una tanda
// ============================================================

std::vector<Numero> procesarTandaParalela(
    Numero inicio,
    Numero fin,
    const std::vector<Numero>& primosConocidos,
    unsigned int cantidadThreads)
{
    // Cantidad es igual a la tanda
    Numero cantidad = fin - inicio + 1;

    // Se optimiza para que no haya más threads que números a procesar.
    if (cantidadThreads > cantidad)
        cantidadThreads = static_cast<unsigned int>(cantidad);

    if (cantidadThreads == 0)
        cantidadThreads = 1;

    //creamos un vector de threads para almacenar los hilos que se van a crear.
    std::vector<std::thread> threads;

    // Cada thread tiene SU PROPIO vector. Aquí se usa la técnica de "thread-local storage" para evitar conflictos de escritura.
    std::vector<std::vector<Numero>> resultados(cantidadThreads);

    // Este cálculo es muy importante:
    // Divide la tanda en bloques pero lo hace de manera en que la división entera no afecte a la distribución de números entre threads.
    // Al sumar la cantidad de threads menos 1 antes de dividir, nos aseguramos de que cualquier residuo se distribuya entre los primeros threads.
    // Esto evita que no se puedan cubrir todos los números de la tanda.
    Numero tamanoBloque =
        (cantidad + cantidadThreads - 1) / cantidadThreads;

    // Crear threads y asignarles su bloque de trabajo.
    for (unsigned int t = 0; t < cantidadThreads; ++t)
    {
        Numero desde = inicio + t * tamanoBloque;

        if (desde > fin)
            break;

        Numero hasta = std::min(
            fin,
            desde + tamanoBloque - 1
        );

        // Esta función crea el hilo, emplace_back construye el objeto thread, no hay que inicializarlo.
        // std::cref() se usa para pasar la referencia constante de primosConocidos, evitando copias innecesarias.
        // std::ref() se usa para pasar la referencia de resultados[t], permitiendo que el hilo escriba directamente en el vector correspondiente.
        threads.emplace_back(
            trabajoThread,
            desde,
            hasta,
            std::cref(primosConocidos),
            std::ref(resultados[t])
        );
    }

    // Esperar a todos. El hilo principal no continúna su trabajo hasta que todos los threads hayan terminado.
    for (std::thread& hilo : threads)
        hilo.join();

    // Juntar resultados.
    std::vector<Numero> encontrados;

    for (const auto& lista : resultados)
    {
        encontrados.insert(
            encontrados.end(),
            lista.begin(),
            lista.end()
        );
    }

    return encontrados;
}

// ============================================================
// Resultado de una ejecución
// ============================================================

struct Resultado
{
    uint64_t cantidadPrimos;
    double segundos;
};

// ============================================================
// Ejecución SECUENCIAL completa
// ============================================================

Resultado ejecutarSecuencial(
    Numero N,
    Numero tamanoTanda)
{
    std::vector<Numero> primos = primosIniciales();

    // Si N <= 100, contamos solamente los que correspondan.
    if (N <= 100)
    {
        uint64_t contador = 0;

        for (Numero p : primos)
        {
            if (p <= N)
                ++contador;
        }

        return {contador, 0.0};
    }

    auto inicioTiempo =
        std::chrono::high_resolution_clock::now();

    Numero inicio = 101;

    while (inicio <= N)
    {
        Numero fin = std::min(
            N,
            inicio + tamanoTanda - 1
        );

        if (!tandaEsSegura(inicio, fin))
        {
            std::cerr
                << "ERROR: la tanda "
                << inicio << "-" << fin
                << " es demasiado grande.\n";

            std::cerr
                << "sqrt(" << fin << ") = "
                << std::sqrt(static_cast<long double>(fin))
                << "\n";

            exit(1);
        }

        // Importante:
        // durante la tanda usamos solamente los primos
        // conocidos ANTES de comenzar.
        std::vector<Numero> encontrados =
            procesarTandaSecuencial(
                inicio,
                fin,
                primos
            );

        // Ahora sí actualizamos la lista.
        primos.insert(
            primos.end(),
            encontrados.begin(),
            encontrados.end()
        );

        inicio = fin + 1;
    }

    auto finTiempo =
        std::chrono::high_resolution_clock::now();

    double segundos =
        std::chrono::duration<double>(
            finTiempo - inicioTiempo
        ).count();

    return {
        static_cast<uint64_t>(primos.size()),
        segundos
    };
}

// ============================================================
// Ejecución PARALELA completa
// ============================================================

Resultado ejecutarParalelo(
    Numero N,
    Numero tamanoTanda,
    unsigned int cantidadThreads)
{
    std::vector<Numero> primos = primosIniciales();

    if (N <= 100)
    {
        uint64_t contador = 0;

        for (Numero p : primos)
        {
            if (p <= N)
                ++contador;
        }

        return {contador, 0.0};
    }

    auto inicioTiempo =
        std::chrono::high_resolution_clock::now();

    Numero inicio = 101;

    while (inicio <= N)
    {
        Numero fin = std::min(
            N,
            inicio + tamanoTanda - 1
        );

        if (!tandaEsSegura(inicio, fin))
        {
            std::cerr
                << "ERROR: la tanda "
                << inicio << "-" << fin
                << " es demasiado grande para "
                << "los primos conocidos.\n";

            exit(1);
        }

        std::vector<Numero> encontrados =
            procesarTandaParalela(
                inicio,
                fin,
                primos,
                cantidadThreads
            );

        // Ningún thread modifica "primos".
        // Solo el hilo principal lo modifica aquí.
        primos.insert(
            primos.end(),
            encontrados.begin(),
            encontrados.end()
        );

        inicio = fin + 1;
    }

    auto finTiempo =
        std::chrono::high_resolution_clock::now();

    double segundos =
        std::chrono::duration<double>(
            finTiempo - inicioTiempo
        ).count();

    return {
        static_cast<uint64_t>(primos.size()),
        segundos
    };
}

// ============================================================
// MAIN
//
// Uso:
// ./primos N M threads
//
// Ejemplo:
// ./primos 1000000 100 4
// ============================================================

int main(int argc, char* argv[])
{
    Numero N = 500;
    Numero tamanoTanda = 100;

    unsigned int threads =
        std::thread::hardware_concurrency(); // Detecta la cantidad de threads lógicos disponibles en el hardware.

    if (threads == 0)
        threads = 1;

    if (argc >= 2)
        N = std::stoull(argv[1]);

    if (argc >= 3)
        tamanoTanda = std::stoull(argv[2]);

    if (argc >= 4)
        threads = std::stoul(argv[3]);

    std::cout << "====================================\n";
    std::cout << " Calculo de numeros primos\n";
    std::cout << "====================================\n\n";

    std::cout << "N:                  "
              << N << "\n";

    std::cout << "Tamano de tanda:    "
              << tamanoTanda << "\n";

    std::cout << "Threads utilizados: "
              << threads << "\n";

    std::cout << "Threads HW detectados: "
              << std::thread::hardware_concurrency()
              << "\n";

    std::cout << "sqrt(N):            "
              << std::sqrt(static_cast<long double>(N))
              << "\n\n";

    // ----------------------------------
    // SECUENCIAL
    // ----------------------------------

    std::cout << "Ejecutando secuencial...\n";

    Resultado secuencial =
        ejecutarSecuencial(
            N,
            tamanoTanda
        );

    std::cout << "Primos encontrados: "
              << secuencial.cantidadPrimos
              << "\n";

    std::cout << "Tiempo secuencial:   "
              << secuencial.segundos
              << " segundos\n\n";
    
    // ----------------------------------
    // PARALELO
    // ----------------------------------

    std::cout << "Ejecutando paralelo...\n";

    Resultado paralelo =
        ejecutarParalelo(
            N,
            tamanoTanda,
            threads
        );

    std::cout << "Primos encontrados: "
              << paralelo.cantidadPrimos
              << "\n";

    std::cout << "Tiempo paralelo:     "
              << paralelo.segundos
              << " segundos\n\n";
    
    // ----------------------------------
    // Comprobar resultados
    // ----------------------------------

    if (secuencial.cantidadPrimos ==
        paralelo.cantidadPrimos)
    {
        std::cout
            << "OK: ambos algoritmos dieron "
            << "el mismo resultado.\n";
    }
    else
    {
        std::cout
            << "ERROR: los resultados no coinciden.\n";
    }

    // ----------------------------------
    // Speedup
    // ----------------------------------

    if (paralelo.segundos > 0)
    {
        double speedup =
            secuencial.segundos /
            paralelo.segundos;

        std::cout << "\nSpeedup: "
                  << speedup
                  << "x\n";
    }
    
    return 0;
}