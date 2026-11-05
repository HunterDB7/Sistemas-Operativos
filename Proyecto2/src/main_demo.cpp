#include "ExperimentEngine.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace lab;

void printResultSummary(const ExperimentResult& res) {
    std::cout << "--------------------------------------------------------\n";
    std::cout << "Experimento #" << res.run_id << " | Duracion: " << res.duration_ms << " ms\n";
    std::cout << "Estado final: "
              << (res.final_status == ExperimentStatus::COMPLETED ? "COMPLETADO" :
                 (res.final_status == ExperimentStatus::STOPPED_BY_USER ? "DETENIDO POR USUARIO" : "FALLIDO"))
              << "\n";
    std::cout << "Operaciones totales: " << res.total_operations
              << " (" << static_cast<std::uint64_t>(res.operations_per_sec) << " ops/s)\n";

    if (res.config.workload_type == WorkloadType::SHARED_DATA_SYNC) {
        std::cout << "Valor esperado: " << res.expected_shared_value
                  << " | Valor obtenido: " << res.actual_shared_value
                  << " | Actualizaciones perdidas: " << res.lost_updates << "\n";
    } else if (res.config.workload_type == WorkloadType::MEMORY_PRESSURE) {
        std::cout << "Memoria virtual reservada: " << (res.virtual_bytes_reserved / (1024 * 1024)) << " MB ("
                  << res.pages_reserved << " paginas de " << res.page_size_bytes << " bytes)\n";
        std::cout << "Paginas tocadas en RAM: " << res.pages_touched
                  << " | Checksum: " << res.memory_checksum << "\n";
    }
    std::cout << "Nota: " << res.notes << "\n";
}

int main() {
    ExperimentEngine engine;

    std::cout << "=== 1. PRUEBA DE CPU: HILOS NATIVOS VS TAREAS RUNTIME ===\n";
    ExperimentConfig cpu_cfg;
    cpu_cfg.workload_type = WorkloadType::CPU_BOUND;
    cpu_cfg.num_threads = 4;
    cpu_cfg.iterations_per_worker = 2500000;

    cpu_cfg.exec_model = ExecutionModel::NATIVE_OS_THREADS;
    printResultSummary(engine.runExperiment(cpu_cfg));

    cpu_cfg.exec_model = ExecutionModel::RUNTIME_TASKS;
    cpu_cfg.num_tasks = 16;
    printResultSummary(engine.runExperiment(cpu_cfg));

    std::cout << "\n=== 2. COMPARACION AUTOMATICA: SIN SINCRONIZACION VS MUTEX Y SEMAFORO ===\n";
    ExperimentConfig sync_cfg;
    sync_cfg.num_threads = 4;
    sync_cfg.iterations_per_worker = 500000;
    sync_cfg.exec_model = ExecutionModel::NATIVE_OS_THREADS;

    auto [unsync_res, mutex_res] = engine.compareSyncVsUnsync(sync_cfg, SyncMechanism::MUTEX);
    printResultSummary(unsync_res);
    printResultSummary(mutex_res);

    sync_cfg.workload_type = WorkloadType::SHARED_DATA_SYNC;
    sync_cfg.sync_mech = SyncMechanism::SEMAPHORE;
    printResultSummary(engine.runExperiment(sync_cfg));

    std::cout << "\n=== 3. PRUEBA DE MEMORIA VIRTUAL Y ACCESO A PAGINAS ===\n";
    ExperimentConfig mem_cfg;
    mem_cfg.workload_type = WorkloadType::MEMORY_PRESSURE;
    mem_cfg.exec_model = ExecutionModel::NATIVE_OS_THREADS;
    mem_cfg.num_threads = 4;
    mem_cfg.virtual_mem_mb = 128;
    mem_cfg.touch_percentage = 75;
    mem_cfg.memory_passes = 2;
    mem_cfg.hold_memory_ms = 300;
    printResultSummary(engine.runExperiment(mem_cfg));

    std::cout << "\n=== 4. PRUEBA DE REPETICION DE EXPERIMENTO ===\n";
    printResultSummary(engine.repeatLastExperiment());

    std::cout << "\n=== 5. PRUEBA DE DETENCION SEGURA EN EJECUCION ASINCRONA ===\n";
    cpu_cfg.iterations_per_worker = 100000000;
    engine.startExperimentAsync(cpu_cfg);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    engine.stopExperiment();
    printResultSummary(engine.waitForCompletion());

    return 0;
}