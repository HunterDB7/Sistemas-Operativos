#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

namespace fs = std::filesystem;

struct ProcesoInfo {
    int pid;
    int ppid;
    std::string usuario;
    char estado;
    unsigned long long utime;
    unsigned long long stime;
    long prioridad;
    int num_hilos;
    unsigned long mem_rss;
    std::string comando;
};

struct SistemaInfo {
    unsigned long long cpu_user;
    unsigned long long cpu_nice;
    unsigned long long cpu_system;
    unsigned long long cpu_idle;
    unsigned long long cpu_iowait;
    unsigned long long cpu_total;

    unsigned long mem_total;
    unsigned long mem_free;
    unsigned long mem_available;

    double load_1;
    double load_5;
    double load_15;
};

std::string obtener_usuario_por_uid(uid_t uid);
bool leer_resumen_sistema(SistemaInfo &sys);
bool leer_proceso(int pid, ProcesoInfo &proc);
std::vector<ProcesoInfo> obtener_procesos();

std::string obtener_usuario_por_uid(uid_t uid) {
    if (struct passwd* pwd = getpwuid(uid); pwd != nullptr) {
        return std::string(pwd->pw_name);
    }
    return std::to_string(uid);
}

bool leer_resumen_sistema(SistemaInfo &sys) {
    // Leer /proc/stat
    if (std::ifstream stat_file("/proc/stat"); stat_file.is_open()) {
        std::string linea;
        if (std::getline(stat_file, linea)) {
            std::istringstream iss(linea);
            std::string cpu;
            iss >> cpu; 
            
            std::array<unsigned long long, 8> t;
            for (auto& val : t) {
                iss >> val;
            }
            
            sys.cpu_user   = t[0];
            sys.cpu_nice   = t[1];
            sys.cpu_system = t[2];
            sys.cpu_idle   = t[3];
            sys.cpu_iowait = t[4];
            
            sys.cpu_total = 0;
            for (auto val : t) {
                sys.cpu_total += val;
            }
        }
    } else {
        return false;
    }

    // Leer /proc/meminfo
    if (std::ifstream mem_file("/proc/meminfo"); mem_file.is_open()) {
        std::string linea;
        sys.mem_total = 0;
        sys.mem_free = 0;
        sys.mem_available = 0;
        while (std::getline(mem_file, linea)) {
            std::istringstream iss(linea);
            std::string clave;
            unsigned long valor;
            iss >> clave >> valor;
            
            if (clave == "MemTotal:") sys.mem_total = valor;
            else if (clave == "MemFree:") sys.mem_free = valor;
            else if (clave == "MemAvailable:") sys.mem_available = valor;
        }
    } else {
        return false;
    }

    // Leer /proc/loadavg
    if (std::ifstream load_file("/proc/loadavg"); load_file.is_open()) {
        load_file >> sys.load_1 >> sys.load_5 >> sys.load_15;
    }

    return true;
}

bool leer_proceso(int pid, ProcesoInfo &proc) {
    fs::path base_path = fs::path("/proc") / std::to_string(pid);
    
    std::ifstream stat_file(base_path / "stat");
    if (!stat_file.is_open()) return false;
    
    std::string linea_stat;
    if (!std::getline(stat_file, linea_stat)) return false;

    size_t pos_open = linea_stat.find('(');
    size_t pos_close = linea_stat.rfind(')');
    if (pos_open == std::string::npos || pos_close == std::string::npos) return false;

    proc.pid = pid;
    proc.comando = linea_stat.substr(pos_open + 1, pos_close - pos_open - 1);

    std::istringstream iss(linea_stat.substr(pos_close + 2));
    std::string estado_str;
    iss >> estado_str;
    proc.estado = estado_str.empty() ? '?' : estado_str[0];

    auto saltar = [&iss](int cantidad) {
        std::string skip;
        for (int i = 0; i < cantidad; ++i) {
            iss >> skip;
        }
    };

    iss >> proc.ppid;
    saltar(10);
    iss >> proc.utime >> proc.stime;
    saltar(2);
    iss >> proc.prioridad;
    saltar(1);
    iss >> proc.num_hilos;

    std::ifstream status_file(base_path / "status");
    if (status_file.is_open()) {
        uid_t uid = 0;
        proc.mem_rss = 0;
        std::string linea_status;
        while (std::getline(status_file, linea_status)) {
            if (linea_status.compare(0, 4, "Uid:") == 0) {
                std::istringstream iss_uid(linea_status);
                std::string etiqueta;
                iss_uid >> etiqueta >> uid;
                proc.usuario = obtener_usuario_por_uid(uid);
            } else if (linea_status.compare(0, 6, "VmRSS:") == 0) {
                std::istringstream iss_rss(linea_status);
                std::string etiqueta;
                iss_rss >> etiqueta >> proc.mem_rss;
            }
        }
    } else {
        proc.usuario = "desconocido";
        proc.mem_rss = 0;
    }

    return true;
}

std::vector<ProcesoInfo> obtener_procesos() {
    std::vector<ProcesoInfo> lista_procesos;
    fs::path proc_dir("/proc");

    if (!fs::is_directory(proc_dir)) {
        return lista_procesos;
    }

    for (const auto& entry : fs::directory_iterator(proc_dir)) {
        if (entry.is_directory()) {
            std::string filename = entry.path().filename().string();
            
            bool es_pid = !filename.empty();
            for (char c : filename) {
                if (c < '0' || c > '9') {
                    es_pid = false;
                    break;
                }
            }
            
            if (es_pid) {
                int pid = std::stoi(filename);
                ProcesoInfo proc;
                if (leer_proceso(pid, proc)) {
                    lista_procesos.push_back(std::move(proc));
                }
            }
        }
    }

    return lista_procesos;
}

