#include "Monitor.hpp"

#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/resource.h>

// Busca en un archivo de /proc la linea que empieza con "clave"
// y devuelve el numero que tiene (en kB). Si no la encuentra devuelve 0.
static long buscarValor(const char* archivo, const std::string& clave) {
    std::ifstream f(archivo);
    std::string linea;
    while (std::getline(f, linea)) {
        if (linea.compare(0, clave.size(), clave) == 0) {
            return std::atol(linea.c_str() + clave.size());
        }
    }
    return 0;
}

InfoMemoria leerInfoMemoria() {
    InfoMemoria info;

    info.vmsize_kb = buscarValor("/proc/self/status", "VmSize:");
    info.rss_kb = buscarValor("/proc/self/status", "VmRSS:");
    info.rss_pico_kb = buscarValor("/proc/self/status", "VmHWM:");
    info.swap_kb = buscarValor("/proc/self/status", "VmSwap:");

    info.ram_total_kb = buscarValor("/proc/meminfo", "MemTotal:");
    info.ram_libre_kb = buscarValor("/proc/meminfo", "MemAvailable:");
    info.swap_total_kb = buscarValor("/proc/meminfo", "SwapTotal:");
    info.swap_libre_kb = buscarValor("/proc/meminfo", "SwapFree:");

    // Los fallos de pagina se sacan con getrusage
    struct rusage uso;
    getrusage(RUSAGE_SELF, &uso);
    info.fallos_menores = uso.ru_minflt;
    info.fallos_mayores = uso.ru_majflt;

    return info;
}

void reiniciarPicoRss() {
    // Escribir "5" en este archivo reinicia el maximo (VmHWM)
    std::ofstream f("/proc/self/clear_refs");
    f << "5";
}
