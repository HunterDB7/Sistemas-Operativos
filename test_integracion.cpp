#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <csignal>
#include "procesos.cpp"

#define VERDE "\033[32m"
#define ROJO "\033[31m"
#define RESET "\033[0m"

void probar_lectura_sistema() {
    std::cout << "[TEST] Probando lectura del sistema (/proc/stat, meminfo, loadavg)... ";
    SistemaInfo sys;
    bool exito = leer_resumen_sistema(sys);
    assert(exito);
    assert(sys.cpu_total > 0);
    assert(sys.mem_total > 0);
    std::cout << VERDE << "EXITOSO" << RESET << std::endl;
}

void probar_proceso_actual() {
    std::cout << "[TEST] Probando lectura del proceso actual (PID propio)... ";
    pid_t pid_actual = getpid();
    ProcesoInfo proc;
    bool exito = leer_proceso(pid_actual, proc);
    assert(exito);
    assert(proc.pid == pid_actual);
    assert(!proc.comando.empty());
    std::cout << VERDE << "EXITOSO" << RESET << std::endl;
}

void probar_obtener_procesos() {
    std::cout << "[TEST] Probando enumeracion de procesos (/proc)... ";
    std::vector<ProcesoInfo> lista = obtener_procesos();
    assert(!lista.empty());
    bool encontrado_propio = false;
    pid_t pid_actual = getpid();
    for (const auto& p : lista) {
        if (p.pid == pid_actual) {
            encontrado_propio = true;
            break;
        }
    }
    assert(encontrado_propio);
    std::cout << VERDE << "EXITOSO" << RESET << std::endl;
}

void probar_gestor_metricas() {
    std::cout << "[TEST] Probando GestorMetricas (actualizacion, ordenamiento, filtrado)... ";
    GestorMetricas gestor;
    std::vector<ProcesoInfo> crudos = obtener_procesos();
    SistemaInfo sys;
    leer_resumen_sistema(sys);

    std::vector<ProcesoMetricas> procs_metricas;
    SistemaMetricas sys_metricas;
    gestor.actualizar(crudos, sys, procs_metricas, sys_metricas);

    assert(!procs_metricas.empty());
    assert(sys_metricas.total_procs > 0);

    GestorMetricas::ordenar(procs_metricas, CriterioOrden::PID_ASC);
    assert(procs_metricas[0].pid <= procs_metricas.back().pid);

    std::string filtro_test = std::to_string(getpid());
    auto filtrados = GestorMetricas::filtrar(procs_metricas, filtro_test);
    assert(!filtrados.empty());

    std::cout << VERDE << "EXITOSO" << RESET << std::endl;
}

void probar_control_procesos_y_senales() {
    std::cout << "[TEST] Probando senales, suspension, reanudacion y terminacion con proceso hijo... ";
    
    pid_t hijo = fork();
    if (hijo < 0) {
        std::cerr << "Error en fork" << std::endl;
        exit(1);
    }

    if (hijo == 0) {
        while (true) {
            usleep(100000);
        }
        exit(0);
    } else {
        usleep(200000);

        // 1. Suspension (SIGSTOP)
        int res_stop = kill(hijo, SIGSTOP);
        assert(res_stop == 0);
        usleep(100000);

        ProcesoInfo p_info;
        bool leido_stop = leer_proceso(hijo, p_info);
        assert(leido_stop);
        assert(p_info.estado == 'T' || p_info.estado == 't');

        // 2. Reanudacion (SIGCONT)
        int res_cont = kill(hijo, SIGCONT);
        assert(res_cont == 0);
        usleep(100000);

        bool leido_cont = leer_proceso(hijo, p_info);
        assert(leido_cont);
        assert(p_info.estado == 'R' || p_info.estado == 'S');

        // 3. Prioridades (Nice)
        errno = 0;
        if (setpriority(PRIO_PROCESS, hijo, 5) == 0) {
            int prio_nueva = getpriority(PRIO_PROCESS, hijo);
            assert(prio_nueva == 5);
        } else {
            assert(errno == EPERM || errno == EACCES);
        }

        // 4. Terminacion (SIGKILL)
        int res_term = kill(hijo, SIGKILL);
        assert(res_term == 0);

        int status;
        waitpid(hijo, &status, 0);
        std::cout << VERDE << "EXITOSO" << RESET << std::endl;
    }
}

void probar_manejo_errores() {
    std::cout << "[TEST] Probando manejo de errores (PID inexistente)... ";
    int pid_inexistente = 999999;
    ProcesoInfo p;
    bool exito = leer_proceso(pid_inexistente, p);
    assert(!exito);

    int res_kill = kill(pid_inexistente, SIGTERM);
    assert(res_kill == -1);
    assert(errno == ESRCH);

    std::cout << VERDE << "EXITOSO" << RESET << std::endl;
}

int main() {
    std::cout << "========================================\n";
    std::cout << "  INICIANDO PRUEBAS DE INTEGRACION      \n";
    std::cout << "========================================\n";

    try {
        probar_lectura_sistema();
        probar_proceso_actual();
        probar_obtener_procesos();
        probar_gestor_metricas();
        probar_control_procesos_y_senales();
        probar_manejo_errores();

        std::cout << "========================================\n";
        std::cout << VERDE << " TODAS LAS PRUEBAS PASARON EXITOSAMENTE \n" << RESET;
        std::cout << "========================================\n";
    } catch (const std::exception& e) {
        std::cerr << ROJO << "[ERROR] Excepcion capturada: " << e.what() << RESET << std::endl;
        return 1;
    } catch (...) {
        std::cerr << ROJO << "[ERROR] Excepcion desconocida capturada." << RESET << std::endl;
        return 1;
    }

    return 0;
}
