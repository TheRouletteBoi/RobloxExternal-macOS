#pragma once

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <cstring>
#include <string>
#include <vector>
#include <type_traits>
#include <algorithm>

namespace memory {

inline bool read_bytes(task_t task, vm_address_t address, void* out, size_t size) {
    uint8_t* dst = static_cast<uint8_t*>(out);
    size_t total_read = 0;

    while (total_read < size) {
        vm_size_t to_read = std::min<size_t>(0x1000, size - total_read);
        vm_size_t bytes_read = 0;

        kern_return_t kr = vm_read_overwrite(
            task,
            address + total_read,
            to_read,
            reinterpret_cast<vm_address_t>(dst + total_read),
            &bytes_read
        );

        if (kr != KERN_SUCCESS || bytes_read == 0)
            return false;

        total_read += bytes_read;
    }

    return true;
}

template <typename T>
inline bool read_value(task_t task, vm_address_t address, T& out) {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    return read_bytes(task, address, &out, sizeof(T));
}

inline bool read_cstring(task_t task, vm_address_t addr, std::string& out, size_t max_len = 4096) {
    std::vector<char> buffer(max_len);
    if (!read_bytes(task, addr, buffer.data(), max_len)) {
        return false;
    }

    size_t len = strnlen(buffer.data(), max_len);
    out.assign(buffer.data(), len);
    return true;
}

inline std::vector<uint8_t> read_buffer(task_t task, vm_address_t address, size_t size) {
    std::vector<uint8_t> buffer(size);
    if (!read_bytes(task, address, buffer.data(), size)) {
        return {};
    }
    return buffer;
}

inline bool read_large(task_t task, mach_vm_address_t address, size_t size, void* out) {
    mach_vm_size_t bytes_read = 0;
    kern_return_t kr = mach_vm_read_overwrite(
        task,
        address,
        size,
        reinterpret_cast<mach_vm_address_t>(out),
        &bytes_read
    );
    return kr == KERN_SUCCESS && bytes_read == size;
}

inline std::optional<vm_address_t> read_pointer_chain(
    task_t task, 
    vm_address_t base, 
    const std::vector<vm_offset_t>& offsets) 
{
    vm_address_t addr = base;
    
    for (size_t i = 0; i < offsets.size(); ++i) {
        addr += offsets[i];
        
        // Don't dereference the last offset
        if (i < offsets.size() - 1) {
            vm_address_t next = 0;
            if (!read_value(task, addr, next) || next == 0) {
                return std::nullopt;
            }
            addr = next;
        }
    }
    
    return addr;
}

} // namespace memory
