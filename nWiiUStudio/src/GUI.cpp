#include "GUI.hpp"
#include "ui/StyleManager.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_memory_editor.h"
#include "TextEditor.h"
#include "ImGuiFileDialog.h"
// #include "recompiler/recompiler.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cstdlib>

static void ShowTooltip(const char* text, StudioState& state) {
    if (state.settings.showTooltips && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text);
    }
}

static MemoryEditor mem_edit;
static TextEditor code_editor;
static TextEditor config_editor;
static TextEditor ghidra_editor;
static TextEditor log_editor;  
static TextEditor runtime_log_editor;
static std::string last_runtime_log;
static bool editors_initialized = false;
static bool show_settings_window = false;
static bool config_editor_needs_sync = false;
static size_t last_log_version = 0;
static bool s_wantsQuit = false;

struct HexHighlightRange {
    size_t start;
    size_t end;
    ImU32 color;
    std::string label;
};
static std::vector<HexHighlightRange> hex_highlight_ranges;

static std::vector<HexHighlightRange>* g_hex_highlights = &hex_highlight_ranges;

std::string FormatHex(uint32_t val) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << val;
    return ss.str();
}

static ImU32 HexBgColorCallback(const ImU8* , size_t off, void* ) {
    if (g_hex_highlights) {
        for (const auto& hl : *g_hex_highlights) {
            if (off >= hl.start && off < hl.end) {
                return hl.color;
            }
        }
    }
    return 0; 
}

void GUI::ApplySettings(StudioState& state) {
    StyleManager::SetupFonts(state.settings);
    StyleManager::ApplyTheme(state.settings.theme, state.settings);
}

void GUI::RebuildFontsIfNeeded(StudioState& state) {
    if (state.pendingFontRebuild.exchange(false)) {
        StyleManager::SetupFonts(state.settings);
    }
}

void GUI::SyncConfigEditor(StudioState& state) {
    config_editor_needs_sync = true;
}

bool GUI::WantsQuit() {
    return s_wantsQuit;
}

static void DrawSettingsWindow(StudioState& state) {
    if (!show_settings_window) return;

    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", &show_settings_window)) {

        if (ImGui::CollapsingHeader("Theme", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* themes[] = { "Dark", "Light", "Nintendo", "Custom" };
            int current = static_cast<int>(state.settings.theme);
            if (ImGui::Combo("Theme Mode", &current, themes, 4)) {
                state.settings.theme = static_cast<ThemeMode>(current);
                StyleManager::ApplyTheme(state.settings.theme, state.settings);
            }

            if (state.settings.theme == ThemeMode::Custom) {
                ImGui::Spacing();
                ImGui::ColorEdit4("Background", state.settings.customBgBase);
                ImGui::ColorEdit4("Accent", state.settings.customAccent);
                if (ImGui::Button("Apply Custom Colors")) {
                    StyleManager::ApplyTheme(state.settings.theme, state.settings);
                }
            }
        }

        if (ImGui::CollapsingHeader("Font & Size", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginCombo("Font", state.settings.selectedFont.c_str())) {
                for (const auto& font : state.availableFonts) {
                    bool selected = (state.settings.selectedFont == font);
                    if (ImGui::Selectable(font.c_str(), selected)) {
                        state.settings.selectedFont = font;
                        state.pendingFontRebuild = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::SliderFloat("Font Size", &state.settings.fontSize, 10.0f, 24.0f, "%.1f")) {
                state.settings.fontSize = std::clamp(state.settings.fontSize, 10.0f, 48.0f);
                state.pendingFontRebuild = true;
            }
        }

        if (ImGui::CollapsingHeader("UI Scale", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::SliderFloat("Scale", &state.settings.uiScale, 0.5f, 2.0f, "%.2f")) {
                state.settings.uiScale = std::clamp(state.settings.uiScale, 0.5f, 3.0f);
                StyleManager::ApplyTheme(state.settings.theme, state.settings);
            }
        }

        if (ImGui::CollapsingHeader("Window", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Start Maximized", &state.settings.maximized);
            ImGui::SliderInt("Default Width", &state.settings.windowWidth, 800, 3840);
            ImGui::SliderInt("Default Height", &state.settings.windowHeight, 600, 2160);
        }

        if (ImGui::CollapsingHeader("Features", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show Tooltips (First Launch)", &state.settings.showTooltips);
        }

        if (ImGui::CollapsingHeader("Manual (Documentation)", ImGuiTreeNodeFlags_None)) {
            ImGui::TextColored(ImVec4(0.0f, 0.6f, 1.0f, 1.0f), "NWiiRecomp Studio - User Manual");
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::TreeNode("1. Project Setup & Analysis")) {
                ImGui::BulletText("Load Executable: Use File -> Open Unpacked Game to select a directory containing main.dol.");
                ImGui::BulletText("Symbol Mapping: Supply a Ghidra-exported CSV to map memory addresses to function names.");
                ImGui::BulletText("Static Analysis: Press F5 or 'Run Analysis' to disassemble PowerPC instructions and generate the Control Flow Graph (CFG).");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("2. High Level Emulation (HLE)")) {
                ImGui::BulletText("OS Subsystem: Automatically intercepts OSInit, OSAlloc, and thread management to prevent infinite lock loops.");
                ImGui::BulletText("Memory Arena: The internal memory manager handles standard MEM1/MEM2 allocations automatically.");
                ImGui::BulletText("Graphics (GX): Display lists sent to the FIFO at 0xCC008000 are captured for backend rendering.");
                ImGui::BulletText("Audio (AX): DSP interrupts are stubbed to prevent audio thread hangs.");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("3. Recompilation & Execution")) {
                ImGui::BulletText("Generate C++: Press F6 to emit native C++ code for all identified PPC functions.");
                ImGui::BulletText("Runtime: Navigate to the 'Runtime' workspace tab to compile the CMake project and launch the standalone engine.");
                ImGui::TreePop();
            }
            ImGui::Spacing();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Rescan Fonts", ImVec2(150, 0))) {
            state.ScanFonts();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset to Defaults", ImVec2(150, 0))) {
            state.settings = AppSettings();
            state.pendingFontRebuild = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(150, 0))) {
            show_settings_window = false;
        }
    }
    ImGui::End();
}

void GUI::DrawStudio(StudioState& state) {
    if (!editors_initialized) {
        code_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
        code_editor.SetPalette(TextEditor::GetDarkPalette());
        code_editor.SetReadOnly(true);

        config_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
        config_editor.SetPalette(TextEditor::GetDarkPalette());
        config_editor.SetReadOnly(false);
        config_editor.SetText(state.data.configTomlContent);

        ghidra_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
        ghidra_editor.SetPalette(TextEditor::GetDarkPalette());
        ghidra_editor.SetReadOnly(true);

        log_editor.SetPalette(TextEditor::GetDarkPalette());
        log_editor.SetReadOnly(true);
        log_editor.SetShowWhitespaces(false);

        runtime_log_editor.SetPalette(TextEditor::GetDarkPalette());
        runtime_log_editor.SetReadOnly(true);
        runtime_log_editor.SetShowWhitespaces(false);

        mem_edit.ReadOnly = true;
        mem_edit.OptShowAscii = true;
        mem_edit.OptGreyOutZeroes = true;
        mem_edit.OptUpperCaseHex = false;
        mem_edit.BgColorFn = HexBgColorCallback;
        mem_edit.UserData = nullptr;

        editors_initialized = true;
    }

    if (config_editor_needs_sync) {
        config_editor.SetText(state.data.configTomlContent);
        config_editor_needs_sync = false;
    }

    {
        size_t currentVersion = state.logVersion.load();
        if (currentVersion != last_log_version) {
            std::lock_guard<std::mutex> lock(state.stateMutex);
            std::stringstream ss;
            for (const auto& logLine : state.logs) {
                ss << logLine << "\n";
            }
            log_editor.SetText(ss.str());

            TextEditor::ErrorMarkers markers;
            for (size_t i = 0; i < state.logs.size(); i++) {
                const auto& line = state.logs[i];
                if (line.find("Error") != std::string::npos ||
                    line.find("Failed") != std::string::npos ||
                    line.find("error") != std::string::npos) {
                    markers.insert(std::make_pair(static_cast<int>(i + 1), line));
                }
            }
            log_editor.SetErrorMarkers(markers);

            last_log_version = currentVersion;
        }
    }

    ImGuiID dockspace_id = ImGui::GetID("StudioDock");
    ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport());

    static bool first_run = true;
    if (first_run) {
        first_run = false;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

        ImGuiID dock_left = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.22f, nullptr, &dockspace_id);
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.28f, nullptr, &dockspace_id);
        ImGuiID dock_down = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.25f, nullptr, &dockspace_id);

        ImGui::DockBuilderDockWindow("Explorer", dock_left);
        ImGui::DockBuilderDockWindow("Inspector", dock_right);
        ImGui::DockBuilderDockWindow("Logs", dock_down);
        ImGui::DockBuilderDockWindow("Workspace", dockspace_id);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);

    if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            state.LoadUnpackedGame(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("ChooseDirDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            state.SetOutputDir(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("ChooseGhidraCSV")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
            state.LoadGhidraCSV(path);
            
            std::ifstream file(path);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                ghidra_editor.SetText(buffer.str());
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Unpacked Game...", "Ctrl+O")) {
                IGFD::FileDialogConfig config;
                config.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose Game Executable (main.dol)", ".dol", config);
            }
            if (ImGui::MenuItem("Set Output Directory...")) {
                IGFD::FileDialogConfig config;
                config.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("ChooseDirDlgKey", "Choose Output Dir", nullptr, config);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Config", "Ctrl+S", false, state.data.isAnalysisComplete)) {
                state.SaveConfigTOML();
                config_editor_needs_sync = true;
            }
            if (ImGui::MenuItem("Reload Config from Disk")) {
                state.LoadConfigToml();
                config_editor_needs_sync = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) s_wantsQuit = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools")) {
            bool busy = state.isBusy.load();
            if (ImGui::MenuItem("Analyze", "F5", false, !busy && state.data.executable != nullptr)) {
                state.StartAnalysis();
            }
            if (ImGui::MenuItem("Generate C++", "F6", false, !busy && state.data.isAnalysisComplete)) {
                state.StartRecompilation();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Settings")) {
            if (ImGui::MenuItem("Open Settings Window")) {
                show_settings_window = true;
            }
            ImGui::EndMenu();
        }

        std::string pathDisplay = state.GetEffectiveOutputPath();
        float pathWidth = ImGui::CalcTextSize(pathDisplay.c_str()).x;
        if (ImGui::GetWindowWidth() > pathWidth + 300) {
            ImGui::SameLine(ImGui::GetWindowWidth() - pathWidth - 20);
            ImGui::TextDisabled("Output: %s", pathDisplay.c_str());
        }

        ImGui::EndMainMenuBar();
    }

    {
        ImGuiIO& io = ImGui::GetIO();
        bool noPopup = !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        if (noPopup) {
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
                IGFD::FileDialogConfig config;
                config.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose Game Executable (main.dol)", ".dol", config);
            }
            bool busy = state.isBusy.load();
            if (ImGui::IsKeyPressed(ImGuiKey_F5) && !busy && state.data.executable != nullptr) {
                state.StartAnalysis();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F6) && !busy && state.data.isAnalysisComplete) {
                state.StartRecompilation();
            }
        }
    }

    DrawSettingsWindow(state);

    ImGui::Begin("Explorer");
    ImGui::SetNextItemWidth(-1);
    static char filterBuf[128] = "";
    ImGui::InputTextWithHint("##Search", "Search functions...", filterBuf, 128);
    ImGui::Separator();

    if (state.data.isAnalysisComplete && state.data.analyzer) {
        ImGui::BeginChild("FuncList", ImVec2(0, 0), false);
        const auto& funcsMap = state.data.analyzer->get_functions();
        std::vector<nwii::analyzer::Function> funcs;
        for(const auto& pair : funcsMap) funcs.push_back(pair.second);

        std::string filterStr(filterBuf);
        static std::vector<int> filteredIndices;
        filteredIndices.clear();
        for (int idx = 0; idx < static_cast<int>(funcs.size()); idx++) {
            std::string addrStr = FormatHex(funcs[idx].start_address);
            if (filterStr.empty() || addrStr.find(filterStr) != std::string::npos) {
                filteredIndices.push_back(idx);
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filteredIndices.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                int i = filteredIndices[row];
                const auto& func = funcs[i];
                std::string funcName = "func_" + FormatHex(func.start_address).substr(2);

                OverrideStatus status = OverrideStatus::Default;
                auto it = state.data.funcOverrides.find(func.start_address);
                if (it != state.data.funcOverrides.end()) {
                    status = it->second;
                }

                ImVec4 color = ImGui::GetStyle().Colors[ImGuiCol_Text];
                if (status == OverrideStatus::Stub) {
                    color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                } else if (status == OverrideStatus::Skip) {
                    color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                } else if (status == OverrideStatus::ForceRecompile) {
                    color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
                }

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                bool isSelected = state.selectedFuncAddress == func.start_address;
                if (ImGui::Selectable(funcName.c_str(), isSelected)) {
                    state.selectedFuncAddress = func.start_address;
                    
                    std::stringstream codeSS;
                    codeSS << "// Function: " << funcName << "\n";
                    codeSS << "// Address:  " << FormatHex(func.start_address) << "\n";
                    codeSS << "// Size:     " << (func.end_address - func.start_address) << " bytes\n\n";
                    
                    // nwii::recomp::Recompiler live_recompiler(*state.data.analyzer, &state.symbolTable);
                    codeSS << "// Recompilation preview disabled in nWiiURecomp port\n";
                    
                    code_editor.SetText(codeSS.str());

                    if (!state.data.rawExecutableData.empty() && func.start_address < state.data.rawExecutableData.size()) {
                        mem_edit.GotoAddrAndHighlight(func.start_address,
                            std::min(static_cast<size_t>(func.end_address), state.data.rawExecutableData.size()));
                    }
                }
                ImGui::PopStyleColor();

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Start: %s", FormatHex(func.start_address).c_str());
                    ImGui::Text("Size:  %d bytes", (func.end_address - func.start_address));
                    ImGui::EndTooltip();
                }
            }
        }
        ImGui::EndChild();
    } else {
        float winW = ImGui::GetWindowSize().x;
        float winH = ImGui::GetWindowSize().y;
        if (!state.data.executable) {
            const char* txt = "No game loaded";
            ImGui::SetCursorPos(ImVec2((winW - ImGui::CalcTextSize(txt).x) * 0.5f, winH * 0.4f));
            ImGui::TextDisabled("%s", txt);
        } else {
            const char* txt = "Ready to Analyze";
            ImGui::SetCursorPos(ImVec2((winW - ImGui::CalcTextSize(txt).x) * 0.5f, winH * 0.4f));
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%s", txt);
            ImGui::SetCursorPosX((winW - 120) * 0.5f);
            if (ImGui::Button("Run Analysis", ImVec2(120, 30))) {
                state.StartAnalysis();
            }
            ShowTooltip("Start scanning the DOL file for PowerPC functions and references.", state);
        }
    }
    ImGui::End();

    ImGui::Begin("Inspector");
    if (state.data.analyzer && state.selectedFuncAddress != 0) {
        auto& funcsMap = state.data.analyzer->get_functions();
        if (funcsMap.find(state.selectedFuncAddress) != funcsMap.end()) {
            auto& func = funcsMap.at(state.selectedFuncAddress);
            std::string funcName = "func_" + FormatHex(func.start_address).substr(2);

            if (ImGui::GetIO().Fonts->Fonts.Size > 0) {
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
                ImGui::TextColored(ImVec4(0.0f, 0.6f, 1.0f, 1.0f), "%s", funcName.c_str());
                ImGui::PopFont();
            } else {
                ImGui::TextColored(ImVec4(0.0f, 0.6f, 1.0f, 1.0f), "%s", funcName.c_str());
            }
            ImGui::Separator();
            ImGui::Spacing();

            OverrideStatus current = OverrideStatus::Default;
            auto it = state.data.funcOverrides.find(func.start_address);
            if (it != state.data.funcOverrides.end()) {
                current = it->second;
            }

            const char* options[] = { "Auto (Default)", "Stub (Plug)", "Skip (Ignore)", "Force Recompile" };
            int idx = static_cast<int>(current);
            ImGui::TextDisabled("Strategy:");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##Action", &idx, options, 4)) {
                state.data.funcOverrides[func.start_address] = static_cast<OverrideStatus>(idx);
                state.Log("Strategy changed for " + funcName);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::BeginTable("MetaTable", 2)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Address:");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", FormatHex(func.start_address).c_str());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Size:");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d bytes", (func.end_address - func.start_address));

                ImGui::EndTable();
            }
        } else {
            state.selectedFuncAddress = 0;
            ImGui::TextDisabled("Select a function to inspect");
        }
    } else {
        ImGui::TextDisabled("Select a function to inspect");
    }
    ImGui::End();

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 1.00f));
    ImGui::Begin("Workspace", nullptr, ImGuiWindowFlags_NoTitleBar);

    if (ImGui::BeginTabBar("Tabs", ImGuiTabBarFlags_Reorderable)) {
        if (ImGui::BeginTabItem("  C++ Preview  ")) {
            if (ImGui::BeginPopupContextItem("CppContext")) {
                if (ImGui::MenuItem("Copy All")) {
                    ImGui::SetClipboardText(code_editor.GetText().c_str());
                    state.Log("C++ code copied to clipboard");
                }
                ImGui::EndPopup();
            }
            code_editor.Render("Editor");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("  Hex View  ")) {
            if (state.data.rawExecutableData.empty()) {
                ImGui::TextDisabled("No binary data available (File not loaded or read error)");
            } else {
                hex_highlight_ranges.clear();
                if (state.data.analyzer && state.selectedFuncAddress != 0) {
                    auto& funcsMap = state.data.analyzer->get_functions();
                    if (funcsMap.find(state.selectedFuncAddress) != funcsMap.end()) {
                        auto& func = funcsMap.at(state.selectedFuncAddress);
                        if (func.start_address < state.data.rawExecutableData.size()) {
                            HexHighlightRange hl;
                            hl.start = func.start_address;
                            hl.end = std::min(static_cast<size_t>(func.end_address), state.data.rawExecutableData.size());
                            hl.color = IM_COL32(0, 120, 215, 80);
                            hl.label = "Function: func_" + FormatHex(func.start_address).substr(2);
                            hex_highlight_ranges.push_back(hl);
                        }
                    }
                }

                mem_edit.DrawContents(state.data.rawExecutableData.data(), state.data.rawExecutableData.size());
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("  config.toml  ")) {
            if (ImGui::Button("Save")) {
                state.SaveConfigTomlFromEditor(config_editor.GetText());
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload from Disk")) {
                state.LoadConfigToml();
                config_editor.SetText(state.data.configTomlContent);
                state.Log("config.toml reloaded from disk");
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy All")) {
                ImGui::SetClipboardText(config_editor.GetText().c_str());
                state.Log("config.toml copied to clipboard");
            }
            ImGui::SameLine();
            if (!state.configTomlPath.empty()) {
                ImGui::TextDisabled("Path: %s", state.configTomlPath.c_str());
            }
            ImGui::Separator();

            config_editor.Render("ConfigEditor");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("  Ghidra CSV  ")) {
            if (ImGui::Button("Import CSV")) {
                IGFD::FileDialogConfig config;
                config.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("ChooseGhidraCSV", "Choose Ghidra CSV", ".csv", config);
            }
            ShowTooltip("Import Ghidra symbol table for function names.", state);
            ImGui::SameLine();
            if (!state.symbolsPath.empty()) {
                ImGui::TextDisabled("Loaded: %s", state.symbolsPath.c_str());
            } else {
                ImGui::TextDisabled("No CSV loaded.");
            }
            ImGui::Separator();
            ghidra_editor.Render("GhidraEditor");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("  Runtime  ")) {
            ImGui::Spacing();
            ImGui::TextWrapped("Manage and execute the Recompiled Game runtime.");
            ImGui::Spacing();
            if (ImGui::Button("Build & Launch Game", ImVec2(250, 40))) {
                state.Log("Starting Recompiled Game in background...");
                
                std::system("cd export/build && cmake .. && make -j8 && ./RecompiledGame \"../../NO_GitHub/Recomp_game(NO_PUBLIK)/SHSM-Extract\" > output.log 2>&1 &");
            }
            ShowTooltip("Compiles the generated C++ project and starts the game engine natively.", state);
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.4f, 1.0f), "Runtime Output Logs:");
            ImGui::Spacing();

            static float timer = 0.0f;
            timer += ImGui::GetIO().DeltaTime;
            if (timer > 1.0f) {
                timer = 0.0f;
                std::ifstream ifs("export/build/output.log");
                if (ifs.good()) {
                    std::stringstream buffer;
                    buffer << ifs.rdbuf();
                    std::string content = buffer.str();
                    if (content != last_runtime_log) {
                        last_runtime_log = content;
                        runtime_log_editor.SetText(content);
                        
                    }
                }
            }

            runtime_log_editor.Render("RuntimeLogEditor");

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::End();
    ImGui::PopStyleColor();

    ImGui::Begin("Logs");

    {
        std::string currentStatus = state.GetStatus();
        if (state.isBusy.load()) {
            float progress = std::fmod((float)ImGui::GetTime(), 1.0f);
            ImGui::ProgressBar(progress, ImVec2(-1, 6), "");
            ImGui::SameLine();
            ImGui::Text("%s", currentStatus.c_str());
        } else {
            ImGui::TextDisabled("Status: %s", currentStatus.c_str());
        }
    }

    {
        float btnWidth = ImGui::CalcTextSize("Copy All").x + ImGui::CalcTextSize("Clear").x
                       + ImGui::GetStyle().FramePadding.x * 4 + ImGui::GetStyle().ItemSpacing.x * 2 + 20;
        ImGui::SameLine(std::max(0.0f, ImGui::GetWindowWidth() - btnWidth));
    }
    if (ImGui::SmallButton("Copy All")) {
        std::lock_guard<std::mutex> lock(state.stateMutex);
        std::stringstream allLogs;
        for (const auto& log : state.logs) {
            allLogs << log << "\n";
        }
        ImGui::SetClipboardText(allLogs.str().c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        std::lock_guard<std::mutex> lock(state.stateMutex);
        state.logs.clear();
        state.logVersion.fetch_add(1);
    }

    ImGui::Separator();
    log_editor.Render("LogEditor");

    ImGui::End();

    ImVec2 w_pos = ImVec2(ImGui::GetIO().DisplaySize.x - 260, ImGui::GetIO().DisplaySize.y - 35);
    ImGui::GetForegroundDrawList()->AddText(ImVec2(w_pos.x+1, w_pos.y+1), IM_COL32(0, 0, 0, 200), "Made by Blackline Interactive");
    ImGui::GetForegroundDrawList()->AddText(w_pos, IM_COL32(200, 200, 200, 150), "Made by Blackline Interactive");
}
