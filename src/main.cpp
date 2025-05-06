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
#include "include/cxxopts.hpp" // For parsing command line options


// Size of stack for the child process
#define STACK_SIZE 1024 * 1024 // 1 MB stack

// Child process function: Runs in the new namespace, executes command
int child_process(void *arg) {
    const std::string rootfs = "./images/ubuntu"; // Path to the root filesystem

    // Change the root directory of the container
    if (chroot(rootfs.c_str()) == -1) {
        std::cerr << "Error in chroot: " << strerror(errno) << std::endl;
        exit(1);
    }

    // Change the working directory to "/"
    chdir("/");

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
        cxxopts::Options options("minidocker", "Mini Docker in C++");

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

        // Wait for the child process to finish
        waitpid(pid, nullptr, 0);

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
