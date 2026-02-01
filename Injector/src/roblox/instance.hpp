#pragma once

#include "memory/memory.hpp"
#include "roblox/offsets.hpp"
#include "roblox/math.hpp"
#include "roblox/string.hpp"

#include <vector>
#include <string>
#include <optional>
#include <functional>
#include <print>

namespace roblox {

class Instance;

struct ChildEntry {
    vm_address_t address;
    uint8_t _padding[8];
};

class Instance {
public:
    Instance() : m_task(MACH_PORT_NULL), m_address(0) {}
    
    Instance(task_t task, vm_address_t address) 
        : m_task(task), m_address(address) {}

    bool is_valid() const {
        return m_address != 0 && m_task != MACH_PORT_NULL;
    }
    
    bool exists() const {
        if (!is_valid())
            return false;

        vm_address_t self = 0;
        if (!memory::read_value(m_task, m_address + offsets::INSTANCE_SELF, self)) {
            return false;
        }
        return self == m_address;
    }
    
    explicit operator bool() const {
        return is_valid();
    }

    vm_address_t address() const { return m_address; }
    task_t task() const { return m_task; }
    
    std::optional<std::string> name() const {
        if (!is_valid())
            return std::nullopt;
        return read_rbx_string_at(m_task, m_address, offsets::INSTANCE_NAME);
    }
    
    std::optional<std::string> class_name() const {
        if (!is_valid())
            return std::nullopt;
        
        vm_address_t class_info_ptr = 0;
        if (!memory::read_value(m_task, m_address + offsets::INSTANCE_CLASS_INFO, class_info_ptr)) {
            return std::nullopt;
        }
        if (class_info_ptr == 0)
            return std::nullopt;
        
        vm_address_t class_name_ptr = 0;
        if (!memory::read_value(m_task, class_info_ptr + 8, class_name_ptr)) {
            return std::nullopt;
        }
        
        return read_rbx_string(m_task, class_name_ptr);
    }
    
    Instance parent() const {
        if (!is_valid())
            return {};
        
        vm_address_t parent_addr = 0;
        if (!memory::read_value(m_task, m_address + offsets::INSTANCE_PARENT, parent_addr)) {
            return {};
        }
        return Instance(m_task, parent_addr);
    }

    bool is_a(std::string_view class_name_to_check) const {
        auto cn = class_name();
        return cn && *cn == class_name_to_check;
    }
    
    bool is_named(std::string_view name_to_check) const {
        auto n = name();
        return n && *n == name_to_check;
    }
    
    bool name_contains(std::string_view partial) const {
        auto n = name();
        return n && n->find(partial) != std::string::npos;
    }
    
    bool class_name_contains(std::string_view partial) const {
        auto cn = class_name();
        return cn && cn->find(partial) != std::string::npos;
    }

    std::vector<Instance> children() const {
        std::vector<Instance> result;
        if (!is_valid())
            return result;
        
        vm_address_t children_info_ptr = 0;
        if (!memory::read_value(m_task, m_address + offsets::INSTANCE_CHILDREN, children_info_ptr)) {
            return result;
        }
        if (children_info_ptr == 0)
            return result;
        
        vm_address_t list_start = 0, list_end = 0;
        if (!memory::read_value(m_task, children_info_ptr, list_start))
            return result;

        if (!memory::read_value(m_task, children_info_ptr + 8, list_end))
            return result;
        
        if (list_start >= list_end || list_start == 0) return result;
        
        size_t size = list_end - list_start;
        size_t count = size / sizeof(ChildEntry);

        // Sanity check
        if (count > 10000)
            return result;
        
        std::vector<ChildEntry> entries(count);
        if (!memory::read_bytes(m_task, list_start, entries.data(), size)) {
            return result;
        }
        
        result.reserve(count);
        for (const auto& entry : entries) {
            if (entry.address != 0) {
                result.emplace_back(m_task, entry.address);
            }
        }
        
        return result;
    }
    
    size_t child_count() const {
        if (!is_valid())
            return 0;
        
        vm_address_t children_info_ptr = 0;
        if (!memory::read_value(m_task, m_address + offsets::INSTANCE_CHILDREN, children_info_ptr)) {
            return 0;
        }
        if (children_info_ptr == 0)
            return 0;
        
        vm_address_t list_start = 0, list_end = 0;
        if (!memory::read_value(m_task, children_info_ptr, list_start)) return 0;
        if (!memory::read_value(m_task, children_info_ptr + 8, list_end)) return 0;
        
        if (list_start >= list_end)
            return 0;
        return (list_end - list_start) / sizeof(ChildEntry);
    }

    Instance find_first_child(std::string_view name_to_find) const {
        for (const auto& child : children()) {
            if (child.is_named(name_to_find)) {
                return child;
            }
        }
        return {};
    }
    
    Instance find_first_child_of_class(std::string_view class_to_find) const {
        for (const auto& child : children()) {
            if (child.is_a(class_to_find)) {
                return child;
            }
        }
        return {};
    }
    
    Instance find_first_child_where(std::string_view class_to_find, std::string_view name_to_find) const {
        for (const auto& child : children()) {
            if (child.is_a(class_to_find) && child.is_named(name_to_find)) {
                return child;
            }
        }
        return {};
    }
    
    Instance child_at_index(size_t index) const {
        auto kids = children();
        if (index < kids.size()) {
            return kids[index];
        }
        return {};
    }

    void for_each_child(const std::function<bool(Instance&)>& callback) const {
        if (!is_valid()) return;

        for (auto child : children()) {
            if (!callback(child))
                return;
        }
    }


    void for_each_descendant(const std::function<bool(Instance&)>& callback, int max_depth = 10) const {
        for_each_descendant_impl(callback, 0, max_depth);
    }
    
    std::vector<Instance> get_descendants(int max_depth = 10, size_t max_count = 10000) const {
        std::vector<Instance> result;
        result.reserve(256);
        
        for_each_descendant([&](Instance& inst) {
            if (result.size() >= max_count) return false;
            result.push_back(inst);
            return true;
        }, max_depth);
        
        return result;
    }
    
    Instance find_first_descendant(std::string_view class_to_find, std::string_view name_to_find, 
                                   int max_depth = 10) const {
        Instance found;
        for_each_descendant([&](Instance& inst) {
            if (inst.is_a(class_to_find) && inst.is_named(name_to_find)) {
                found = inst;
                return false; // Stop iteration
            }
            return true;
        }, max_depth);
        return found;
    }
    
    Instance find_first_descendant_of_class(std::string_view class_to_find, int max_depth = 10) const {
        Instance found;
        for_each_descendant([&](Instance& inst) {
            if (inst.is_a(class_to_find)) {
                found = inst;
                return false;
            }
            return true;
        }, max_depth);
        return found;
    }

    void print(int indent = 0) const {
        if (!is_valid()) {
            std::println("{}[nil]", std::string(indent * 2, ' '));
            return;
        }
        
        auto cn = class_name().value_or("?");
        auto n = name().value_or("?");
        std::println("{}[{}] {} ({:#x}) (children: {})", 
                    std::string(indent * 2, ' '), cn, n, m_address, child_count());
    }
    
    void print_tree(int max_depth = 3, int current_depth = 0) const {
        print(current_depth);
        
        if (current_depth >= max_depth) return;
        
        for (const auto& child : children()) {
            child.print_tree(max_depth, current_depth + 1);
        }
    }

    template<typename T>
    std::optional<T> read_property(int offset) const {
        if (!is_valid()) return std::nullopt;
        T value;
        if (memory::read_value(m_task, m_address + offset, value)) {
            return value;
        }
        return std::nullopt;
    }
    
    template<typename T>
    bool write_property(int offset, const T& value) const {
        if (!is_valid()) return false;
        return vm_write(m_task, m_address + offset,
                       reinterpret_cast<vm_offset_t>(&value), sizeof(T)) == KERN_SUCCESS;
    }
    
private:
    task_t m_task;
    vm_address_t m_address;
    
    void for_each_descendant_impl(const std::function<bool(Instance&)>& callback, 
                                  int depth, int max_depth) const {
        if (depth > max_depth || !is_valid()) return;
        
        for (auto child : children()) {
            if (!callback(child)) return;
            child.for_each_descendant_impl(callback, depth + 1, max_depth);
        }
    }
};


inline bool operator==(const Instance& a, const Instance& b) {
    return a.address() == b.address();
}

inline bool operator!=(const Instance& a, const Instance& b) {
    return !(a == b);
}

} // namespace roblox
