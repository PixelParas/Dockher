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

#include <pty.h>        // openpty
#include <utmp.h>       // struct utmp
#include <termios.h>    // termios

// Size of stack for the child process
#define STACK_SIZE 1024 * 1024 // 1 MB stack
int slave_fd; // Declared globally or outside main if needed in child


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

    // Set up terminal in the child
    setsid(); // New session
    if (ioctl(slave_fd, TIOCSCTTY, 0) == -1) {
        perror("ioctl TIOCSCTTY");
        exit(1);
    }

    dup2(slave_fd, STDIN_FILENO);
    dup2(slave_fd, STDOUT_FILENO);
    dup2(slave_fd, STDERR_FILENO);
    close(slave_fd);

    // [TODO] Automatically install image if not present
    const std::string rootfs = "./images/ubuntu"; // Path to the root filesystem


    pid_t child = fork();
    if (child < 0) {
        perror("fork in child_process");
        exit(1);
    }

    if (child == 0) {
        // In grandchild: This becomes PID 1 in new PID namespace
        setsid(); // New session leader

        if (ioctl(slave_fd, TIOCSCTTY, 0) == -1) {
            perror("ioctl TIOCSCTTY");
            exit(1);
        }

        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        close(slave_fd);

        if (chroot(rootfs.c_str()) == -1) {
            std::cerr << "chroot failed: " << strerror(errno) << std::endl;
            exit(1);
        }
        chdir("/");
        setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);

        char *const shell[] = { (char*)"sh", NULL };
        execvp(shell[0], shell);
        perror("execvp");
        exit(1);
    } else {
        // In child: wait for grandchild
        int status;
        waitpid(child, &status, 0);
        exit(WEXITSTATUS(status));
    }

    // Change the root directory of the container
    if (chroot(rootfs.c_str()) == -1) {
        std::cerr << "Error in chroot: " << strerror(errno) << std::endl;
        exit(1);
    }

    // Change the working directory to "/"
    chdir("/");

    // Set PATH varibales for the container
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games", 1);


    // Execute the command passed by the user
    char *const shell[] = { (char*)"sh", NULL };
    execvp(shell[0], shell);

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
            ("p,cpu", "CPU limit (%cpu allocation)", cxxopts::value<int>())
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
        std::cout << "CPU limit: " << cpu_limit << "%" << std::endl;

        // Allocate memory for the child stack
        char *stack = (char *)malloc(STACK_SIZE);
        if (!stack) {
            std::cerr << "Failed to allocate stack for child process" << std::endl;
            return 1;
        }

        // Create a pseudo-terminal (PTY) for the child process
        int master_fd;
        if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == -1) {
            perror("openpty failed");
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
        close(slave_fd); // Parent doesn’t use the slave side


        // Unified cgroup v2 directory
        std::string cgroup_path = "/sys/fs/cgroup/dockher_" + pid_str;
        mkdir(cgroup_path.c_str(), 0755); // Create cgroup directory

        // Write memory limit (in bytes)
        write_to_file(cgroup_path + "/memory.max", std::to_string(mem_limit * 1024 * 1024));

        // Write CPU limit: quota and period in microseconds
        // "50000 100000" = 50ms out of every 100ms => 50% CPU
        int cpu_limit_actual = static_cast<int>((static_cast<double>(cpu_limit)/static_cast<double>(100))*100000);
        write_to_file(cgroup_path + "/cpu.max", std::to_string(cpu_limit_actual) + " 100000");  // [TODO]: Make this dynamic based on `cpu_limit`

        // Add the child process to the cgroup
        write_to_file(cgroup_path + "/cgroup.procs", pid_str);

        // Psudo-terminal setup
        char buf[1024];
        while (true) {
            std::cout << "dockher> ";
            std::string user_input;
            std::getline(std::cin, user_input);

            if (user_input == "dockher_exit") break;

            user_input += "\n";
            write(master_fd, user_input.c_str(), user_input.size());

            ssize_t n = read(master_fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                std::cout << buf;
            }
        }

        // Wait for the child process to finish
        waitpid(pid, nullptr, 0);
        close(master_fd);

        // Cleanup
        // [BUG] Sometimes code does not reach here, eg docker_1270 and dockher_4434
        // Notebly 4434 was the container with 512% cpu allocation
        // 1270 seems fine though
        // might be deu to error in "vmstat" and "ls" 
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
