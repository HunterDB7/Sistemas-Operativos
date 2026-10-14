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
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <algorithm>
#include <iomanip>


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
struct ProcesoMetricas : public ProcesoInfo {
    double cpu_uso{0.0};
    double mem_uso{0.0};
    std::deque<double> historico_cpu;
    double tiempo_cpu_segundos{0.0};
    std::string tiempo_cpu_formateado;
    bool flag_zombie{false};
    bool flag_bloqueado{false};

    ProcesoMetricas() {
        pid = 0; ppid = 0; usuario = ""; estado = '?';
        utime = 0; stime = 0; prioridad = 0; num_hilos = 0; mem_rss = 0; comando = "";
    }
    explicit ProcesoMetricas(const ProcesoInfo& base) : ProcesoInfo(base) {}
};

struct SistemaMetricas : public SistemaInfo {
    unsigned long mem_used{0};
    int total_procs{0};
    int running_procs{0};
    int stopped_procs{0};
    int zombie_procs{0};
    int blocked_procs{0};

    SistemaMetricas() {
        cpu_user = 0; cpu_nice = 0; cpu_system = 0; cpu_idle = 0; cpu_iowait = 0; cpu_total = 0;
        mem_total = 0; mem_free = 0; mem_available = 0; load_1 = 0.0; load_5 = 0.0; load_15 = 0.0;
    }
    explicit SistemaMetricas(const SistemaInfo& base) : SistemaInfo(base) {}
};

enum class CriterioOrden {
    CPU_DESC,
    MEM_DESC,
    PID_ASC,
    TIEMPO_CPU_DESC
};

class GestorMetricas {
private:
    std::unordered_map<int, unsigned long long> prev_procs_ticks_;
    std::unordered_map<int, std::deque<double>> historico_procs_;
    unsigned long long prev_sys_total_{0};
    double clk_tck_{100.0};
    int num_processors_{1};
    size_t max_historial_size_;

public:
    explicit GestorMetricas(size_t max_historial = 30) 
        : prev_sys_total_(0), max_historial_size_(max_historial) {
        
        long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
        if (nprocs > 0) num_processors_ = static_cast<int>(nprocs);

        long clk = sysconf(_SC_CLK_TCK);
        if (clk > 0) clk_tck_ = static_cast<double>(clk);
    }

    void actualizar(const std::vector<ProcesoInfo>& procesos_crudos, 
                    const SistemaInfo& sys_crudo, 
                    std::vector<ProcesoMetricas>& out_procesos, 
                    SistemaMetricas& out_sys);

    static void ordenar(std::vector<ProcesoMetricas>& procesos, CriterioOrden criterio);
    static std::vector<ProcesoMetricas> filtrar(const std::vector<ProcesoMetricas>& procesos, const std::string& filtro);

    static constexpr bool es_zombie(char estado) noexcept { return estado == 'Z'; }
    static constexpr bool es_bloqueado(char estado) noexcept { return estado == 'D'; }

    static unsigned long calcular_memoria_usada(const SistemaInfo& sys) {
        if (sys.mem_total >= sys.mem_available && sys.mem_available > 0) {
            return sys.mem_total - sys.mem_available;
        } else if (sys.mem_total >= sys.mem_free) {
            return sys.mem_total - sys.mem_free;
        }
        return 0;
    }

    static double calcular_memoria_proceso(unsigned long mem_rss, unsigned long mem_total) {
        if (mem_total == 0) return 0.0;
        return (static_cast<double>(mem_rss) / static_cast<double>(mem_total)) * 100.0;
    }

    void limpiar_pids_obsoletos(const std::unordered_set<int>& pids_actuales) {
        for (auto it = prev_procs_ticks_.begin(); it != prev_procs_ticks_.end(); ) {
            if (pids_actuales.find(it->first) == pids_actuales.end()) {
                it = prev_procs_ticks_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = historico_procs_.begin(); it != historico_procs_.end(); ) {
            if (pids_actuales.find(it->first) == pids_actuales.end()) {
                it = historico_procs_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    std::string formatear_tiempo_cpu(double segundos) const {
        int total_secs = static_cast<int>(segundos);
        int horas = total_secs / 3600;
        int minutos = (total_secs % 3600) / 60;
        int secs = total_secs % 60;

        std::ostringstream oss;
        if (horas > 0) {
            oss << horas << "h " << std::setfill('0') << std::setw(2) << minutos << "m";
        } else {
            oss << minutos << ":" << std::setfill('0') << std::setw(2) << secs;
        }
        return oss.str();
    }
};

void GestorMetricas::actualizar(const std::vector<ProcesoInfo>& procesos_crudos, 
                                const SistemaInfo& sys_crudo, 
                                std::vector<ProcesoMetricas>& out_procesos, 
                                SistemaMetricas& out_sys) {
    out_sys = SistemaMetricas(sys_crudo);
    out_sys.mem_used = calcular_memoria_usada(sys_crudo);

    out_sys.total_procs = static_cast<int>(procesos_crudos.size());
    out_sys.running_procs = 0;
    out_sys.stopped_procs = 0;
    out_sys.zombie_procs = 0;
    out_sys.blocked_procs = 0;

    unsigned long long delta_sys = 0;
    bool primer_refresco = (prev_sys_total_ == 0);

    if (!primer_refresco && sys_crudo.cpu_total >= prev_sys_total_) {
        delta_sys = sys_crudo.cpu_total - prev_sys_total_;
    }

    std::unordered_set<int> pids_actuales;
    pids_actuales.reserve(procesos_crudos.size());

    out_procesos.clear();
    out_procesos.reserve(procesos_crudos.size());

    for (const auto& proc : procesos_crudos) {
        pids_actuales.insert(proc.pid);

        ProcesoMetricas pm(proc);

        pm.flag_zombie = es_zombie(pm.estado);
        pm.flag_bloqueado = es_bloqueado(pm.estado);

        if (pm.estado == 'R') {
            out_sys.running_procs++;
        } else if (pm.estado == 'T' || pm.estado == 't') {
            out_sys.stopped_procs++;
        } else if (pm.flag_zombie) {
            out_sys.zombie_procs++;
        } else if (pm.flag_bloqueado) {
            out_sys.blocked_procs++;
        }

        pm.mem_uso = calcular_memoria_proceso(pm.mem_rss, out_sys.mem_total);

        unsigned long long ticks_actuales = pm.utime + pm.stime;
        pm.tiempo_cpu_segundos = static_cast<double>(ticks_actuales) / clk_tck_;
        pm.tiempo_cpu_formateado = formatear_tiempo_cpu(pm.tiempo_cpu_segundos);

        double cpu_pct = 0.0;
        if (!primer_refresco && delta_sys > 0) {
            auto it = prev_procs_ticks_.find(pm.pid);
            if (it != prev_procs_ticks_.end()) {
                unsigned long long ticks_previos = it->second;
                if (ticks_actuales >= ticks_previos) {
                    unsigned long long delta_proc = ticks_actuales - ticks_previos;
                    cpu_pct = (static_cast<double>(delta_proc) / static_cast<double>(delta_sys)) 
                              * 100.0 * static_cast<double>(num_processors_);
                    
                    double max_possible = 100.0 * static_cast<double>(num_processors_);
                    if (cpu_pct > max_possible) cpu_pct = max_possible;
                }
            }
        }

        pm.cpu_uso = cpu_pct;
        prev_procs_ticks_[pm.pid] = ticks_actuales;

        auto& historial = historico_procs_[pm.pid];
        historial.push_back(cpu_pct);
        if (historial.size() > max_historial_size_) {
            historial.pop_front();
        }
        pm.historico_cpu = historial;

        out_procesos.push_back(std::move(pm));
    }

    limpiar_pids_obsoletos(pids_actuales);
    prev_sys_total_ = sys_crudo.cpu_total;
}

void GestorMetricas::ordenar(std::vector<ProcesoMetricas>& procesos, CriterioOrden criterio) {
    switch (criterio) {
        case CriterioOrden::CPU_DESC:
            std::sort(procesos.begin(), procesos.end(), [](const auto& a, const auto& b) { return a.cpu_uso > b.cpu_uso; });
            break;
        case CriterioOrden::MEM_DESC:
            std::sort(procesos.begin(), procesos.end(), [](const auto& a, const auto& b) { return a.mem_uso > b.mem_uso; });
            break;
        case CriterioOrden::PID_ASC:
            std::sort(procesos.begin(), procesos.end(), [](const auto& a, const auto& b) { return a.pid < b.pid; });
            break;
        case CriterioOrden::TIEMPO_CPU_DESC:
            std::sort(procesos.begin(), procesos.end(), [](const auto& a, const auto& b) { return a.tiempo_cpu_segundos > b.tiempo_cpu_segundos; });
            break;
    }
}

std::vector<ProcesoMetricas> GestorMetricas::filtrar(const std::vector<ProcesoMetricas>& procesos, const std::string& filtro) {
    if (filtro.empty()) return procesos;

    std::vector<ProcesoMetricas> filtrados;
    filtrados.reserve(procesos.size());

    for (const auto& proc : procesos) {
        std::string pid_str = std::to_string(proc.pid);
        if (proc.comando.find(filtro) != std::string::npos || 
            pid_str.find(filtro) != std::string::npos || 
            proc.usuario.find(filtro) != std::string::npos) {
            filtrados.push_back(proc);
        }
    }
    return filtrados;
}


