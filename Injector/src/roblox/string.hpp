#pragma once

#include "memory/memory.hpp"
#include <string>
#include <optional>

namespace roblox {

class StringReader {
public:
    explicit StringReader(task_t task) : m_task(task) {}
    
    // Read a Roblox string from a pointer
    std::optional<std::string> read(vm_address_t string_ptr) {
        if (string_ptr == 0)
            return std::nullopt;

        std::string result;
        if (memory::read_cstring(m_task, string_ptr, result, 4096)) {
            return result;
        }
        
        return std::nullopt;
    }

    std::optional<std::string> read_at_offset(vm_address_t instance, uintptr_t offset) {
        vm_address_t string_ptr = 0;
        if (!memory::read_value(m_task, instance + offset, string_ptr)) {
            return std::nullopt;
        }
        return read(string_ptr);
    }
    
private:
    task_t m_task;
};

inline std::optional<std::string> read_rbx_string(task_t task, vm_address_t string_ptr) {
    return StringReader(task).read(string_ptr);
}

inline std::optional<std::string> read_rbx_string_at(task_t task, vm_address_t base, uintptr_t offset) {
    return StringReader(task).read_at_offset(base, offset);
}

} // namespace roblox
