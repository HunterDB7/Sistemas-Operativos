// Interfaz TUI (Integrante 2)
// Aqui se configuran las pruebas, se ejecutan con el motor y se muestran los resultados.

#include <ncurses.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "ExperimentEngine.hpp"
#include "Limites.hpp"
#include "Monitor.hpp"

using namespace lab;

// ---------------- Variables globales ----------------

ExperimentEngine engine;
ExperimentConfig config;                 // la configuracion actual
std::vector<ExperimentResult> resultados; // todas las pruebas, para comparar

std::thread hilo;                        // hilo donde corre la prueba
std::atomic<bool> terminado(false);      // avisa cuando la prueba termino
bool corriendo = false;
ExperimentResult resultadoActual;

// Datos del monitor
InfoMemoria actual;                      // ultima lectura
InfoMemoria base;                        // lectura al empezar la prueba
long picoVirtual = 0;                    // valores maximos (kB) de la prueba
long picoSwap = 0;
std::chrono::steady_clock::time_point inicio;
double duracionFinal = 0;                // ms de la ultima prueba
ExperimentStatus ultimoEstado = ExperimentStatus::IDLE;

std::string mensajeMem = "-";
std::string mensajeCpu = "-";
std::string aviso = "";                  // mensaje para el usuario

int seleccion = 0;
bool verTabla = false;

// ---------------- Configuracion ----------------

const int NUM_ITEMS = 11;
const char* nombres[NUM_ITEMS] = {
    "Tipo de carga",
    "Modelo de ejecucion",
    "Cantidad de hilos",
    "Sincronizacion (dato compartido)",
    "Iteraciones por hilo",
    "Memoria virtual a reservar (MB)",
    "Paginas a tocar (%)",
    "Pasadas sobre las paginas",
    "Tiempo reteniendo memoria (ms)",
    "Limite de memoria (MB, 0 = sin)",
    "Limite de CPU (% de un nucleo, 0 = sin)"
};

// Suma o resta un paso al valor, sin pasarse del minimo ni del maximo
long cambiarNumero(long valor, int dir, long paso, long minimo, long maximo) {
    long v = valor + dir * paso;
    if (v < minimo) v = minimo;
    if (v > maximo) v = maximo;
    return v;
}

// dir es +1 si es flecha derecha o -1 si es flecha izquierda
void cambiarItem(int i, int dir) {
    if (i == 0) {
        config.workload_type = (WorkloadType)(((int)config.workload_type + dir + 3) % 3);
    } else if (i == 1) {
        config.exec_model = (ExecutionModel)(((int)config.exec_model + dir + 2) % 2);
    } else if (i == 2) {
        config.num_threads = cambiarNumero(config.num_threads, dir, 1, 1, 32);
    } else if (i == 3) {
        config.sync_mech = (SyncMechanism)(((int)config.sync_mech + dir + 3) % 3);
    } else if (i == 4) {
        config.iterations_per_worker = cambiarNumero(config.iterations_per_worker, dir, 1000000, 1000000, 100000000);
    } else if (i == 5) {
        config.virtual_mem_mb = cambiarNumero(config.virtual_mem_mb, dir, 64, 64, 1024);
    } else if (i == 6) {
        config.touch_percentage = cambiarNumero(config.touch_percentage, dir, 10, 10, 100);
    } else if (i == 7) {
        config.memory_passes = cambiarNumero(config.memory_passes, dir, 1, 1, 10);
    } else if (i == 8) {
        config.hold_memory_ms = cambiarNumero(config.hold_memory_ms, dir, 500, 0, 5000);
    } else if (i == 9) {
        config.memory_limit_mb = cambiarNumero(config.memory_limit_mb, dir, 64, 0, 2048);
    } else if (i == 10) {
        config.cpu_quota_percent = cambiarNumero(config.cpu_quota_percent, dir, 25, 0, 400);
    }
}

std::string valorItem(int i) {
    if (i == 0) {
        if (config.workload_type == WorkloadType::CPU_BOUND) return "CPU";
        if (config.workload_type == WorkloadType::MEMORY_PRESSURE) return "Memoria";
        return "Dato compartido";
    }
    if (i == 1) {
        return (config.exec_model == ExecutionModel::NATIVE_OS_THREADS) ? "Hilos nativos" : "Tareas de runtime";
    }
    if (i == 2) return std::to_string(config.num_threads);
    if (i == 3) {
        if (config.sync_mech == SyncMechanism::NONE) return "Ninguna";
        if (config.sync_mech == SyncMechanism::MUTEX) return "Mutex";
        return "Semaforo";
    }
    if (i == 4) return std::to_string(config.iterations_per_worker);
    if (i == 5) return std::to_string(config.virtual_mem_mb);
    if (i == 6) return std::to_string(config.touch_percentage);
    if (i == 7) return std::to_string(config.memory_passes);
    if (i == 8) return std::to_string(config.hold_memory_ms);
    if (i == 9) return (config.memory_limit_mb == 0) ? "sin limite" : std::to_string(config.memory_limit_mb);
    return (config.cpu_quota_percent == 0) ? "sin limite" : std::to_string(config.cpu_quota_percent);
}

// ---------------- Control de las pruebas ----------------

void iniciarPrueba(ExperimentConfig cfg) {
    if (corriendo) {
        aviso = "Ya hay una prueba corriendo.";
        return;
    }

    cfg.num_tasks = cfg.num_threads * 4;   // cantidad de tareas para el modo runtime

    // Ponemos los limites. Si alguno falla, se avisa en pantalla.
    bool okMem = aplicarLimiteMemoria(cfg.memory_limit_mb, mensajeMem);
    bool okCpu = aplicarLimiteCpu(cfg.cpu_quota_percent, mensajeCpu);
    cfg.limits_applied_successfully = okMem && okCpu;

    reiniciarPicoRss();
    base = leerInfoMemoria();
    picoVirtual = base.vmsize_kb;
    picoSwap = base.swap_kb;
    inicio = std::chrono::steady_clock::now();
    terminado = false;

    try {
        hilo = std::thread([cfg]() {
            resultadoActual = engine.runExperiment(cfg);
            terminado = true;
        });
    } catch (...) {
        quitarLimiteCpu();
        quitarLimiteMemoria();
        aviso = "No se pudo iniciar la prueba (limite muy bajo?).";
        return;
    }

    corriendo = true;
    ultimoEstado = ExperimentStatus::RUNNING;
    aviso = "Prueba iniciada.";
}

// Se llama cuando la prueba termino
void terminarPrueba() {
    hilo.join();
    quitarLimiteCpu();
    quitarLimiteMemoria();

    InfoMemoria fin = leerInfoMemoria();
    resultadoActual.peak_rss_kb = fin.rss_pico_kb;
    resultadoActual.peak_vmsize_kb = picoVirtual;
    resultadoActual.peak_swap_kb = picoSwap;
    resultadoActual.minor_page_faults = fin.fallos_menores - base.fallos_menores;
    resultadoActual.major_page_faults = fin.fallos_mayores - base.fallos_mayores;

    if (!resultadoActual.config.limits_applied_successfully) {
        resultadoActual.notes += " [UN LIMITE NO SE PUDO APLICAR: la prueba NO estuvo limitada]";
    }

    duracionFinal = resultadoActual.duration_ms;
    ultimoEstado = resultadoActual.final_status;
    resultados.push_back(resultadoActual);
    corriendo = false;
    aviso = resultadoActual.notes;
}

void repetirPrueba() {
    if (resultados.empty()) {
        aviso = "No hay ninguna prueba para repetir.";
        return;
    }
    iniciarPrueba(resultados.back().config);
}

// Lee la memoria y guarda los valores maximos mientras corre la prueba
void actualizarMonitor() {
    actual = leerInfoMemoria();
    if (corriendo) {
        if (actual.vmsize_kb > picoVirtual) picoVirtual = actual.vmsize_kb;
        if (actual.swap_kb > picoSwap) picoSwap = actual.swap_kb;
    }
}

// ---------------- Dibujo de pantallas ----------------

const char* textoEstado(ExperimentStatus e) {
    if (e == ExperimentStatus::IDLE) return "SIN EJECUTAR";
    if (e == ExperimentStatus::RUNNING) return "EJECUTANDO";
    if (e == ExperimentStatus::COMPLETED) return "COMPLETADO";
    if (e == ExperimentStatus::STOPPED_BY_USER) return "DETENIDO POR EL USUARIO";
    return "FALLIDO";
}

// Muestra el mensaje de un limite (en rojo si no se pudo aplicar)
void dibujarLimite(int fila, const char* nombre, const std::string& mensaje) {
    bool fallo = (mensaje.compare(0, 2, "NO") == 0);
    if (fallo) attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(fila, 2, "%-10s %s", nombre, mensaje.c_str());
    if (fallo) attroff(COLOR_PAIR(1) | A_BOLD);
}

void dibujarPrincipal() {
    attron(A_BOLD);
    mvprintw(0, 0, "LABORATORIO DE CONCURRENCIA Y MEMORIA");
    attroff(A_BOLD);

    mvprintw(2, 0, "CONFIGURACION (arriba/abajo: elegir, izquierda/derecha: cambiar)");
    for (int i = 0; i < NUM_ITEMS; i++) {
        if (i == seleccion) attron(A_REVERSE);
        mvprintw(3 + i, 2, "%-40s %-20s", nombres[i], valorItem(i).c_str());
        if (i == seleccion) attroff(A_REVERSE);
    }

    int f = 3 + NUM_ITEMS + 1;
    attron(A_BOLD);
    mvprintw(f, 0, "MONITOR");
    attroff(A_BOLD);

    // Tiempo
    double segundos = duracionFinal / 1000.0;
    if (corriendo) {
        auto ahora = std::chrono::steady_clock::now();
        segundos = std::chrono::duration<double>(ahora - inicio).count();
    }
    mvprintw(f + 1, 2, "Estado: %-24s Duracion: %.2f s", textoEstado(ultimoEstado), segundos);

    // Progreso (lo da el motor)
    const LiveProgress& p = engine.getLiveProgress();
    mvprintw(f + 2, 2, "Unidades completadas: %llu   Paginas reservadas: %zu   Paginas tocadas: %zu",
             (unsigned long long)p.completed_units.load(), p.pages_reserved.load(), p.pages_touched.load());

    // Memoria del programa (virtual, RAM y swap se muestran por separado)
    mvprintw(f + 3, 2, "Memoria virtual: %ld MB   RAM residente (RSS): %ld MB   SWAP del proceso: %ld MB",
             actual.vmsize_kb / 1024, actual.rss_kb / 1024, actual.swap_kb / 1024);

    // Fallos de pagina desde que empezo la prueba
    long menores = actual.fallos_menores - base.fallos_menores;
    long mayores = actual.fallos_mayores - base.fallos_mayores;
    if (!corriendo && !resultados.empty()) {
        menores = resultados.back().minor_page_faults;
        mayores = resultados.back().major_page_faults;
    }
    mvprintw(f + 4, 2, "Fallos de pagina: menores %ld   mayores %ld", menores, mayores);

    // Sistema
    if (actual.swap_total_kb > 0) {
        mvprintw(f + 5, 2, "Sistema: RAM libre %ld de %ld MB   SWAP libre %ld de %ld MB",
                 actual.ram_libre_kb / 1024, actual.ram_total_kb / 1024,
                 actual.swap_libre_kb / 1024, actual.swap_total_kb / 1024);
    } else {
        mvprintw(f + 5, 2, "Sistema: RAM libre %ld de %ld MB   SWAP: no disponible en este sistema",
                 actual.ram_libre_kb / 1024, actual.ram_total_kb / 1024);
    }

    // Limites
    dibujarLimite(f + 6, "Lim. mem.:", mensajeMem);
    dibujarLimite(f + 7, "Lim. CPU:", mensajeCpu);

    mvprintw(f + 9, 0, "%s", aviso.c_str());

    mvprintw(LINES - 1, 0, "[s] iniciar  [x] detener  [r] repetir  [t] tabla comparativa  [c] borrar historial  [q] salir");
}

std::string limitesTexto(const ExperimentConfig& c) {
    if (c.memory_limit_mb == 0 && c.cpu_quota_percent == 0) return "-";
    std::string s = "";
    if (c.memory_limit_mb > 0) s += "M" + std::to_string(c.memory_limit_mb) + " ";
    if (c.cpu_quota_percent > 0) s += "C" + std::to_string(c.cpu_quota_percent) + "% ";
    s += c.limits_applied_successfully ? "ok" : "NO";
    return s;
}

void dibujarTabla() {
    attron(A_BOLD);
    mvprintw(0, 0, "TABLA COMPARATIVA ([t] volver)");
    mvprintw(2, 0, "%-3s %-9s %-7s %3s %-5s %-9s %9s %7s %6s %6s %6s %7s %7s %8s %-12s",
             "#", "Carga", "Modelo", "Hil", "Sync", "Estado", "Dur(ms)", "Mops/s",
             "Virt", "RSS", "Swap", "Menor", "Mayor", "Perdidas", "Limites");
    attroff(A_BOLD);
    mvprintw(3, 0, "(Virt, RSS y Swap en MB; Menor/Mayor = fallos de pagina)");

    // Si hay muchas filas, mostramos solo las ultimas
    int maxFilas = LINES - 7;
    int desde = 0;
    if ((int)resultados.size() > maxFilas) desde = resultados.size() - maxFilas;

    int fila = 4;
    for (int i = desde; i < (int)resultados.size(); i++) {
        const ExperimentResult& r = resultados[i];

        std::string carga = "CPU";
        if (r.config.workload_type == WorkloadType::MEMORY_PRESSURE) {
            carga = "Mem " + std::to_string(r.config.virtual_mem_mb);
        } else if (r.config.workload_type == WorkloadType::SHARED_DATA_SYNC) {
            carga = "Datos";
        }

        std::string sync = "-";
        if (r.config.workload_type == WorkloadType::SHARED_DATA_SYNC) {
            if (r.config.sync_mech == SyncMechanism::NONE) sync = "No";
            else if (r.config.sync_mech == SyncMechanism::MUTEX) sync = "Mutex";
            else sync = "Sem";
        }

        std::string estado = "OK";
        if (r.final_status == ExperimentStatus::STOPPED_BY_USER) estado = "Detenido";
        if (r.final_status == ExperimentStatus::FAILED) estado = "Fallido";

        mvprintw(fila, 0, "%-3llu %-9s %-7s %3zu %-5s %-9s %9.1f %7.2f %6ld %6ld %6ld %7llu %7llu %8lld %-12s",
                 (unsigned long long)r.run_id, carga.c_str(),
                 (r.config.exec_model == ExecutionModel::NATIVE_OS_THREADS) ? "Nativo" : "Runtime",
                 r.config.num_threads, sync.c_str(), estado.c_str(),
                 r.duration_ms, r.operations_per_sec / 1000000.0,
                 (long)(r.peak_vmsize_kb / 1024), (long)(r.peak_rss_kb / 1024), (long)(r.peak_swap_kb / 1024),
                 (unsigned long long)r.minor_page_faults, (unsigned long long)r.major_page_faults,
                 (long long)r.lost_updates, limitesTexto(r.config).c_str());
        fila++;
    }

    if (resultados.empty()) {
        mvprintw(4, 0, "Todavia no hay pruebas. Ejecuta alguna con [s].");
    }
    mvprintw(LINES - 1, 0, "[t] volver  [c] borrar historial  [q] salir");
}

// ---------------- Programa principal ----------------

int main() {
    // Valores iniciales (sin limites)
    config.memory_limit_mb = 0;
    config.cpu_quota_percent = 0;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(200);            // getch espera 200 ms, asi la pantalla se actualiza sola
    start_color();
    use_default_colors();
    init_pair(1, COLOR_RED, -1);

    bool salir = false;
    while (!salir) {
        actualizarMonitor();
        if (corriendo && terminado) {
            terminarPrueba();
        }

        erase();
        if (verTabla) dibujarTabla();
        else dibujarPrincipal();
        refresh();

        int tecla = getch();

        if (tecla == 'q') {
            salir = true;
        } else if (tecla == 't') {
            verTabla = !verTabla;
        } else if (tecla == 's') {
            iniciarPrueba(config);
        } else if (tecla == 'x') {
            if (corriendo) {
                engine.stopExperiment();
                aviso = "Deteniendo la prueba...";
            }
        } else if (tecla == 'r') {
            repetirPrueba();
        } else if (tecla == 'c') {
            if (!corriendo) {
                resultados.clear();
                engine.clearHistory();
                aviso = "Historial borrado.";
            }
        } else if (tecla == KEY_UP) {
            seleccion = (seleccion + NUM_ITEMS - 1) % NUM_ITEMS;
        } else if (tecla == KEY_DOWN) {
            seleccion = (seleccion + 1) % NUM_ITEMS;
        } else if (tecla == KEY_LEFT && !corriendo) {
            cambiarItem(seleccion, -1);
        } else if (tecla == KEY_RIGHT && !corriendo) {
            cambiarItem(seleccion, +1);
        }
    }

    // Si salen con una prueba corriendo, la detenemos antes de cerrar
    if (corriendo) {
        engine.stopExperiment();
        hilo.join();
        quitarLimiteCpu();
        quitarLimiteMemoria();
    }

    endwin();
    return 0;
}
