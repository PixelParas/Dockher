#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <fstream>
#include "include/cxxopts.hpp" // For parsing command line options


// Size of stack for the child process
#define STACK_SIZE 1024 * 1024 // 1 MB stack

void write_to_file(const std::string &path, const std::string &value) {
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << path << " — " << strerror(errno) << std::endl;
        exit(1);
    }
    file << value;
    file.close();
}

// Child process function: Runs in the new namespace, executes command
int child_process(void *arg) {
    // [TODO] Automatically install image if not present
    const std::string rootfs = "./images/ubuntu"; // Path to the root filesystem

    // Change the root directory of the container
    if (chroot(rootfs.c_str()) == -1) {
        std::cerr << "Error in chroot: " << strerror(errno) << std::endl;
        exit(1);
    }

    // Change the working directory to "/"
    chdir("/");
    // Set PATH Variables for the container
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games", 1);
    
    // Execute the command passed by the user
    char *const cmd[] = {(char*)"sh", (char*)"-c", (char*)arg, NULL}; // Run the command in a shell
    execvp(cmd[0], cmd);

    // If execvp fails
    std::cerr << "Error in execvp: " << strerror(errno) << std::endl;
    return 1;
}

int main(int argc, char *argv[]) {
    try {
        // Create the option parser
        cxxopts::Options options("dockher", "Mini Docker in C++");

        // Define the CLI options
        // [TODO] Try to set default values for these
        options.add_options()
            ("c,cmd", "Command to run inside container", cxxopts::value<std::string>())
            ("m,mem", "Memory limit (MB)", cxxopts::value<int>())
            ("p,cpu", "CPU limit (shares)", cxxopts::value<int>())
            ("h,help", "Print usage");

        // Parse the command lines
        auto result = options.parse(argc, argv);

        // If help is requested, print usage and exit
        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        // Get values from the parsed options
        std::string cmd = result["cmd"].as<std::string>();
        int mem_limit = result["mem"].as<int>();    //[TODO] Add support for prefixes so that 1GB = 1024MB
        int cpu_limit = result["cpu"].as<int>();

        //Input Validation
        if (cpu_limit < 0 || cpu_limit > 100) {
            std::cerr << "CPU limit must be between 0 and 100 (%)" << std::endl;
            return 1;
        }

        // Print the parsed values for verification
        std::cout << "Parsed values:\n";
        std::cout << "Command to run: " << cmd << std::endl;
        std::cout << "Memory limit: " << mem_limit << " MB" << std::endl;
        std::cout << "CPU limit: " << cpu_limit << " shares" << std::endl;

        // Allocate memory for the child stack
        char *stack = (char *)malloc(STACK_SIZE);
        if (!stack) {
            std::cerr << "Failed to allocate stack for child process" << std::endl;
            return 1;
        }

        // The child process will run in a new PID and mount namespace
        pid_t pid = clone(child_process, stack + STACK_SIZE, CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, (void*)cmd.c_str());
        if (pid == -1) {
            std::cerr << "Error in clone: " << strerror(errno) << std::endl;
            return 1;
        }

    // Only parent reaches here
    std::string pid_str = std::to_string(pid);

    // Unified cgroup v2 directory
    std::string cgroup_path = "/sys/fs/cgroup/dockher_" + pid_str;
    mkdir(cgroup_path.c_str(), 0755); // Create cgroup directory

    // Write memory limit (in bytes)
    write_to_file(cgroup_path + "/memory.max", std::to_string(mem_limit * 1024 * 1024));

    // Write CPU limit: quota and period in microseconds
    // Example: "50000 100000" = 50ms out of every 100ms => 50% CPU
    int cpu_period_us = 100000; // 100ms default period
    int cpu_quota_us = (cpu_limit * cpu_period_us) / 100; // e.g., 50% => 50000

    // If cpu_limit is 0, allow max cpu allocation
    std::string cpu_max_value = (cpu_limit == 0) ? "max" : std::to_string(cpu_quota_us) + " " + std::to_string(cpu_period_us);
    write_to_file(cgroup_path + "/cpu.max", cpu_max_value);

    // Add the child process to the cgroup
    write_to_file(cgroup_path + "/cgroup.procs", pid_str);

    // Wait for the child process to finish
    waitpid(pid, nullptr, 0);

    // Cleanup
    rmdir(cgroup_path.c_str());

    // Free the allocated stack
    free(stack);
    } 
    /*
              All exceptions derive from "cxxopts::exceptions::exception"
     Errors defining options derive from "cxxopts::exceptions::specification"
    Errors parsing arguments derive from "cxxopts::exceptions::parsing"
    */
    catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error defining options: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}