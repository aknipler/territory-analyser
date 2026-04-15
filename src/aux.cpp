#include "../include/header.h"

namespace {
Config runtimeConfig;
}

namespace AppConfig {

const Config& get() {
    return runtimeConfig;
}

void set(const Config& newConfig) {
    runtimeConfig = newConfig;
}

}


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


Config loadConfig(const std::string& givenConfigPath) {

    std::ifstream file;
    std::string configPath;
    for (const auto& candidate : {givenConfigPath, "../"+givenConfigPath}) {
        file.open(candidate);
        if (file.is_open()) {
            configPath = candidate;
            break;
        }
        file.clear();
    }

    if (!file.is_open()) {
        std::cerr << "Error: Could not open settings.json. Tried: settings.json, ../settings.json" << std::endl;
        std::cerr << "Current path: " << std::filesystem::current_path() << std::endl;
        std::exit(1);
    }

    Config config;
    try {
        json data = json::parse(file);
        config = data.get<Config>();
    } catch (const json::parse_error& e) {
        std::cerr << "Error: Failed to parse config file '" << configPath << "': " << e.what() << std::endl;
        std::cerr << "Current path: " << std::filesystem::current_path() << std::endl;
        std::exit(1);
    } catch (const json::exception& e) {
        std::cerr << "Error: Invalid config schema in '" << configPath << "': " << e.what() << std::endl;
        std::exit(1);
    }
    
    return config;
}