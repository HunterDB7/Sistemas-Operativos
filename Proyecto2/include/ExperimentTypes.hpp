// Estructuras que comparten el motor y la interfaz

#ifndef EXPERIMENT_TYPES_HPP
#define EXPERIMENT_TYPES_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lab {

// Modelos de ejecucion
enum class ExecutionModel {
    NATIVE_OS_THREADS, // Hilos nativos (std::thread)
    RUNTIME_TASKS      // Tareas de un runtime (pool de hilos)
};

// Tipos de prueba
enum class WorkloadType {
    CPU_BOUND,         // Carga de CPU
    MEMORY_PRESSURE,   // Reserva de memoria y acceso a paginas
    SHARED_DATA_SYNC   // Varios hilos usando un dato compartido
};

// Tipos de sincronizacion
enum class SyncMechanism {
    NONE,              // Sin sincronizacion (hay condicion de carrera)
    MUTEX,             // std::mutex
    SEMAPHORE          // Semaforo POSIX (sem_t)
};

// Estado de la prueba
enum class ExperimentStatus {
    IDLE,
    RUNNING,
    COMPLETED,
    STOPPED_BY_USER,
    FAILED
};

// Configuracion de la prueba (la manda la interfaz al motor)
struct ExperimentConfig {
    WorkloadType workload_type = WorkloadType::CPU_BOUND;
    ExecutionModel exec_model = ExecutionModel::NATIVE_OS_THREADS;
    SyncMechanism sync_mech = SyncMechanism::NONE;

    std::size_t num_threads = 4;               // Cantidad de hilos
    std::size_t num_tasks = 16;                // Cantidad de tareas (modo runtime)

    // Para la prueba de CPU y la de dato compartido
    std::uint64_t iterations_per_worker = 5000000;

    // Para la prueba de memoria
    std::size_t virtual_mem_mb = 256;          // Memoria virtual a reservar
    std::size_t touch_percentage = 100;        // Porcentaje de paginas que se escriben
    std::size_t memory_passes = 3;             // Cuantas veces se recorren las paginas
    std::size_t hold_memory_ms = 1000;         // Tiempo que se mantiene la memoria antes de liberarla

    // Limites (los aplica la interfaz, aqui solo se guardan)
    std::size_t cpu_quota_percent = 100;
    std::size_t memory_limit_mb = 0;
    bool limits_applied_successfully = true;
};

// Progreso en vivo, para mostrarlo mientras corre la prueba
struct LiveProgress {
    std::atomic<ExperimentStatus> status{ExperimentStatus::IDLE};
    std::atomic<std::uint64_t> completed_units{0};
    std::atomic<std::size_t> pages_reserved{0};
    std::atomic<std::size_t> pages_touched{0};
    std::atomic<double> elapsed_ms{0.0};
};

// Resultado de una prueba terminada
struct ExperimentResult {
    std::uint64_t run_id = 0;
    ExperimentConfig config;
    ExperimentStatus final_status = ExperimentStatus::IDLE;

    double duration_ms = 0.0;
    std::uint64_t total_operations = 0;
    double operations_per_sec = 0.0;

    // Dato compartido
    std::uint64_t expected_shared_value = 0;
    std::uint64_t actual_shared_value = 0;
    std::int64_t lost_updates = 0;

    // Memoria
    std::size_t page_size_bytes = 4096;
    std::size_t virtual_bytes_reserved = 0;
    std::size_t pages_reserved = 0;
    std::size_t pages_touched = 0;
    std::uint64_t memory_checksum = 0;

    // Mediciones del sistema (las llena la interfaz leyendo /proc)
    std::size_t peak_rss_kb = 0;
    std::size_t peak_vmsize_kb = 0;
    std::size_t peak_swap_kb = 0;
    std::uint64_t minor_page_faults = 0;
    std::uint64_t major_page_faults = 0;

    std::string notes;
};

} // namespace lab

#endif // EXPERIMENT_TYPES_HPP