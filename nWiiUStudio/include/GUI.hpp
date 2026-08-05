#pragma once

#include "StudioState.hpp"

class GUI {
public:
    static void DrawStudio(StudioState& state);
    static bool WantsQuit();
    static void ApplySettings(StudioState& state);
    static void RebuildFontsIfNeeded(StudioState& state);
    static void SyncConfigEditor(StudioState& state);
};