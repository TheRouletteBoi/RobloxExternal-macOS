#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstring>
#include <print>

#include <spawn.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_images.h>

#include <libproc.h>
#include <sys/proc_info.h>

namespace process {

inline std::vector<pid_t> list_all_pids() {
    int size = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (size <= 0)
        return {};

    std::vector<pid_t> pids(size / sizeof(pid_t));
    proc_listpids(PROC_ALL_PIDS, 0, pids.data(), size);
    return pids;
}

inline std::vector<pid_t> find_pids_by_name(std::string_view process_name) {
    std::vector<pid_t> result{};

    for (pid_t pid : list_all_pids()) {
        if (pid == 0)
            continue;

        proc_bsdinfo info{};
        if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info))
            continue;

        if (process_name == info.pbi_name)
            result.push_back(pid);
    }

    return result;
}

// Get task port for a PID (requires root or entitlements)
inline std::optional<task_t> get_task_for_pid_safe(pid_t pid) {
    task_t task = MACH_PORT_NULL;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);

    if (kr != KERN_SUCCESS) {
        std::println("task_for_pid failed for {}: {}", pid, mach_error_string(kr));
        return std::nullopt;
    }

    return task;
}

// Auto select the newest PID (highest PID number)
inline std::optional<pid_t> auto_select_pid(const std::vector<pid_t>& pids) {
    if (pids.empty())
        return std::nullopt;
    return *std::ranges::max_element(pids);
}

// Find process by name and get task port
inline std::optional<task_t> get_task_port_and_pid(std::string_view process_name, pid_t& out_pid) {
    auto pids = find_pids_by_name(process_name);
    if (pids.empty())
        return std::nullopt;

    auto pid = auto_select_pid(pids);
    if (!pid)
        return std::nullopt;

    auto task = get_task_for_pid_safe(*pid);
    if (!task)
        return std::nullopt;

    out_pid = *pid;
    return task;
}

inline bool is_running(std::string_view process_name) {
    return !find_pids_by_name(process_name).empty();
}

inline bool wait_for_process(const std::string& process_name, int timeout_seconds) {
    for (int i = 0; i < timeout_seconds; i++) {
        if (is_running(process_name)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
}

enum class InjectionMode {
    AUTO,
    FORCE_RESTART
};

struct InjectionResult {
    bool success;
    task_t task;
    pid_t pid;

    explicit operator bool() const { return success; }
};

inline std::filesystem::path get_executable_directory() {
    char path[PATH_MAX];
    uint32_t size = sizeof(path);

    if (_NSGetExecutablePath(path, &size) != 0)
        return {};

    return std::filesystem::canonical(path).parent_path();
}

inline std::filesystem::path find_dylib(std::string_view dylib_name) {
    auto dir = get_executable_directory();
    if (dir.empty())
        return {};

    auto dylib = dir / dylib_name;
    if (std::filesystem::exists(dylib))
        return dylib;

    return {};
}

inline bool is_dylib_loaded(task_t task, std::string_view dylib_substring) {
    task_dyld_info_data_t dyld{};
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;

    if (task_info(task, TASK_DYLD_INFO, (task_info_t)&dyld, &count) != KERN_SUCCESS)
        return false;

    dyld_all_image_infos infos{};
    mach_vm_size_t read = 0;

    if (mach_vm_read_overwrite(task,
        dyld.all_image_info_addr,
        sizeof(infos),
        (mach_vm_address_t)&infos,
        &read) != KERN_SUCCESS)
        return false;

    for (uint32_t i = 0; i < infos.infoArrayCount; ++i) {
        dyld_image_info image{};
        if (mach_vm_read_overwrite(task,
            (mach_vm_address_t)infos.infoArray + i * sizeof(image),
            sizeof(image),
            (mach_vm_address_t)&image,
            &read) != KERN_SUCCESS)
            continue;

        char path[PATH_MAX]{};
        mach_vm_read_overwrite(task,
            (mach_vm_address_t)image.imageFilePath,
            sizeof(path),
            (mach_vm_address_t)path,
            &read);

        if (std::string_view(path).contains(dylib_substring))
            return true;
    }

    return false;
}

inline void kill_processes(std::string_view process_name) {
    for (pid_t pid : find_pids_by_name(process_name)) {
        kill(pid, SIGKILL);
    }
}

extern "C" char** environ;

inline pid_t launch_with_dylib(
    const std::filesystem::path& executable_path,
    const std::filesystem::path& dylib_path,
    const std::vector<std::string>& args = {},
    bool verbose_logging = false) {

    if (!std::filesystem::exists(dylib_path)) {
        std::println("[ERROR] dylib missing: {}", dylib_path.string());
        return -1;
    }

    posix_spawn_file_actions_t action;
    posix_spawn_file_actions_init(&action);

    if (!verbose_logging) {
        posix_spawn_file_actions_addopen(&action, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&action, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    }

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(executable_path.c_str()));
    for (auto& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    std::vector<std::string> env_store;
    std::vector<char*> envp;
    for (char** e = environ; *e; ++e) {
        if (std::strncmp(*e, "DYLD_INSERT_LIBRARIES=", 22) != 0)
            env_store.emplace_back(*e);
    }

    env_store.emplace_back("DYLD_INSERT_LIBRARIES=" + dylib_path.string());
    for (auto& s : env_store)
        envp.push_back(s.data());
    envp.push_back(nullptr);

    pid_t pid{};
    int status = posix_spawn(&pid, executable_path.c_str(), &action, nullptr, argv.data(), envp.data());

    posix_spawn_file_actions_destroy(&action);

    if (status != 0) {
        std::println("[ERROR] posix_spawn failed with error: {}", status);
        return -1;
    }

    return pid;
}

inline pid_t launch_roblox_with_dylib(const std::string& dylib_name, bool verbose = false) {
    const std::string roblox_path = "/Applications/Roblox.app/Contents/MacOS/RobloxPlayer";

    std::string dylib_path = find_dylib(dylib_name);
    if (dylib_path.empty()) {
        std::println("[INJECTION] Cannot launch: dylib '{}' not found in injector directory", dylib_name);
        std::println("[INJECTION] Expected location: {}/{}", get_executable_directory().string(), dylib_name);
        return -1;
    }

    return launch_with_dylib(roblox_path, dylib_path, {}, verbose);
}

inline InjectionResult inject_dylib(const std::string& executable_name,
                                    const std::string& dylib_name,
                                    InjectionMode mode,
                                    bool verbose = false) {
    InjectionResult result{false, MACH_PORT_NULL, -1};

    std::string dylib_path = find_dylib(dylib_name);
    if (dylib_path.empty()) {
        std::println("[INJECTION] Cannot launch: dylib '{}' not found in injector directory", dylib_name);
        std::println("[INJECTION] Expected location: {}/{}", get_executable_directory().string(), dylib_name);
        return result;
    }

    bool needs_launch = false;
    pid_t target_pid = -1;

    if (is_running(executable_name)) {

        auto pids = find_pids_by_name(executable_name);

        target_pid = *auto_select_pid(pids);

        if (mode == InjectionMode::FORCE_RESTART) {
            std::println("[INJECTION] Force restart mode active.");
            needs_launch = true;
        } else {
            // Check if we can see inside the process
            auto task = get_task_for_pid_safe(target_pid);
            if (!task) {
                // If we can't get the task (SIP), we can't verify the dylib.
                // We must restart to guarantee injection via DYLD_INSERT_LIBRARIES.
                std::println("[INJECTION] Cannot verify {} (SIP/Permissions). Restarting...", executable_name);
                needs_launch = true;
            } else if (!is_dylib_loaded(*task, dylib_name)) {
                std::println("[INJECTION] {} is running but dylib is missing. Restarting...", executable_name);
                needs_launch = true;
            } else {
                std::println("[INJECTION] {} already injected.", dylib_name);
                result.success = true;
                result.task = *task;
                result.pid = target_pid;
                return result;
            }
        }
    } else {
        std::println("[INJECTION] {} not found. Starting fresh launch...", executable_name);
        needs_launch = true;
    }

    if (needs_launch) {
        if (is_running(executable_name)) {
            kill_processes(executable_name);
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
        }

        pid_t new_pid = launch_roblox_with_dylib(dylib_name, verbose);
        if (new_pid <= 0) {
            std::println("[ERROR] Failed to launch {}", executable_name);
            return result;
        }

        std::println("[INJECTION] Waiting for process {}", executable_name);
        if (!wait_for_process(executable_name, 30)) {
            std::println("[ERROR] Timed out waiting for {}", executable_name);
            return result;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));

        pid_t final_pid = -1;
        auto final_task = get_task_port_and_pid(executable_name, final_pid);

        result.pid = final_pid;
        result.task = final_task.value_or(MACH_PORT_NULL);
        result.success = (final_pid != -1);
    }

    return result;
}

} // namespace process

