#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <future>
#include <atomic>
#include <mutex>
#include <iostream>
#include <sstream>
#include <fstream>
#include <memory>

#include "loader/loader.h"
#include "analyzer/analyzer.h"
#include "recompiler/recompiler.h"
#include "recompiler/symbols.h"
#include <toml++/toml.hpp>

enum class OverrideStatus {
    Default, Stub, Skip, ForceRecompile
};

enum class ThemeMode {
    Dark, Light, Nintendo, Custom
};

struct AppSettings {
    ThemeMode theme = ThemeMode::Nintendo;
    float fontSize = 15.0f;
    float uiScale = 1.0f;
    std::string selectedFont = "Font_1.ttf";
    int windowWidth = 1280;
    int windowHeight = 720;
    bool maximized = true;

    float customBgBase[4] = {0.08f, 0.08f, 0.08f, 1.00f};
    float customAccent[4] = {0.00f, 0.48f, 0.80f, 1.00f};

    bool showTooltips = true;
    bool isFirstLaunch = true;
};

class StreamRedirector : public std::stringbuf {
public:
    StreamRedirector(std::vector<std::string>& target, std::mutex& mtx, std::atomic<size_t>& ver)
        : logs(target), mutex(mtx), version(ver) {}
    int sync() override {
        std::lock_guard<std::mutex> lock(mutex);
        std::string line = this->str();
        if (!line.empty() && line.back() == '\n') line.pop_back();
        if (!line.empty()) {
            logs.push_back(line);
            version.fetch_add(1);
        }
        this->str("");
        return 0;
    }
private:
    std::vector<std::string>& logs;
    std::mutex& mutex;
    std::atomic<size_t>& version;
};

struct UIState {
    std::string unpackedGamePath;
    std::string outputPath = "output";
    std::string customOutputPath;
    std::string configTomlContent;

    std::vector<uint8_t> rawExecutableData;
    
    std::map<uint32_t, OverrideStatus> funcOverrides;
    
    std::unique_ptr<// nwii::loader::Executable> executable;
    std::unique_ptr<// nwii::analyzer::Analyzer> analyzer;
    bool isAnalysisComplete = false;
};

class StudioState {
public:
    UIState data;
    AppSettings settings;

    uint32_t selectedFuncAddress = 0;
    
    std::atomic<bool> isBusy = false;
    std::vector<std::string> logs;
    std::mutex stateMutex;
    std::streambuf* oldCout = nullptr;
    std::streambuf* oldCerr = nullptr;
    std::unique_ptr<StreamRedirector> redirector;
    std::future<void> workerThread;
    std::vector<std::string> availableFonts;

    std::atomic<bool> pendingFontRebuild{false};
    std::string configTomlPath;
    std::atomic<size_t> logVersion{0};
    
    // nwii::recomp::SymbolTable symbolTable;
    std::string symbolsPath;

    void SetStatus(const std::string& msg) {
        std::lock_guard<std::mutex> lock(statusMutex_);
        statusMessage_ = msg;
    }
    std::string GetStatus() const {
        std::lock_guard<std::mutex> lock(statusMutex_);
        return statusMessage_;
    }

    StudioState() {
        redirector = std::make_unique<StreamRedirector>(logs, stateMutex, logVersion);
        oldCout = std::cout.rdbuf(redirector.get());
        oldCerr = std::cerr.rdbuf(redirector.get());

        try {
            if (!std::filesystem::exists("output")) {
                std::filesystem::create_directory("output");
            }
            data.outputPath = std::filesystem::absolute("output").string();
        } catch(...) {}

        LoadSettings();
        ScanFonts();
        LoadConfigToml();
    }

    ~StudioState() {
        std::cout.rdbuf(oldCout);
        std::cerr.rdbuf(oldCerr);
        SaveSettings();
    }

    void Log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(stateMutex);
        logs.push_back("[Studio] " + msg);
        logVersion.fetch_add(1);
    }

    void LogRaw(const std::string& msg) {
        std::lock_guard<std::mutex> lock(stateMutex);
        logs.push_back(msg);
        logVersion.fetch_add(1);
    }

    void ScanFonts() {
        availableFonts.clear();
        try {
            std::filesystem::path fontDir = "external/Font";
            if (std::filesystem::exists(fontDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(fontDir)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        if (ext == ".ttf" || ext == ".TTF") {
                            availableFonts.push_back(entry.path().filename().string());
                        }
                    }
                }
            }
        } catch(...) {}

        if (availableFonts.empty()) {
            availableFonts.push_back("Default");
        }
    }

    void LoadSettings() {
        try {
            if (std::filesystem::exists("studio_settings.toml")) {
                auto tbl = toml::parse_file("studio_settings.toml");
                settings.theme = static_cast<ThemeMode>(tbl["theme"].value_or(static_cast<int>(ThemeMode::Nintendo)));
                settings.fontSize = tbl["font_size"].value_or(15.0f);
                settings.uiScale = tbl["ui_scale"].value_or(1.0f);
                settings.selectedFont = tbl["selected_font"].value_or("Font_1.ttf");
                settings.windowWidth = tbl["window_width"].value_or(1280);
                settings.windowHeight = tbl["window_height"].value_or(720);
                settings.maximized = tbl["maximized"].value_or(true);
                
                settings.isFirstLaunch = tbl["is_first_launch"].value_or(true);
                settings.showTooltips = tbl["show_tooltips"].value_or(settings.isFirstLaunch);

                if (settings.isFirstLaunch) {
                    settings.isFirstLaunch = false; 
                }
            } else {
                settings.isFirstLaunch = true;
                settings.showTooltips = true;
            }
        } catch(...) {
            settings.isFirstLaunch = true;
            settings.showTooltips = true;
        }
    }

    void SaveSettings() {
        try {
            toml::table tbl;
            tbl.insert("theme", static_cast<int>(settings.theme));
            tbl.insert("font_size", settings.fontSize);
            tbl.insert("ui_scale", settings.uiScale);
            tbl.insert("selected_font", settings.selectedFont);
            tbl.insert("window_width", settings.windowWidth);
            tbl.insert("window_height", settings.windowHeight);
            tbl.insert("maximized", settings.maximized);
            tbl.insert("is_first_launch", false); 
            tbl.insert("show_tooltips", settings.showTooltips);

            std::ofstream ofs("studio_settings.toml");
            if (ofs.good()) {
                ofs << tbl;
            }
        } catch(...) {}
    }

    void LoadUnpackedGame(const std::string& path) {
        data.unpackedGamePath = path;
        data.isAnalysisComplete = false;
        data.analyzer.reset();
        data.executable.reset();
        data.funcOverrides.clear();

        SetStatus("Loading Game...");
        Log("Attempting to load game from: " + path);

        try {
            auto exec = std::make_unique<// nwii::loader::Executable>();
            if (exec->load_unpacked_game(path)) {
                data.executable = std::move(exec);

                std::string dol_path = path + "/sys/main.dol";
                std::ifstream file(dol_path, std::ios::binary);
                if (!file.good()) {
                    dol_path = path + "/main.dol";
                    file.open(dol_path, std::ios::binary);
                }
                
                if (file.good()) {
                    data.rawExecutableData.assign((std::istreambuf_iterator<char>(file)), 
                                                  (std::istreambuf_iterator<char>()));
                }

                SetStatus("Loaded: " + dol_path);
                Log("Successfully loaded DOL. Entry point: " + std::to_string(data.executable->entry_point));
            } else {
                SetStatus("Failed to load");
                Log("Failed to load unpacked game (could not find main.dol)");
            }
        } catch(const std::exception& e) {
            SetStatus("Error");
            Log("Exception during load: " + std::string(e.what()));
        }
    }

    void SetOutputDir(const std::string& path) {
        if (path.empty()) return;
        try {
            data.customOutputPath = std::filesystem::absolute(path).string();
            if (!std::filesystem::exists(data.customOutputPath)) {
                std::filesystem::create_directories(data.customOutputPath);
            }
            Log("Custom output directory set to: " + data.customOutputPath);
        } catch (const std::exception& e) {
            Log(std::string("Error setting output dir: ") + e.what());
            data.customOutputPath = std::filesystem::absolute("output").string();
        }
    }

    void StartAnalysis() {
        if (isBusy || !data.executable) return;

        isBusy = true;
        SetStatus("Analyzing PowerPC Code...");

        workerThread = std::async(std::launch::async, [this]() {
            try {
                auto newAnalyzer = std::make_unique<// nwii::analyzer::Analyzer>(*data.executable);
                newAnalyzer->analyze();
                
                std::lock_guard<std::mutex> lock(stateMutex);
                data.analyzer = std::move(newAnalyzer);
                data.isAnalysisComplete = true;
                SetStatus("Analysis Complete");
                Log("Discovered " + std::to_string(data.analyzer->get_functions().size()) + " functions.");
                logVersion.fetch_add(1);
            } catch (const std::exception& e) {
                SetStatus("Error"); 
                Log(std::string("Analysis exception: ") + e.what());
            }
            isBusy = false;
        });
    }

    void StartRecompilation() {
        if (isBusy || !data.analyzer || !data.isAnalysisComplete) return;

        isBusy = true;
        SetStatus("Generating C++ Code...");

        workerThread = std::async(std::launch::async, [this]() {
            try {
                // nwii::recomp::RecompilerConfig config;
                try {
                    auto tbl = toml::parse(data.configTomlContent);
                    config.project_name = tbl["project_name"].value_or("RecompiledGame");
                    config.input_game_dir = tbl["input_game_dir"].value_or(data.unpackedGamePath);
                    config.output_dir = tbl["output_dir"].value_or(GetEffectiveOutputPath());
                    config.runtime_source_dir = tbl["runtime_source_dir"].value_or("../nWiiRuntime");
                    config.symbols_csv = tbl["symbols_csv"].value_or(symbolsPath);
                    config.split_output = tbl["split_output"].value_or(false);
                    config.instructions_per_file = tbl["instructions_per_file"].value_or(20000);
                } catch(...) {}

                // nwii::recomp::Recompiler recompiler(*data.analyzer, &symbolTable, config);
                std::string out_dir = config.output_dir;
                if (recompiler.generate_cmake_project(data.executable->entry_point)) {
                    SetStatus("C++ Generation Complete");
                    Log("Successfully generated standalone CMake project at: " + out_dir);
                } else {
                    SetStatus("Error generating C++");
                    Log("Failed to generate CMake project at " + out_dir);
                }
            } catch (const std::exception& e) {
                SetStatus("Error"); 
                Log(std::string("Recompilation exception: ") + e.what());
            }
            isBusy = false;
        });
    }

    void LoadGhidraCSV(const std::string& path) {
        if (symbolTable.load_csv(path)) {
            symbolsPath = path;
            Log("Successfully loaded symbols from: " + path);
        } else {
            Log("Failed to load symbols from: " + path);
        }
    }

    void LoadConfigToml() {
        configTomlPath = "recomp_config.toml";
        try {
            if (std::filesystem::exists(configTomlPath)) {
                std::ifstream ifs(configTomlPath);
                if (ifs.good()) {
                    std::stringstream buffer;
                    buffer << ifs.rdbuf();
                    data.configTomlContent = buffer.str();

                    auto tbl = toml::parse(data.configTomlContent);
                    data.unpackedGamePath = tbl["input_game_dir"].value_or("");
                    data.customOutputPath = tbl["output_dir"].value_or("");
                    symbolsPath = tbl["symbols_csv"].value_or("");
                    return;
                }
            }
        } catch(...) {}
        CreateDefaultConfig();
    }

    void CreateDefaultConfig() {
        data.configTomlContent = "project_name = \"RecompiledGame\"\n"
                                 "input_game_dir = \"\"\n"
                                 "output_dir = \"export\"\n"
                                 "runtime_source_dir = \"../nWiiRuntime\"\n"
                                 "symbols_csv = \"\"\n\n"
                                 "split_output = true\n"
                                 "instructions_per_file = 20000\n";
    }

    void SaveConfigTOML() {
        SaveConfigTomlFromEditor(data.configTomlContent);
    }

    void SaveConfigTomlFromEditor(const std::string& newContent) {
        data.configTomlContent = newContent;
        configTomlPath = "recomp_config.toml";
        try {
            std::ofstream ofs(configTomlPath);
            if (ofs.good()) {
                ofs << newContent;
                Log("Saved config to " + configTomlPath);
            }
        } catch (const std::exception& e) {
            Log(std::string("Error saving config: ") + e.what());
        }
    }

    std::string GetEffectiveOutputPath() const {
        if (!data.customOutputPath.empty()) return data.customOutputPath;
        return data.outputPath;
    }

private:
    mutable std::mutex statusMutex_;
    std::string statusMessage_ = "Ready";
};