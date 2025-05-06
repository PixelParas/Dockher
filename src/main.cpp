#include <iostream>
#include "include/cxxopts.hpp" // For parsing command line options

int main(int argc, char *argv[]) {
    try {
        // Create the option parser
        cxxopts::Options options("minidocker", "Mini Docker in C++");

        // Define the CLI options
        options.add_options()
            ("c,cmd", "Command to run inside container", cxxopts::value<std::string>())
            ("m,mem", "Memory limit (MB)", cxxopts::value<int>())
            ("p,cpu", "CPU limit (shares)", cxxopts::value<int>())
            ("h,help", "Print usage");

        // Parse the command line
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
