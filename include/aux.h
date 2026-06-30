#pragma once

#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <unordered_map>
#include <regex>
#include <functional>
#include <chrono>
#include <iomanip>
#include <vector>

struct Config;
struct ObstructionInfo;
class TerritoryAnalyser;

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

/** @brief Parses a obstruction command file and calls analyser.updateObstruction for each valid line.
 *  @return false if the file cannot be opened or contains a malformed command. */
bool loadObstructionCommandsFromFile(TerritoryAnalyser& analyser, const std::string& commandsPath);

/** @brief Runs fn(), logs elapsed wall-clock time labelled with label to stdout, and returns fn()'s result. */
template <typename T>
T measureAndLogExecutionTime(const std::string& label, const std::function<T()>& fn) {
    const auto startTime = std::chrono::high_resolution_clock::now();
    T retVar = fn();
    const auto endTime = std::chrono::high_resolution_clock::now();
    const auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    std::cout << "Time taken to " << label << ": " << durationMs.count() << " ms" << "(" << std::fixed << std::setprecision(3) <<  durationUs.count() << " μs)" << std::endl;
    return retVar;
};

std::vector<std::vector<bool>> rotateBoolBoard90CounterClockwise(const std::vector<std::vector<bool>>& input);

cv::Mat applyTAmatToCAoutput(const cv::Mat& caOutput, const cv::Mat& taMat);