#include "Workloads.hpp"
#include <cmath>
#include <thread>

namespace lab {

std::uint64_t Workloads::runCpuUnit(
    std::uint64_t iterations,
    const std::atomic<bool>& stop_requested,
    LiveProgress& progress
) {
    constexpr std::uint64_t CHECK_INTERVAL = 10000;
    double accumulator = 1.000001;
    std::uint64_t completed = 0;

    for (std::uint64_t i = 0; i < iterations; ++i) {
        // Calculo para gastar CPU
        accumulator = std::sin(accumulator) * std::cos(accumulator) + 1.000001;
        ++completed;

        if ((i + 1) % CHECK_INTERVAL == 0) {
            progress.completed_units.fetch_add(CHECK_INTERVAL, std::memory_order_relaxed);
            if (stop_requested.load(std::memory_order_relaxed)) {
                break;
            }
        }
    }

    std::uint64_t remainder = completed % CHECK_INTERVAL;
    if (remainder > 0) {
        progress.completed_units.fetch_add(remainder, std::memory_order_relaxed);
    }

    // Para que el compilador no borre el calculo
    if (accumulator == 0.0) {
        return 0;
    }
    return completed;
}

std::uint64_t Workloads::runSharedDataUnit(
    std::uint64_t iterations,
    SyncMechanism sync_mech,
    volatile std::uint64_t& shared_counter,
    std::mutex& mtx,
    sem_t& sem,
    const std::atomic<bool>& stop_requested,
    LiveProgress& progress
) {
    constexpr std::uint64_t CHECK_INTERVAL = 5000;
    std::uint64_t completed = 0;

    for (std::uint64_t i = 0; i < iterations; ++i) {
        if (sync_mech == SyncMechanism::NONE) {
            // Leer, sumar y guardar por separado para que se note la carrera
            std::uint64_t temp = shared_counter;
            temp = temp + 1;
            shared_counter = temp;
        } else if (sync_mech == SyncMechanism::MUTEX) {
            std::lock_guard<std::mutex> lock(mtx);
            std::uint64_t temp = shared_counter;
            shared_counter = temp + 1;
        } else if (sync_mech == SyncMechanism::SEMAPHORE) {
            sem_wait(&sem);
            std::uint64_t temp = shared_counter;
            shared_counter = temp + 1;
            sem_post(&sem);
        }

        ++completed;

        if ((i + 1) % CHECK_INTERVAL == 0) {
            progress.completed_units.fetch_add(CHECK_INTERVAL, std::memory_order_relaxed);
            if (stop_requested.load(std::memory_order_relaxed)) {
                break;
            }
        }
    }

    std::uint64_t remainder = completed % CHECK_INTERVAL;
    if (remainder > 0) {
        progress.completed_units.fetch_add(remainder, std::memory_order_relaxed);
    }

    return completed;
}

std::uint64_t Workloads::touchMemoryPagesUnit(
    std::uint8_t* base_ptr,
    std::size_t start_page,
    std::size_t end_page,
    std::size_t page_size,
    std::size_t passes,
    const std::atomic<bool>& stop_requested,
    LiveProgress& progress
) {
    std::uint64_t local_checksum = 0;
    if (passes == 0) {
        passes = 1;
    }

    for (std::size_t pass = 0; pass < passes; ++pass) {
        for (std::size_t page = start_page; page < end_page; ++page) {
            if (stop_requested.load(std::memory_order_relaxed)) {
                return local_checksum;
            }

            std::size_t offset = page * page_size;
            // Al escribir en la pagina, el SO le asigna memoria real
            base_ptr[offset] = static_cast<std::uint8_t>((page + pass) & 0xFF);
            base_ptr[offset + page_size - 1] = static_cast<std::uint8_t>((page ^ pass) & 0xFF);
            local_checksum += base_ptr[offset] + base_ptr[offset + page_size - 1];

            if (pass == 0) {
                progress.pages_touched.fetch_add(1, std::memory_order_relaxed);
            }
            progress.completed_units.fetch_add(1, std::memory_order_relaxed);
        }
    }

    return local_checksum;
}

} // namespace lab