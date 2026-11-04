#ifndef EXPERIMENT_TYPES_HPP
#define EXPERIMENT_TYPES_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lab {

// Modelos de ejecucion requeridos
enum class ExecutionModel {
    NATIVE_OS_THREADS, // Hilos nativos (std::thread / pthread directo)
    RUNTIME_TASKS      // Tareas administradas por un runtime (Pool de tareas)
};

// Tipos de pruebas del laboratorio
enum class WorkloadType {
    CPU_BOUND,         // Carga intensiva de procesador
    MEMORY_PRESSURE,   // Reserva de memoria virtual y acceso a paginas
    SHARED_DATA_SYNC   // Acceso concurrente a dato compartido
};

// Mecanismos de sincronizacion para datos compartidos
enum class SyncMechanism {
    NONE,              // Sin sincronizacion (condicion de carrera observable)
    MUTEX,             // Exclusion mutua con std::mutex
    SEMAPHORE          // Semaforo POSIX (sem_t)
};

// Estado de ejecucion del experimento
enum class ExperimentStatus {
    IDLE,
    RUNNING,
    COMPLETED,
    STOPPED_BY_USER,
    FAILED
};

// Configuracion de entrada enviada desde la interfaz TUI al motor
struct ExperimentConfig {
    WorkloadType workload_type = WorkloadType::CPU_BOUND;
    ExecutionModel exec_model = ExecutionModel::NATIVE_OS_THREADS;
    SyncMechanism sync_mech = SyncMechanism::NONE;

    std::size_t num_threads = 4;               // Cantidad de hilos o workers
    std::size_t num_tasks = 16;                // Cantidad de tareas (si usa runtime)

    // Parametros para carga de CPU y Sincronizacion
    std::uint64_t iterations_per_worker = 5000000;

    // Parametros para carga de Memoria Virtual y Paginacion
    std::size_t virtual_mem_mb = 256;          // Memoria virtual total a reservar
    std::size_t touch_percentage = 100;        // Porcentaje de paginas a escribir (RAM/SWAP)
    std::size_t memory_passes = 3;             // Pasadas de lectura/escritura sobre las paginas
    std::size_t hold_memory_ms = 1000;         // Tiempo reteniendo paginas antes de liberar

    // Parametros informativos de limites para registro (aplicados por Integrante 2)
    std::size_t cpu_quota_percent = 100;
    std::size_t memory_limit_mb = 0;
    bool limits_applied_successfully = true;
};

// Metricas en tiempo real para que el Integrante 2 actualice la TUI durante la prueba
struct LiveProgress {
    std::atomic<ExperimentStatus> status{ExperimentStatus::IDLE};
    std::atomic<std::uint64_t> completed_units{0};
    std::atomic<std::size_t> pages_reserved{0};
    std::atomic<std::size_t> pages_touched{0};
    std::atomic<double> elapsed_ms{0.0};
};

// Resultado consolidado de un experimento finalizado
struct ExperimentResult {
    std::uint64_t run_id = 0;
    ExperimentConfig config;
    ExperimentStatus final_status = ExperimentStatus::IDLE;

    double duration_ms = 0.0;
    std::uint64_t total_operations = 0;
    double operations_per_sec = 0.0;

    // Resultados especificos de acceso compartido y sincronizacion
    std::uint64_t expected_shared_value = 0;
    std::uint64_t actual_shared_value = 0;
    std::int64_t lost_updates = 0;

    // Resultados especificos de memoria paginada desde el motor
    std::size_t page_size_bytes = 4096;
    std::size_t virtual_bytes_reserved = 0;
    std::size_t pages_reserved = 0;
    std::size_t pages_touched = 0;
    std::uint64_t memory_checksum = 0;

    // Campos listos para que el Integrante 2 anexe el monitoreo del SO (/proc)
    std::size_t peak_rss_kb = 0;
    std::size_t peak_vmsize_kb = 0;
    std::size_t peak_swap_kb = 0;
    std::uint64_t minor_page_faults = 0;
    std::uint64_t major_page_faults = 0;

    std::string notes;
};

} // namespace lab

#endif // EXPERIMENT_TYPES_HPP