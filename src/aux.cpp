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



std::vector<std::vector<bool>> rotateBoolBoard90CounterClockwise(const std::vector<std::vector<bool>>& input) {
    if (input.empty()) {
        return input;
    }

    const size_t inputWidth = input.size();
    const size_t inputHeight = input.front().size();
    std::vector<std::vector<bool>> rotated(inputHeight, std::vector<bool>(inputWidth, false));

    for (size_t x = 0; x < inputWidth; ++x) {
        for (size_t y = 0; y < inputHeight; ++y) {
            rotated[y][inputWidth - 1 - x] = input[x][y];
        }
    }

    return rotated;
}


// ---------- loadConfig ----------

/**
 * @brief Parses the JSON settings file at configPath (also tries "../" + configPath as a fallback)
 *        into a Config struct.
 * @note  Exits with an error message if the file cannot be opened or the JSON schema does not match.
 */
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

// ---------- loadObstructionCommandsFromFile ----------

/**
 * @brief Reads a text file of analyser.updateObstruction(...) commands (one per line), parses each
 *        with a regex, and calls updateObstruction on analyser.  Lines beginning with '#' and blank
 *        lines are skipped.
 * @return false on file-open failure or any malformed line.
 */
bool loadObstructionCommandsFromFile(TerritoryAnalyser& analyser, const std::string& commandsPath) {
    std::ifstream file(commandsPath);
    std::string usedPath = commandsPath;

    if (!file.is_open()) {
        usedPath = "../" + commandsPath;
        file.open(usedPath);
    }

    if (!file.is_open()) {
        std::cerr << "Error: Could not open command file. Tried: "
                  << commandsPath << ", " << ("../" + commandsPath) << std::endl;
        return false;
    }

    const std::regex commandPattern(
        R"(^\s*analyser\.updateObstruction\(\s*(\d+)\s*,\s*(\d+)\s*,\s*\"([^\"]+)\"\s*,\s*(-?\d+)\s*,\s*\"([^\"]+)\"\s*\)\s*;\s*$)");

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const std::size_t firstContent = line.find_first_not_of(" \t\r\n");
        if (firstContent == std::string::npos || line[firstContent] == '#') {
            continue;
        }

        std::smatch match;
        if (!std::regex_match(line, match, commandPattern)) {
            std::cerr << "Error: Invalid command in " << usedPath << " at line "
                      << lineNumber << ": " << line << std::endl;
            return false;
        }

        analyser.updateObstructionState(
            static_cast<size_t>(std::stoul(match[1].str())),
            static_cast<size_t>(std::stoul(match[2].str())),
            match[3].str(),
            std::stoi(match[4].str()),
            match[5].str());
    }

    return true;
}

// ---------- outputForTests ----------

/**
 * @brief Renders a labelled 4x2 diagnostic grid (player row and team row) containing the final
 *        territory map, raw bitmask board, gap cells, and obstruction/walkability boards.
 *        Upscales the composite and saves it to config.outputDirectory/fileName.png.
 * @return true on successful write.
 */
bool outputForTests(const TerritoryAnalyser& analyser, const Config& config, const std::string& fileName) {
    auto toBgra = [](const cv::Mat& src) {
        if (src.channels() == 4) return src.clone();
        cv::Mat out;
        if (src.channels() == 3) {
            cv::cvtColor(src, out, cv::COLOR_BGR2BGRA);
        } else {
            cv::cvtColor(src, out, cv::COLOR_GRAY2BGRA);
        }
        return out;
    };

    cv::Mat playerFinal = toBgra(analyser.getFinalTerritoryMap("player"));
    cv::Mat teamFinal = toBgra(analyser.getFinalTerritoryMap("team"));

    auto visualizeMaskBoard = [mapSize = config.mapSize](
        const std::vector<std::vector<double>>& board,
        const std::map<int, std::array<int, 4>>& palette) {
        cv::Mat visual(mapSize, mapSize, CV_8UC4, cv::Scalar(0, 0, 0, 255));
        for (size_t x = 0; x < mapSize; ++x) {
            for (size_t y = 0; y < mapSize; ++y) {
                if (x >= board.size() || y >= board[x].size()) continue;
                const double raw = board[x][y];
                if (raw <= 0.0) continue;

                const size_t mask = static_cast<size_t>(std::llround(raw));
                if (mask == 0) continue;

                if ((mask & (mask - 1)) == 0) {
                    const int owner = static_cast<int>(std::log2(mask)) + 1;
                    auto it = palette.find(owner);
                    if (it != palette.end()) {
                        const auto& c = it->second;
                        visual.at<cv::Vec4b>(x, y) = cv::Vec4b(
                            static_cast<uchar>(c[0]),
                            static_cast<uchar>(c[1]),
                            static_cast<uchar>(c[2]),
                            255);
                    }
                } else {
                    // Multi-bit ownership value: show as contested in gray.
                    visual.at<cv::Vec4b>(x, y) = cv::Vec4b(140, 140, 140, 255);
                }
            }
        }
        return visual;
    };

    cv::Mat masterPlayerVisual = visualizeMaskBoard(analyser.getMasterBoard("player"), config.playerColours);
    cv::Mat masterTeamVisual = visualizeMaskBoard(analyser.getMasterBoard("team"), config.teamColours);

    auto visualizeGapCells = [mapSize = config.mapSize](
        const std::vector<std::vector<std::pair<size_t, size_t>>>& gaps,
        const std::map<int, std::array<int, 4>>& palette) {
        cv::Mat visual(mapSize, mapSize, CV_8UC4, cv::Scalar(0, 0, 0, 255));
        for (size_t owner = 0; owner < gaps.size(); ++owner) {
            auto it = palette.find(static_cast<int>(owner));
            if (it == palette.end()) continue;
            const auto& c = it->second;
            const cv::Vec4b colour(
                static_cast<uchar>(c[0]),
                static_cast<uchar>(c[1]),
                static_cast<uchar>(c[2]),
                255);

            for (const auto& cell : gaps[owner]) {
                if (cell.first >= mapSize || cell.second >= mapSize) continue;
                visual.at<cv::Vec4b>(cell.first, cell.second) = colour;
            }
        }
        return visual;
    };

    cv::Mat playerGapCellsVisual = visualizeGapCells(analyser.getGaps("player"), config.playerColours);
    cv::Mat teamGapCellsVisual = visualizeGapCells(analyser.getGaps("team"), config.teamColours);

    const auto obstructionBoard = analyser.getMasterObstructionBoard("player");
    cv::Mat obstructionVisual(config.mapSize, config.mapSize, CV_8UC4, cv::Scalar(0, 0, 0, 255));
    for (size_t x = 0; x < config.mapSize; ++x) {
        for (size_t y = 0; y < config.mapSize; ++y) {
            if (obstructionBoard[x][y] == -1) continue;
            if (obstructionBoard[x][y] == 0) {
                obstructionVisual.at<cv::Vec4b>(x, y) = cv::Vec4b(255, 255, 255, 255);
                continue;
            }
            size_t num = static_cast<size_t>(std::log2(obstructionBoard[x][y])) + 1;
            cv::Vec4b colour(
                static_cast<uchar>(config.playerColours.at(num)[0]),
                static_cast<uchar>(config.playerColours.at(num)[1]),
                static_cast<uchar>(config.playerColours.at(num)[2]),
                255);
            obstructionVisual.at<cv::Vec4b>(x, y) = colour;
        }
    }

    const auto walkableBoard = analyser.getWalkableTerrainBoard();
    cv::Mat walkableVisual(config.mapSize, config.mapSize, CV_8UC4, cv::Scalar(255, 0, 0, 255)); // blue for non-walkable
    for (size_t x = 0; x < config.mapSize; ++x) {
        for (size_t y = 0; y < config.mapSize; ++y) {
            if (walkableBoard[x][y]) {
                walkableVisual.at<cv::Vec4b>(x, y) = cv::Vec4b(0, 255, 0, 255); // green for walkable
            }
        }
    }

    const int outputScale = 4;
    auto upscaleImage = [outputScale](const cv::Mat& src) {
        cv::Mat scaled;
        cv::resize(src, scaled, cv::Size(), outputScale, outputScale, cv::INTER_NEAREST);
        return scaled;
    };

    cv::rotate(masterPlayerVisual, masterPlayerVisual, cv::ROTATE_90_COUNTERCLOCKWISE);
    cv::rotate(masterTeamVisual, masterTeamVisual, cv::ROTATE_90_COUNTERCLOCKWISE);
    cv::rotate(playerGapCellsVisual, playerGapCellsVisual, cv::ROTATE_90_COUNTERCLOCKWISE);
    cv::rotate(teamGapCellsVisual, teamGapCellsVisual, cv::ROTATE_90_COUNTERCLOCKWISE);
    cv::rotate(obstructionVisual, obstructionVisual, cv::ROTATE_90_COUNTERCLOCKWISE);
    cv::rotate(walkableVisual, walkableVisual, cv::ROTATE_90_COUNTERCLOCKWISE);

    playerFinal = upscaleImage(playerFinal);
    teamFinal = upscaleImage(teamFinal);
    masterPlayerVisual = upscaleImage(masterPlayerVisual);
    masterTeamVisual = upscaleImage(masterTeamVisual);
    playerGapCellsVisual = upscaleImage(playerGapCellsVisual);
    teamGapCellsVisual = upscaleImage(teamGapCellsVisual);
    obstructionVisual = upscaleImage(obstructionVisual);
    walkableVisual = upscaleImage(walkableVisual);

    cv::Mat topRow, bottomRow, testGrid;
    cv::hconcat(std::vector<cv::Mat>{playerFinal, masterPlayerVisual, playerGapCellsVisual, obstructionVisual}, topRow);
    cv::hconcat(std::vector<cv::Mat>{teamFinal, masterTeamVisual, teamGapCellsVisual, walkableVisual}, bottomRow);
    cv::vconcat(topRow, bottomRow, testGrid);

    const int leftLabelWidth = 100;
    const int topLabelHeight = 55;
    const int rightPadding = 20;
    const int bottomPadding = 20;

    cv::Mat labeledGrid(
        testGrid.size().height + topLabelHeight + bottomPadding,
        testGrid.size().width + leftLabelWidth + rightPadding,
        CV_8UC4,
        cv::Scalar(30, 30, 30, 255));

    testGrid.copyTo(labeledGrid(cv::Rect(leftLabelWidth, topLabelHeight, testGrid.size().width, testGrid.size().height)));

    const cv::Scalar separatorColor(255, 255, 255, 255);
    const int separatorThickness = 1;
    const int cellSize = static_cast<int>(config.mapSize) * outputScale;

    for (int xIndex = 0; xIndex <= 4; ++xIndex) {
        const int x = leftLabelWidth + (xIndex * cellSize);
        cv::line(labeledGrid, cv::Point(x, 0), cv::Point(x, topLabelHeight + testGrid.size().height), separatorColor, separatorThickness, cv::LINE_AA);
    }

    for (int yIndex = 0; yIndex <= 2; ++yIndex) {
        const int y = topLabelHeight + (yIndex * cellSize);
        cv::line(labeledGrid, cv::Point(0, y), cv::Point(leftLabelWidth + testGrid.size().width, y), separatorColor, separatorThickness, cv::LINE_AA);
    }

    cv::rectangle(
        labeledGrid,
        cv::Rect(0, 0, leftLabelWidth + testGrid.size().width, topLabelHeight + testGrid.size().height),
        separatorColor,
        separatorThickness,
        cv::LINE_AA);

    const int fontFace = cv::FONT_HERSHEY_SIMPLEX;
    const int thickness = 1;
    const cv::Scalar textColor(235, 235, 235, 255);

    auto drawCenteredText = [&](const std::string& text, int centerX, int centerY, double fontScale = 0.75) {
        int baseline = 0;
        const cv::Size textSize = cv::getTextSize(text, fontFace, fontScale, thickness, &baseline);
        const int x = centerX - (textSize.width / 2);
        const int y = centerY + (textSize.height / 2);
        cv::putText(labeledGrid, text, cv::Point(x, y), fontFace, fontScale, textColor, thickness, cv::LINE_AA);
    };

    const std::vector<std::string> xLabels = {
        "Final",
        "Raw terr",
        "Gaps",
        "Obs/Wlkable"
    };

    for (int xIndex = 0; xIndex < static_cast<int>(xLabels.size()); ++xIndex) {
        const int centerX = leftLabelWidth + (xIndex * cellSize) + (cellSize / 2);
        drawCenteredText(xLabels[xIndex], centerX, topLabelHeight / 2, 0.55);
    }

    drawCenteredText("Player", leftLabelWidth / 2, topLabelHeight + (cellSize / 2));
    drawCenteredText("Team", leftLabelWidth / 2, topLabelHeight + cellSize + (cellSize / 2));

    const std::filesystem::path outputPath = std::filesystem::path(config.outputDirectory) / (fileName + ".png");
    const std::filesystem::path parentDir = outputPath.parent_path();
    if (!parentDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parentDir, ec);
        if (ec) {
            std::cerr << "Error: Could not create output directory '" << parentDir.string()
                      << "': " << ec.message() << std::endl;
            return false;
        }
    }

    const bool wrote = cv::imwrite(outputPath.string(), labeledGrid);
    if (!wrote) {
        std::cerr << "Error: cv::imwrite failed for '" << outputPath.string() << "'" << std::endl;
    }
    return wrote;
}




cv::Mat applyTAmatToCAoutput(const cv::Mat& caOutput, const cv::Mat& taMat)
{
    // Nothing to blend (e.g. the CA minimap failed to load — a relative imread that doesn't resolve
    // from the run cwd, or simply absent under a non-AAA example). Fall back to whichever input is
    // non-empty so the caller gets a writable image: cv::imwrite asserts (SIGABRT) on an empty Mat.
    if (caOutput.empty() || taMat.empty()) {
        if (!caOutput.empty()) return caOutput.clone();
        return taMat.clone(); // may still be empty if both are; caller guards below
    }

    const int rows = caOutput.rows;
    const int cols = caOutput.cols;
    cv::Mat taMatResized;

    // Rotate TA matrix 45 degrees to align with CA orientation
        double angle = -45.0; 
        cv::Point2f center((taMat.cols - 1) / 2.0, (taMat.rows - 1) / 2.0);
        cv::Mat rot = cv::getRotationMatrix2D(center, angle, 1.0);

        // Step 1: Determine the size of the new bounding rectangle
        cv::Rect2f bbox = cv::RotatedRect(cv::Point2f(), taMat.size(), angle).boundingRect2f();

        // Step 2: Adjust the transformation matrix translation to fit the new center
        rot.at<double>(0, 2) += bbox.width / 2.0 - taMat.cols / 2.0;
        rot.at<double>(1, 2) += bbox.height / 2.0 - taMat.rows / 2.0;

        // Step 3: Warp using the expanded canvas size
        cv::Mat taMatRotated;
        cv::warpAffine(taMat, taMatRotated, rot, bbox.size(), cv::INTER_CUBIC, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0, 0));
    // 

    if (caOutput.size() != taMatRotated.size()) {
        cv::resize(taMatRotated, taMatResized, caOutput.size(), 0, 0, cv::INTER_CUBIC);
    } else {
        taMatResized = taMatRotated;
    }

    // Create a mask to hold the TA matrix values
    cv::Mat mask, alphaBlend, alphaChannel, alphaChannelFloat, maskFloat;
    alphaChannel.create(taMatResized.size(), CV_8UC1);

    // Define lower and upper bounds for your specific threshold range
    cv::Scalar lowerBound(35, 90, 40); // dark green, BGR
    cv::Scalar upperBound(121, 190, 115); // light green

    // Create a mask on the CA image
    cv::inRange(caOutput, lowerBound, upperBound, mask);
    mask.convertTo(maskFloat, CV_32F, 1.0 / 255.0);

    // Get the blend from the TA output i.e. setup alphaBlend
    cv::extractChannel(taMatResized, alphaChannel, 3);
    alphaChannel.convertTo(alphaChannelFloat, CV_32F, 1.0 / 255.0);
    cv::imwrite("output/alphaChannel.png", alphaChannel);
    cv::multiply(maskFloat, alphaChannelFloat, alphaBlend);
    cv::imwrite("output/alphaBlend.png", alphaBlend);

    cv::Mat alphaBlend3ch;
    cv::Mat in[] = { alphaBlend, alphaBlend, alphaBlend };
    cv::merge(in, 3, alphaBlend3ch);

    // Create 3 channel version of TA image
    std::vector<cv::Mat> bgraChannels;
    cv::split(taMatResized, bgraChannels);
    cv::Mat foregroundBGR;
    std::vector<cv::Mat> bgrChannels = { bgraChannels[0], bgraChannels[1], bgraChannels[2] };
    cv::merge(bgrChannels, foregroundBGR);

    // Combine CA and TA, make float
    cv::Mat fgFloat, bgFloat, dbg, dbg2;
    foregroundBGR.convertTo(fgFloat, CV_32FC3);
    cv::imwrite("output/fgBGR.png", foregroundBGR);
    caOutput.convertTo(bgFloat, CV_32FC3);

    // dbg
    dbg = (fgFloat.mul(alphaBlend3ch));
    dbg.convertTo(dbg,CV_8UC3);
    cv::imwrite("output/fgFloatBlended.png", dbg);
    dbg2 = (bgFloat.mul(cv::Scalar::all(1.0) - alphaBlend3ch));
    dbg2.convertTo(dbg2, CV_8UC3);
    cv::imwrite("output/bgFloatBlended.png", dbg2);

    cv::Mat result = fgFloat.mul(alphaBlend3ch) + bgFloat.mul(cv::Scalar::all(1.0) - alphaBlend3ch);
    result.convertTo(result, CV_8UC3);

    return result;
}
