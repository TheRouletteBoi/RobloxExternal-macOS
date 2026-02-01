#pragma once

#include "esp_types.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dispatch/dispatch.h>
#include <stdexcept>
#include <string>
#include <cstring>
#include <cerrno>
#include <thread>

class Semaphore {
public:
    explicit Semaphore(long initial_value = 0)
        : m_sem(dispatch_semaphore_create(initial_value))
    {
        if (!m_sem) {
            throw std::runtime_error("dispatch_semaphore_create failed");
        }
    }
    
    ~Semaphore() {
        if (m_sem) {
            dispatch_release(m_sem);
        }
    }

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    Semaphore(Semaphore&& other) noexcept : m_sem(other.m_sem) {
        other.m_sem = nullptr;
    }

    void wait() {
        dispatch_semaphore_wait(m_sem, DISPATCH_TIME_FOREVER);
    }

    bool wait_for(uint32_t timeout_ms) {
        dispatch_time_t timeout = dispatch_time(
            DISPATCH_TIME_NOW, 
            static_cast<int64_t>(timeout_ms) * NSEC_PER_MSEC
        );
        return dispatch_semaphore_wait(m_sem, timeout) == 0;
    }

    bool try_wait() {
        return dispatch_semaphore_wait(m_sem, DISPATCH_TIME_NOW) == 0;
    }

    void post() {
        dispatch_semaphore_signal(m_sem);
    }
    
private:
    dispatch_semaphore_t m_sem = nullptr;
};

class SharedSemaphore {
public:
    static void init(std::atomic<uint32_t>* counter) {
        counter->store(0, std::memory_order_relaxed);
    }

    static void wait(std::atomic<uint32_t>* counter) {
        while (true) {
            uint32_t val = counter->load(std::memory_order_acquire);
            if (val > 0) {
                if (counter->compare_exchange_weak(val, val - 1, 
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    static bool wait_for(std::atomic<uint32_t>* counter, uint32_t timeout_ms) {
        uint32_t elapsed = 0;
        while (elapsed < timeout_ms) {
            uint32_t val = counter->load(std::memory_order_acquire);
            if (val > 0) {
                if (counter->compare_exchange_weak(val, val - 1,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            elapsed += 1;
        }
        return false;
    }

    static bool try_wait(std::atomic<uint32_t>* counter) {
        uint32_t val = counter->load(std::memory_order_acquire);
        if (val > 0) {
            return counter->compare_exchange_strong(val, val - 1,
                std::memory_order_acq_rel, std::memory_order_relaxed);
        }
        return false;
    }

    static void post(std::atomic<uint32_t>* counter) {
        counter->fetch_add(1, std::memory_order_release);
    }
};

// NOTE: macOS doesn't support sem_timedwait, so we use dispatch semaphores
// for timed waits instead of POSIX named semaphores.
class SharedMemory {
public:
    // Create new shared memory (dylib side)
    static SharedMemory create(const std::string& path) {
        return SharedMemory(path, true);
    }
    
    // Open existing shared memory (injector side)
    static SharedMemory open(const std::string& path) {
        return SharedMemory(path, false);
    }
    
    ~SharedMemory() {
        if (m_memory) {
            munmap(m_memory, m_size);
        }
    }

    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& other) noexcept
        : m_memory(other.m_memory)
        , m_size(other.m_size)
        , m_path(std::move(other.m_path))
    {
        other.m_memory = nullptr;
    }
    
    SharedMemoryLayout* get() { return m_memory; }
    const SharedMemoryLayout* get() const { return m_memory; }
    
    SharedMemoryLayout* operator->() { return m_memory; }
    const SharedMemoryLayout* operator->() const { return m_memory; }

    ESPState& state() { return m_memory->state; }
    const ESPState& state() const { return m_memory->state; }

    ESPCommands& commands() { return m_memory->commands; }
    const ESPCommands& commands() const { return m_memory->commands; }

    ESPRenderData& render() { return m_memory->render; }
    const ESPRenderData& render() const { return m_memory->render; }

    ESPBulkData& bulk() { return m_memory->bulk; }
    const ESPBulkData& bulk() const { return m_memory->bulk; }

    ESPConfig& config() { return m_memory->config; }
    const ESPConfig& config() const { return m_memory->config; }
    
private:
    SharedMemory(const std::string& path, bool create)
        : m_path(path), m_size(sizeof(SharedMemoryLayout))
    {
        int flags = O_RDWR;
        if (create) {
            flags |= O_CREAT | O_TRUNC;
        }
        
        int fd = ::open(path.c_str(), flags, 0666);
        if (fd < 0) {
            throw std::runtime_error("Failed to open shared memory: " + 
                                   std::string(strerror(errno)));
        }
        
        if (create) {
            if (ftruncate(fd, m_size) != 0) {
                close(fd);
                throw std::runtime_error("Failed to set file size");
            }
        }
        
        void* addr = mmap(nullptr, m_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        
        if (addr == MAP_FAILED) {
            throw std::runtime_error("Failed to map shared memory");
        }
        
        m_memory = static_cast<SharedMemoryLayout*>(addr);
        
        if (create) {
            std::memset(m_memory, 0, m_size);
            m_memory->magic = 0xE5B12345;
            m_memory->version = 1;

            std::strcpy(m_memory->render.font_name, "Helvetica");
            m_memory->render.font_size = 12;
            m_memory->config.esp_fps = 60;
            m_memory->config.refresh_interval_sec = 5;

            std::string title = "Roblox - " + std::to_string(getpid());
            std::strcpy(m_memory->config.window_title_text, title.c_str());
            
            std::strcpy(m_memory->bulk.dock_badge_text, "*");
        }
    }
    
    SharedMemoryLayout* m_memory = nullptr;
    size_t m_size;
    std::string m_path;
};
