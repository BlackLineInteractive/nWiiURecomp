#include "ui/StyleManager.hpp"
#include <filesystem>
#include <iostream>
#include <algorithm>

namespace StyleManager {

void SetupFonts(AppSettings& settings) {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    float safeFontSize = std::clamp(settings.fontSize, 10.0f, 48.0f);
    settings.fontSize = safeFontSize;

    io.Fonts->Clear();

    std::string fontPath = "external/Font/" + settings.selectedFont;

    ImFont* loadedFont = nullptr;

    if (settings.selectedFont != "Default" && std::filesystem::exists(fontPath)) {
        ImFontConfig config;
        config.OversampleH = 3; 
        config.OversampleV = 2; 
        config.PixelSnapH = true;
        loadedFont = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), safeFontSize, &config);
        if (!loadedFont) {
            
            config.SizePixels = safeFontSize;
            loadedFont = io.Fonts->AddFontDefault(&config);
        }
    } else {
        ImFontConfig config;
        config.OversampleH = 3;
        config.OversampleV = 2;
        config.PixelSnapH = true;
        config.SizePixels = safeFontSize;
        loadedFont = io.Fonts->AddFontDefault(&config);
    }

}

void ApplyDarkTheme() {
    if (ImGui::GetCurrentContext() == nullptr) return;

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 bgBase       = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    const ImVec4 bgPanel      = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
    const ImVec4 bgInput      = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
    const ImVec4 accentMain   = ImVec4(0.00f, 0.48f, 0.80f, 1.00f);
    const ImVec4 accentHover  = ImVec4(0.10f, 0.55f, 0.90f, 1.00f);
    const ImVec4 accentActive = ImVec4(0.00f, 0.40f, 0.70f, 1.00f);
    const ImVec4 textBright   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    const ImVec4 textDim      = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);

    colors[ImGuiCol_Text]                   = textBright;
    colors[ImGuiCol_TextDisabled]           = textDim;
    colors[ImGuiCol_WindowBg]               = bgBase;
    colors[ImGuiCol_ChildBg]                = bgPanel;
    colors[ImGuiCol_PopupBg]                = ImVec4(0.11f, 0.11f, 0.11f, 0.98f);
    colors[ImGuiCol_Border]                 = ImVec4(0.30f, 0.30f, 0.30f, 0.60f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = bgBase;
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    colors[ImGuiCol_CheckMark]              = accentMain;
    colors[ImGuiCol_SliderGrab]             = accentMain;
    colors[ImGuiCol_SliderGrabActive]       = accentActive;
    colors[ImGuiCol_Button]                 = ImVec4(0.24f, 0.24f, 0.25f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = accentMain;
    colors[ImGuiCol_ButtonActive]           = accentActive;
    colors[ImGuiCol_Header]                 = ImVec4(0.24f, 0.24f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.28f, 0.28f, 0.29f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.20f, 0.20f, 0.21f, 1.00f);
    colors[ImGuiCol_FrameBg]                = bgInput;
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.22f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.24f, 0.24f, 0.25f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TabHovered]             = bgPanel;
    colors[ImGuiCol_TabActive]              = bgPanel;
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = bgPanel;
    colors[ImGuiCol_DockingPreview]         = accentMain;
    colors[ImGuiCol_DockingEmptyBg]         = bgBase;
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.00f, 0.48f, 0.80f, 0.40f);
    colors[ImGuiCol_NavHighlight]           = accentMain;
}

void ApplyLightTheme() {
    if (ImGui::GetCurrentContext() == nullptr) return;

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 bgBase       = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    const ImVec4 bgPanel      = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    const ImVec4 bgInput      = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    const ImVec4 accentMain   = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    const ImVec4 accentHover  = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    const ImVec4 accentActive = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
    const ImVec4 textBright   = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    const ImVec4 textDim      = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

    colors[ImGuiCol_Text]                   = textBright;
    colors[ImGuiCol_TextDisabled]           = textDim;
    colors[ImGuiCol_WindowBg]               = bgBase;
    colors[ImGuiCol_ChildBg]                = bgPanel;
    colors[ImGuiCol_PopupBg]                = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);
    colors[ImGuiCol_Border]                 = ImVec4(0.70f, 0.70f, 0.70f, 0.60f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = bgBase;
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.69f, 0.69f, 0.69f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.59f, 0.59f, 0.59f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.49f, 0.49f, 0.49f, 1.00f);
    colors[ImGuiCol_CheckMark]              = accentMain;
    colors[ImGuiCol_SliderGrab]             = accentMain;
    colors[ImGuiCol_SliderGrabActive]       = accentActive;
    colors[ImGuiCol_Button]                 = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_FrameBg]                = bgInput;
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.76f, 0.80f, 0.84f, 0.93f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.60f, 0.73f, 0.88f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.92f, 0.93f, 0.94f, 0.98f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.74f, 0.82f, 0.91f, 1.00f);
    colors[ImGuiCol_DockingPreview]         = accentMain;
    colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_NavHighlight]           = accentMain;
}

void ApplyNintendoTheme() {
    if (ImGui::GetCurrentContext() == nullptr) return;

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    
    const ImVec4 bgBase       = ImVec4(0.06f, 0.05f, 0.09f, 1.00f);
    const ImVec4 bgPanel      = ImVec4(0.10f, 0.09f, 0.14f, 1.00f);
    const ImVec4 bgInput      = ImVec4(0.14f, 0.13f, 0.19f, 1.00f);

    const ImVec4 accentMain   = ImVec4(0.38f, 0.15f, 0.62f, 1.00f);
    const ImVec4 accentHover  = ImVec4(0.48f, 0.25f, 0.72f, 1.00f);
    const ImVec4 accentActive = ImVec4(0.28f, 0.05f, 0.52f, 1.00f);

    const ImVec4 accentSec    = ImVec4(1.00f, 0.65f, 0.00f, 1.00f);

    const ImVec4 textBright   = ImVec4(0.98f, 0.98f, 0.99f, 1.00f);
    const ImVec4 textDim      = ImVec4(0.65f, 0.64f, 0.68f, 1.00f);

    colors[ImGuiCol_Text]                   = textBright;
    colors[ImGuiCol_TextDisabled]           = textDim;
    colors[ImGuiCol_WindowBg]               = bgBase;
    colors[ImGuiCol_ChildBg]                = bgPanel;
    colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.07f, 0.11f, 0.98f);

    colors[ImGuiCol_Border]                 = ImVec4(0.38f, 0.15f, 0.62f, 0.35f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);
    
    colors[ImGuiCol_TitleBg]                = ImVec4(0.05f, 0.04f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.09f, 0.08f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.09f, 0.08f, 0.13f, 1.00f);
    
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.04f, 0.03f, 0.06f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.24f, 0.22f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = accentHover;
    colors[ImGuiCol_ScrollbarGrabActive]    = accentActive;
    
    colors[ImGuiCol_CheckMark]              = accentSec;
    colors[ImGuiCol_SliderGrab]             = accentSec;
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.90f, 0.55f, 0.00f, 1.00f);
    
    colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.18f, 0.25f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = accentMain;
    colors[ImGuiCol_ButtonActive]           = accentActive;
    
    colors[ImGuiCol_Header]                 = ImVec4(0.38f, 0.15f, 0.62f, 0.40f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.38f, 0.15f, 0.62f, 0.80f);
    colors[ImGuiCol_HeaderActive]           = accentMain;
    
    colors[ImGuiCol_FrameBg]                = bgInput;
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.24f, 0.22f, 0.30f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.28f, 0.25f, 0.34f, 1.00f);
    
    colors[ImGuiCol_Tab]                    = ImVec4(0.12f, 0.11f, 0.16f, 1.00f);
    colors[ImGuiCol_TabHovered]             = accentHover;
    colors[ImGuiCol_TabActive]              = accentMain;
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.09f, 0.08f, 0.13f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.22f, 0.10f, 0.40f, 1.00f);
    
    colors[ImGuiCol_DockingPreview]         = accentMain;
    colors[ImGuiCol_DockingEmptyBg]         = bgBase;
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.38f, 0.15f, 0.62f, 0.45f);
    colors[ImGuiCol_NavHighlight]           = accentHover;
}

void ApplyCustomTheme(AppSettings& settings) {
    if (ImGui::GetCurrentContext() == nullptr) return;

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    ImVec4 bgBase(settings.customBgBase[0], settings.customBgBase[1],
                  settings.customBgBase[2], settings.customBgBase[3]);
    ImVec4 accent(settings.customAccent[0], settings.customAccent[1],
                  settings.customAccent[2], settings.customAccent[3]);

    colors[ImGuiCol_WindowBg] = bgBase;
    colors[ImGuiCol_Button] = accent;
    colors[ImGuiCol_ButtonHovered] = ImVec4(accent.x + 0.1f, accent.y + 0.1f, accent.z + 0.1f, accent.w);
    colors[ImGuiCol_ButtonActive] = ImVec4(accent.x - 0.1f, accent.y - 0.1f, accent.z - 0.1f, accent.w);
}

void ApplyTheme(ThemeMode mode, AppSettings& settings) {
    if (ImGui::GetCurrentContext() == nullptr) return;

    ImGuiStyle& style = ImGui::GetStyle();

    float safeScale = std::clamp(settings.uiScale, 0.5f, 3.0f);

    style.WindowPadding     = ImVec2(8 * safeScale, 8 * safeScale);
    style.FramePadding      = ImVec2(5 * safeScale, 4 * safeScale);
    style.CellPadding       = ImVec2(4 * safeScale, 4 * safeScale);
    style.ItemSpacing       = ImVec2(8 * safeScale, 6 * safeScale);
    style.ItemInnerSpacing  = ImVec2(6 * safeScale, 6 * safeScale);
    style.IndentSpacing     = 20 * safeScale;
    style.ScrollbarSize     = 12 * safeScale;
    style.GrabMinSize       = 10 * safeScale;
    style.WindowBorderSize  = 1 * safeScale;
    style.ChildBorderSize   = 1 * safeScale;
    style.PopupBorderSize   = 1 * safeScale;
    style.FrameBorderSize   = 0;
    style.TabBorderSize     = 0;
    
    style.WindowRounding    = 12.0f * safeScale;
    style.ChildRounding     = 8.0f * safeScale;
    style.FrameRounding     = 6.0f * safeScale;
    style.PopupRounding     = 10.0f * safeScale;
    style.ScrollbarRounding = 12.0f * safeScale;
    style.GrabRounding      = 6.0f * safeScale;
    style.TabRounding       = 8.0f * safeScale;

    switch(mode) {
        case ThemeMode::Dark:
            ApplyDarkTheme();
            break;
        case ThemeMode::Light:
            ApplyLightTheme();
            break;
        case ThemeMode::Nintendo:
            ApplyNintendoTheme();
            break;
        case ThemeMode::Custom:
            ApplyDarkTheme();
            ApplyCustomTheme(settings);
            break;
    }
}

} 
