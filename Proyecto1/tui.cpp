// Monitor de procesos (interfaz TUI)
// Usa procesos.cpp para leer /proc y calcular las metricas.
// Aqui estan los hilos, el control de procesos (kill y nice), la pantalla y el main.
//
// Compilar: g++ -std=c++17 tui.cpp -o monitor
// Ejecutar: ./monitor (solo en Linux)

#include "procesos.cpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <map>
#include <set>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <termios.h>

// Hilos

struct HiloInfo {
    int tid;
    char estado;
    std::string nombre;
    double cpu_segundos;
};

// Lee el stat de cada hilo en /proc/[pid]/task
// Si el proceso no existe o no hay permiso devuelve la lista vacia
std::vector<HiloInfo> leer_hilos(int pid) {
    std::vector<HiloInfo> lista;
    fs::path carpeta = fs::path("/proc") / std::to_string(pid) / "task";
    std::error_code ec;
    fs::directory_iterator it(carpeta, ec);
    if (ec) return lista;

    double ticks_por_seg = static_cast<double>(sysconf(_SC_CLK_TCK));

    for (const auto& entrada : it) {
        std::string nombre_dir = entrada.path().filename().string();
        if (nombre_dir.empty() || nombre_dir.find_first_not_of("0123456789") != std::string::npos) continue;

        std::ifstream archivo(entrada.path() / "stat");
        std::string linea;
        if (!std::getline(archivo, linea)) continue;

        size_t abre = linea.find('(');
        size_t cierra = linea.rfind(')');
        if (abre == std::string::npos || cierra == std::string::npos) continue;

        HiloInfo h;
        h.tid = std::stoi(nombre_dir);
        h.nombre = linea.substr(abre + 1, cierra - abre - 1);

        // campos[0] es el campo 3 del stat, entonces el campo N queda en campos[N-3]
        std::istringstream iss(linea.substr(cierra + 2));
        std::vector<std::string> campos;
        std::string campo;
        while (iss >> campo) campos.push_back(campo);
        if (campos.size() < 13) continue;

        h.estado = campos[0][0];
        h.cpu_segundos = (std::stoull(campos[11]) + std::stoull(campos[12])) / ticks_por_seg;  // utime + stime
        lista.push_back(h);
    }

    std::sort(lista.begin(), lista.end(), [](const HiloInfo& a, const HiloInfo& b) { return a.tid < b.tid; });
    return lista;
}

std::string descripcion_estado(char e) {
    switch (e) {
        case 'R': return "corriendo";
        case 'S': return "durmiendo";
        case 'D': return "bloqueado (E/S)";
        case 'T': return "detenido";
        case 't': return "detenido (traza)";
        case 'Z': return "zombi";
        case 'I': return "inactivo";
        case 'X': return "terminado";
        default:  return "otro";
    }
}

// Control de procesos

// Manda la senal con kill() y devuelve el mensaje que se muestra abajo
std::string enviar_senal(int pid, int senal, const std::string& accion, const std::string& comando) {
    std::string quien = "PID " + std::to_string(pid) + " (" + comando + ")";
    if (kill(pid, senal) == 0) {
        return "OK: " + accion + " enviado a " + quien;
    }
    if (errno == ESRCH) return "El proceso " + quien + " ya no existe";
    if (errno == EPERM) return "Sin permiso para actuar sobre " + quien;
    return "Error al actuar sobre " + quien;
}

std::string cambiar_prioridad(int pid, int nice, const std::string& comando) {
    std::string quien = "PID " + std::to_string(pid) + " (" + comando + ")";
    if (setpriority(PRIO_PROCESS, pid, nice) == 0) {
        return "OK: prioridad (nice) de " + quien + " cambiada a " + std::to_string(nice);
    }
    if (errno == ESRCH) return "El proceso " + quien + " ya no existe";
    if (errno == EPERM || errno == EACCES) return "Sin permiso para cambiar la prioridad de " + quien + " (subirla requiere root)";
    return "Error al cambiar la prioridad de " + quien;
}

// Terminal

termios terminal_original;
volatile sig_atomic_t salir = 0;

void restaurar_terminal() {
    // volver a mostrar el cursor, salir de la pantalla alterna y dejar la terminal como estaba
    const char* seq = "\033[0m\033[?25h\033[?1049l";
    if (write(STDOUT_FILENO, seq, strlen(seq)) < 0) {}
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal_original);
}

void iniciar_terminal() {
    tcgetattr(STDIN_FILENO, &terminal_original);
    termios raw = terminal_original;
    raw.c_lflag &= ~(ICANON | ECHO);  // leer tecla por tecla y sin mostrar lo que se escribe
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    atexit(restaurar_terminal);
    const char* seq = "\033[?1049h\033[?25l\033[2J";  // pantalla alterna y cursor oculto
    if (write(STDOUT_FILENO, seq, strlen(seq)) < 0) {}
}

void al_recibir_senal(int) { salir = 1; }  // con Ctrl+C se sale normal

void obtener_tamano(int& alto, int& ancho) {
    winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0 && w.ws_col > 0) {
        alto = w.ws_row;
        ancho = w.ws_col;
    } else {
        alto = 24;
        ancho = 80;
    }
}

// Deja el texto de exactamente n caracteres (lo corta o le agrega espacios)
// Cuenta caracteres y no bytes para no cortar las letras con tilde
std::string ajustar(const std::string& texto, int n) {
    std::string res;
    int cuenta = 0;
    for (size_t i = 0; i < texto.size() && cuenta < n; ) {
        size_t largo = 1;
        unsigned char c = texto[i];
        if (c >= 0xF0) largo = 4;
        else if (c >= 0xE0) largo = 3;
        else if (c >= 0xC0) largo = 2;
        res += texto.substr(i, largo);
        i += largo;
        cuenta++;
    }
    if (cuenta < n) res += std::string(n - cuenta, ' ');
    return res;
}

// Variables globales

enum Vista { VISTA_TABLA, VISTA_ARBOL, VISTA_HILOS };
enum Modo { MODO_NORMAL, MODO_FILTRO, MODO_NICE, MODO_CONFIRMAR };

struct Fila {
    const ProcesoMetricas* p;
    int nivel;  // nivel en el arbol (0 en la tabla normal)
};

GestorMetricas gestor;
std::vector<ProcesoMetricas> procs;   // todos los procesos
SistemaMetricas resumen;
std::vector<ProcesoMetricas> visibles;  // los que se muestran (ya filtrados y ordenados)
std::vector<Fila> filas;
std::vector<HiloInfo> hilos;

Vista vista = VISTA_TABLA;
Modo modo = MODO_NORMAL;
CriterioOrden orden = CriterioOrden::CPU_DESC;
std::string filtro;
std::string entrada;       // lo que el usuario va escribiendo (filtro o nice)
std::string mensaje;       // resultado de la ultima accion
int intervalo_ms = 1000;
int sel_pid = 0;           // proceso seleccionado
int inicio = 0;            // primera fila que se ve (scroll)
int inicio_hilos = 0;
int pid_accion = 0;        // proceso del K o del nice
std::string comando_accion;

const ProcesoMetricas* buscar_proceso(int pid) {
    for (const auto& p : procs) {
        if (p.pid == pid) return &p;
    }
    return nullptr;
}

// Vuelve a leer /proc y recalcula las metricas
void refrescar() {
    std::vector<ProcesoInfo> crudos = obtener_procesos();
    SistemaInfo sistema{};
    leer_resumen_sistema(sistema);
    gestor.actualizar(crudos, sistema, procs, resumen);
}

// Agrega un proceso y despues sus hijos (recursivo)
void agregar_al_arbol(int idx, int nivel, const std::map<int, std::vector<int>>& hijos) {
    filas.push_back({&visibles[idx], nivel});
    auto it = hijos.find(visibles[idx].pid);
    if (it == hijos.end()) return;
    for (int h : it->second) agregar_al_arbol(h, nivel + 1, hijos);
}

// Arma la lista de filas segun la vista
void armar_filas() {
    filas.clear();
    if (vista == VISTA_ARBOL) {
        visibles = procs;
        GestorMetricas::ordenar(visibles, CriterioOrden::PID_ASC);

        std::set<int> pids;
        for (const auto& p : visibles) pids.insert(p.pid);

        std::map<int, std::vector<int>> hijos;
        std::vector<int> raices;
        for (int i = 0; i < (int)visibles.size(); i++) {
            int padre = visibles[i].ppid;
            if (padre != visibles[i].pid && pids.count(padre)) hijos[padre].push_back(i);
            else raices.push_back(i);
        }
        for (int r : raices) agregar_al_arbol(r, 0, hijos);
    } else {
        visibles = GestorMetricas::filtrar(procs, filtro);
        GestorMetricas::ordenar(visibles, orden);
        for (const auto& p : visibles) filas.push_back({&p, 0});
    }

    // si el seleccionado ya no existe, se selecciona el primero
    bool encontrado = false;
    for (const auto& f : filas) {
        if (f.p->pid == sel_pid) encontrado = true;
    }
    if (!encontrado) sel_pid = filas.empty() ? 0 : filas[0].p->pid;
}

int indice_seleccion() {
    for (int i = 0; i < (int)filas.size(); i++) {
        if (filas[i].p->pid == sel_pid) return i;
    }
    return 0;
}

void mover(int cantidad) {
    if (filas.empty()) return;
    int idx = indice_seleccion() + cantidad;
    if (idx < 0) idx = 0;
    if (idx >= (int)filas.size()) idx = (int)filas.size() - 1;
    sel_pid = filas[idx].p->pid;
}

// Dibujar la pantalla

std::string nombre_orden() {
    switch (orden) {
        case CriterioOrden::CPU_DESC: return "%CPU";
        case CriterioOrden::MEM_DESC: return "%MEM";
        case CriterioOrden::PID_ASC: return "PID";
        case CriterioOrden::TIEMPO_CPU_DESC: return "TIEMPO";
    }
    return "";
}

// Historial de CPU como texto
std::string grafico_historial(const std::deque<double>& hist) {
    const std::string niveles = " .:-=+*#%@";
    std::string g;
    for (double v : hist) {
        if (v > 100.0) v = 100.0;
        g += niveles[(int)(v / 100.0 * 9.0)];
    }
    return g;
}

std::string formato_fila(const std::string& pid, const std::string& ppid, const std::string& usuario,
                         char estado, const std::string& cpu, const std::string& mem,
                         const std::string& rss, const std::string& hilos_s, const std::string& ni,
                         const std::string& tiempo, const std::string& comando) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%7s %7s %-10s %c %6s %6s %9s %4s %3s %8s %s",
             pid.c_str(), ppid.c_str(), ajustar(usuario, 10).c_str(), estado, cpu.c_str(), mem.c_str(),
             rss.c_str(), hilos_s.c_str(), ni.c_str(), tiempo.c_str(), comando.c_str());
    return buf;
}

void dibujar() {
    static int alto_prev = 0, ancho_prev = 0;
    int alto, ancho;
    obtener_tamano(alto, ancho);

    std::string out;
    if (alto != alto_prev || ancho != ancho_prev) {
        out += "\033[2J";  // si cambio el tamano de la ventana, limpiar
        alto_prev = alto;
        ancho_prev = ancho;
    }

    // escribe un texto en una fila (ocupa todo el ancho)
    auto linea = [&](int fila, const std::string& texto, const std::string& estilo = "") {
        out += "\033[" + std::to_string(fila) + ";1H" + estilo + ajustar(texto, ancho) + "\033[0m";
    };

    // en la vista de hilos hay que leerlos
    if (vista == VISTA_HILOS) {
        hilos = leer_hilos(sel_pid);
        if (hilos.empty()) {
            mensaje = "El proceso " + std::to_string(sel_pid) + " ya no existe o no hay permiso para consultarlo";
            vista = VISTA_TABLA;
            armar_filas();
        }
    }

    int filas_vis = alto - 8;
    if (filas_vis < 1) filas_vis = 1;

    const ProcesoMetricas* sel = buscar_proceso(sel_pid);

    // resumen (filas 1 a 5)
    char buf[256];
    std::string v = (vista == VISTA_TABLA) ? "tabla" : (vista == VISTA_ARBOL ? "arbol" : "hilos");
    snprintf(buf, sizeof(buf), " MONITOR DE PROCESOS | vista: %s | orden: %s | refresco: %.1fs | filtro: %s",
             v.c_str(), nombre_orden().c_str(), intervalo_ms / 1000.0, filtro.empty() ? "(ninguno)" : filtro.c_str());
    linea(1, buf, "\033[1;44m");

    snprintf(buf, sizeof(buf), " Procesos: %d totales | %d corriendo | %d detenidos | %d zombis | %d bloqueados (E/S)",
             resumen.total_procs, resumen.running_procs, resumen.stopped_procs,
             resumen.zombie_procs, resumen.blocked_procs);
    linea(2, buf);

    snprintf(buf, sizeof(buf), " Carga promedio: %.2f %.2f %.2f", resumen.load_1, resumen.load_5, resumen.load_15);
    linea(3, buf);

    snprintf(buf, sizeof(buf), " Memoria: total %lu MB | usada %lu MB | disponible %lu MB",
             resumen.mem_total / 1024, resumen.mem_used / 1024, resumen.mem_available / 1024);
    linea(4, buf);

    if (vista == VISTA_HILOS && sel) {
        snprintf(buf, sizeof(buf), " Hilos del PID %d (%s): %d hilos", sel->pid, sel->comando.c_str(), (int)hilos.size());
        linea(5, buf);
    } else if (sel) {
        snprintf(buf, sizeof(buf), " Historial CPU PID %d (%s): [%s]",
                 sel->pid, sel->comando.c_str(), grafico_historial(sel->historico_cpu).c_str());
        linea(5, buf);
    } else {
        linea(5, " (sin proceso seleccionado)");
    }

    // tabla (fila 6 es el encabezado, los procesos desde la 7)
    if (vista == VISTA_HILOS) {
        char enc[256];
        snprintf(enc, sizeof(enc), "%8s %-3s %-18s %10s  %s", "TID", "E", "ESTADO", "CPU(s)", "NOMBRE");
        linea(6, enc, "\033[7m");

        if (inicio_hilos > (int)hilos.size() - 1) inicio_hilos = (int)hilos.size() - 1;
        if (inicio_hilos < 0) inicio_hilos = 0;

        for (int i = 0; i < filas_vis; i++) {
            int idx = inicio_hilos + i;
            if (idx >= (int)hilos.size()) {
                linea(7 + i, "");
                continue;
            }
            const HiloInfo& h = hilos[idx];
            char fila[256];
            snprintf(fila, sizeof(fila), "%8d %-3c %-18s %10.2f  %s", h.tid, h.estado,
                     descripcion_estado(h.estado).c_str(), h.cpu_segundos, h.nombre.c_str());
            std::string color = (h.estado == 'Z') ? "\033[31m" : (h.estado == 'D' ? "\033[33m" : "");
            linea(7 + i, fila, color);
        }
    } else {
        linea(6, formato_fila("PID", "PPID", "USUARIO", 'E', "%CPU", "%MEM", "RSS(KB)", "HILOS", "NI", "TIEMPO", "COMANDO"),
              "\033[7m");

        // scroll para que se vea la fila seleccionada
        int idx_sel = indice_seleccion();
        if (idx_sel < inicio) inicio = idx_sel;
        if (idx_sel >= inicio + filas_vis) inicio = idx_sel - filas_vis + 1;
        if (inicio < 0) inicio = 0;

        for (int i = 0; i < filas_vis; i++) {
            int idx = inicio + i;
            if (idx >= (int)filas.size()) {
                linea(7 + i, "");
                continue;
            }
            const ProcesoMetricas& p = *filas[idx].p;
            char cpu[16], mem[16];
            snprintf(cpu, sizeof(cpu), "%.1f", p.cpu_uso);
            snprintf(mem, sizeof(mem), "%.1f", p.mem_uso);

            std::string comando = p.comando;
            if (filas[idx].nivel > 0) comando = std::string(2 * (filas[idx].nivel - 1), ' ') + "└─ " + comando;

            std::string texto = formato_fila(std::to_string(p.pid), std::to_string(p.ppid), p.usuario, p.estado,
                                             cpu, mem, std::to_string(p.mem_rss), std::to_string(p.num_hilos),
                                             std::to_string(p.prioridad), p.tiempo_cpu_formateado, comando);

            // zombi rojo, bloqueado amarillo, detenido cian, seleccionado con colores invertidos
            std::string estilo;
            if (p.flag_zombie) estilo = "\033[31m";
            else if (p.flag_bloqueado) estilo = "\033[33m";
            else if (p.estado == 'T' || p.estado == 't') estilo = "\033[36m";
            if (p.pid == sel_pid) estilo += "\033[7m";
            linea(7 + i, texto, estilo);
        }
    }

    // mensaje (penultima fila) y ayuda o entrada (ultima fila)
    linea(alto - 1, " " + mensaje, "\033[1;32m");

    if (modo == MODO_FILTRO) {
        linea(alto, " Filtrar por nombre, PID o usuario (Enter acepta, Esc cancela, vacio quita filtro): " + entrada + "_");
    } else if (modo == MODO_NICE) {
        linea(alto, " Nuevo nice para PID " + std::to_string(pid_accion) + " (" + comando_accion +
                    ") entre -20 y 19: " + entrada + "_");
    } else if (modo == MODO_CONFIRMAR) {
        linea(alto, " Terminar POR LA FUERZA PID " + std::to_string(pid_accion) + " (" + comando_accion +
                    ")? (s/n)", "\033[1;41m");
    } else if (vista == VISTA_HILOS) {
        linea(alto, " Flechas: mover | h/Esc: volver | k: terminar | K: forzar | s: suspender | r: reanudar | n: nice | q: salir",
              "\033[7m");
    } else {
        linea(alto, " q salir | Flechas mover | c/m/p/e orden | / filtro | t arbol | h hilos | k term | K forzar | s susp | r reanud | n nice | +/- refresco",
              "\033[7m");
    }

    // imprimir todo de una vez
    if (write(STDOUT_FILENO, out.c_str(), out.size()) < 0) {}
}

// Teclado

enum Teclas { T_ARRIBA = 1000, T_ABAJO, T_PGUP, T_PGDN };

bool hay_entrada(int ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    timeval tv{0, ms * 1000};
    return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0;
}

// Lee una tecla (las flechas llegan como ESC [ A, ESC [ B, etc.)
int leer_tecla() {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) <= 0) return -1;
    if (c != 27) return c;

    if (!hay_entrada(30)) return 27;  // solo se presiono ESC
    unsigned char c2, c3;
    if (read(STDIN_FILENO, &c2, 1) <= 0) return 27;
    if (c2 != '[') return 27;
    if (read(STDIN_FILENO, &c3, 1) <= 0) return 27;

    if (c3 == 'A') return T_ARRIBA;
    if (c3 == 'B') return T_ABAJO;
    if (c3 == '5' || c3 == '6') {
        unsigned char tilde;
        if (read(STDIN_FILENO, &tilde, 1) <= 0) return -1;
        return c3 == '5' ? T_PGUP : T_PGDN;
    }
    return -1;
}

// Manda una senal al proceso seleccionado (k, s, r)
void accion_senal(int senal, const std::string& nombre) {
    const ProcesoMetricas* p = buscar_proceso(sel_pid);
    if (!p) {
        mensaje = "No hay proceso seleccionado o ya no existe";
        return;
    }
    mensaje = enviar_senal(p->pid, senal, nombre, p->comando);
    usleep(100000);  // esperar un momento a que se aplique la senal
    refrescar();
}

void procesar_tecla(int t) {
    mensaje.clear();

    // escribiendo el filtro o el nice
    if (modo == MODO_FILTRO || modo == MODO_NICE) {
        if (t == 10 || t == 13) {  // Enter
            if (modo == MODO_FILTRO) {
                filtro = entrada;
                inicio = 0;
            } else {
                char* fin = nullptr;
                long valor = strtol(entrada.c_str(), &fin, 10);
                if (entrada.empty() || *fin != '\0' || valor < -20 || valor > 19) {
                    mensaje = "Valor invalido: el nice debe ser un numero entre -20 y 19";
                } else {
                    mensaje = cambiar_prioridad(pid_accion, (int)valor, comando_accion);
                    refrescar();
                }
            }
            modo = MODO_NORMAL;
        } else if (t == 27) {  // Esc
            modo = MODO_NORMAL;
        } else if (t == 127 || t == 8) {  // Backspace
            if (!entrada.empty()) entrada.pop_back();
        } else if (t >= 32 && t < 256 && t != 127) {
            entrada += (char)t;
        }
        return;
    }

    // confirmando el K
    if (modo == MODO_CONFIRMAR) {
        if (t == 's' || t == 'S') {
            mensaje = enviar_senal(pid_accion, SIGKILL, "SIGKILL (terminar por la fuerza)", comando_accion);
            usleep(100000);
            refrescar();
        } else {
            mensaje = "Cancelado: no se envio ninguna senal";
        }
        modo = MODO_NORMAL;
        return;
    }

    // modo normal
    switch (t) {
        case 'q': salir = 1; break;

        case T_ARRIBA:
            if (vista == VISTA_HILOS) inicio_hilos--; else mover(-1);
            break;
        case T_ABAJO:
            if (vista == VISTA_HILOS) inicio_hilos++; else mover(1);
            break;
        case T_PGUP:
            if (vista == VISTA_HILOS) inicio_hilos -= 10; else mover(-10);
            break;
        case T_PGDN:
            if (vista == VISTA_HILOS) inicio_hilos += 10; else mover(10);
            break;

        case '+':
            if (intervalo_ms < 10000) intervalo_ms += 500;
            break;
        case '-':
            if (intervalo_ms > 500) intervalo_ms -= 500;
            break;

        case 'c': orden = CriterioOrden::CPU_DESC; break;
        case 'm': orden = CriterioOrden::MEM_DESC; break;
        case 'p': orden = CriterioOrden::PID_ASC; break;
        case 'e': orden = CriterioOrden::TIEMPO_CPU_DESC; break;

        case '/':
            modo = MODO_FILTRO;
            entrada.clear();
            break;

        case 't':
            vista = (vista == VISTA_ARBOL) ? VISTA_TABLA : VISTA_ARBOL;
            break;

        case 'h':
            if (vista == VISTA_HILOS) {
                vista = VISTA_TABLA;
            } else if (sel_pid == 0) {
                mensaje = "No hay proceso seleccionado";
            } else {
                vista = VISTA_HILOS;
                inicio_hilos = 0;
            }
            break;

        case 27:
            if (vista == VISTA_HILOS) vista = VISTA_TABLA;
            break;

        case 'k': accion_senal(SIGTERM, "SIGTERM (terminar)"); break;
        case 's': accion_senal(SIGSTOP, "SIGSTOP (suspender)"); break;
        case 'r': accion_senal(SIGCONT, "SIGCONT (reanudar)"); break;

        case 'K':
        case 'n': {
            const ProcesoMetricas* p = buscar_proceso(sel_pid);
            if (!p) {
                mensaje = "No hay proceso seleccionado o ya no existe";
                break;
            }
            pid_accion = p->pid;
            comando_accion = p->comando;
            if (t == 'K') {
                modo = MODO_CONFIRMAR;
            } else {
                modo = MODO_NICE;
                entrada.clear();
                mensaje = "Prioridad (nice) actual de PID " + std::to_string(p->pid) + ": " + std::to_string(p->prioridad);
            }
            break;
        }
    }
}

// Main

int main() {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "Este programa debe ejecutarse en una terminal.\n");
        return 1;
    }
    if (!fs::is_directory("/proc")) {
        fprintf(stderr, "No se encontro /proc: este programa solo funciona en Linux.\n");
        return 1;
    }

    signal(SIGINT, al_recibir_senal);
    signal(SIGTERM, al_recibir_senal);
    signal(SIGWINCH, [](int) {});  // al cambiar el tamano se interrumpe el select y se redibuja

    iniciar_terminal();

    // se lee dos veces para que el primer %CPU no salga en 0
    refrescar();
    usleep(300000);
    refrescar();

    auto ultimo_refresco = std::chrono::steady_clock::now();

    while (!salir) {
        auto ahora = std::chrono::steady_clock::now();
        long transcurrido = std::chrono::duration_cast<std::chrono::milliseconds>(ahora - ultimo_refresco).count();
        if (transcurrido >= intervalo_ms) {
            refrescar();
            ultimo_refresco = ahora;
            transcurrido = 0;
        }

        armar_filas();
        dibujar();

        // esperar una tecla hasta el siguiente refresco
        long restante = intervalo_ms - transcurrido;
        if (restante < 1) restante = 1;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        timeval tv;
        tv.tv_sec = restante / 1000;
        tv.tv_usec = (restante % 1000) * 1000;

        if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
            int tecla = leer_tecla();
            if (tecla != -1) procesar_tecla(tecla);
        }
    }

    return 0;  // atexit deja la terminal normal
}
