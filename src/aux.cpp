#include "../include/aux.h"


void createDirectoryIfNotExists(const std::string& directoryPath) {
    // create_directories() creates all missing directories in the path.
    // If the directory already exists, it does nothing and no error is reported.
    try {
        if (std::filesystem::create_directories(directoryPath)) {
            std::cout << "Directory created: " << directoryPath << std::endl;
        } else {
            std::cout << "Directory already exists or could not be created for other reasons (e.g., permissions)." << std::endl;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error creating directory: " << e.what() << std::endl;
    }
}
