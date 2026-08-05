#include "raylib.h"
#include "rlImGui.h"
#include "imgui.h"
#include "GUI.hpp"
#include "StudioState.hpp"
#include <iostream>

int main(int argc, char** argv) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    InitWindow(1280, 720, "nWiiStudio - PowerPC Analyzer");
    SetTargetFPS(60);

    rlImGuiSetup(true);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    StudioState state;

    if (argc > 1) {
        state.LoadUnpackedGame(argv[1]);
        state.StartAnalysis();
    }

    while (!WindowShouldClose() && !GUI::WantsQuit()) {
        if (state.pendingFontRebuild.load()) {
            state.pendingFontRebuild = false;
        }

        BeginDrawing();
        ClearBackground(GetColor(0x1a1a1aff));

        rlImGuiBegin();

        GUI::DrawStudio(state);

        rlImGuiEnd();
        EndDrawing();
    }

    state.SaveSettings();

    if (state.workerThread.valid()) {
        state.workerThread.wait();
    }

    rlImGuiShutdown();
    CloseWindow();

    return 0;
}
