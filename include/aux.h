#pragma once

#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>

struct Config;

namespace AppConfig {
    const Config& get();
    void set(const Config& newConfig);
}

void createDirectoryIfNotExists(const std::string& directoryPath);

template <typename T>
inline int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

Config loadConfig(const std::string& configPath);
