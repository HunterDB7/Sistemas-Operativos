// Pone los limites: memoria con setrlimit y CPU con cgroups

#include "Limites.hpp"
#include "Monitor.hpp"

#include <fstream>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

// Limite de memoria

static struct rlimit limite_original;
static bool memoria_limitada = false;

bool aplicarLimiteMemoria(std::size_t limite_mb, std::string& mensaje) {
    if (limite_mb == 0) {
        mensaje = "sin limite";
        return true;
    }

    getrlimit(RLIMIT_AS, &limite_original);

    // El limite es la memoria virtual que ya usa el programa + los MB que pidio el usuario
    InfoMemoria info = leerInfoMemoria();
    rlim_t nuevo = ((rlim_t)info.vmsize_kb + (rlim_t)limite_mb * 1024) * 1024;

    struct rlimit r = limite_original;
    r.rlim_cur = nuevo;
    if (setrlimit(RLIMIT_AS, &r) != 0) {
        mensaje = "NO aplicado (setrlimit fallo)";
        return false;
    }

    memoria_limitada = true;
    mensaje = "aplicado (" + std::to_string(limite_mb) + " MB de memoria virtual extra)";
    return true;
}

void quitarLimiteMemoria() {
    if (memoria_limitada) {
        setrlimit(RLIMIT_AS, &limite_original);
        memoria_limitada = false;
    }
}

// Limite de CPU

static std::string carpeta_cpu;   // carpeta nueva que creamos para el limite
static std::string carpeta_padre; // carpeta donde estaba el programa antes

static bool escribirArchivo(const std::string& ruta, const std::string& texto) {
    std::ofstream f(ruta);
    if (!f) {
        return false;
    }
    f << texto;
    f.flush();
    return f.good();
}

static bool existe(const char* ruta) {
    return access(ruta, F_OK) == 0;
}

bool aplicarLimiteCpu(std::size_t porcentaje, std::string& mensaje) {
    if (porcentaje == 0) {
        mensaje = "sin limite";
        return true;
    }

    // El periodo es 100000 us. Para 50% de un nucleo la cuota es 50000 us.
    std::string cuota = std::to_string(porcentaje * 1000);
    std::string padre;
    std::string archivo_cuota;
    std::string texto_cuota;

    if (existe("/sys/fs/cgroup/cpu/cpu.cfs_quota_us")) {
        // cgroup v1
        padre = "/sys/fs/cgroup/cpu";
        archivo_cuota = "/cpu.cfs_quota_us";
        texto_cuota = cuota;
    } else if (existe("/sys/fs/cgroup/cgroup.controllers")) {
        // cgroup v2
        padre = "/sys/fs/cgroup";
        escribirArchivo(padre + "/cgroup.subtree_control", "+cpu");
        archivo_cuota = "/cpu.max";
        texto_cuota = cuota + " 100000";
    } else {
        mensaje = "NO aplicado (el sistema no tiene cgroups)";
        return false;
    }

    std::string carpeta = padre + "/lab_so";
    mkdir(carpeta.c_str(), 0755);

    bool ok = escribirArchivo(carpeta + archivo_cuota, texto_cuota) &&
              escribirArchivo(carpeta + "/cgroup.procs", std::to_string(getpid()));

    if (!ok) {
        rmdir(carpeta.c_str());
        mensaje = "NO aplicado (hay que ejecutar con sudo)";
        return false;
    }

    carpeta_cpu = carpeta;
    carpeta_padre = padre;
    mensaje = "aplicado (" + std::to_string(porcentaje) + "% de un nucleo)";
    return true;
}

void quitarLimiteCpu() {
    if (carpeta_cpu.empty()) {
        return;
    }
    // Regresamos el programa a su carpeta original y borramos la que creamos
    escribirArchivo(carpeta_padre + "/cgroup.procs", std::to_string(getpid()));
    rmdir(carpeta_cpu.c_str());
    carpeta_cpu.clear();
}
