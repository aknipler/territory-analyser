#pragma once

#include <filesystem>
#include <iostream>

void createDirectoryIfNotExists(const std::string& directoryPath);

template <typename T>
inline int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}