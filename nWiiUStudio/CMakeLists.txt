cmake_minimum_required(VERSION 3.21)
project(nWiiUStudio)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# 1. Raylib
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# 2. ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# 3. rlImGui
FetchContent_Declare(
    rlImGui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(rlImGui)

# 4. TextEditor
FetchContent_Declare(
    imgui_colortextedit
    GIT_REPOSITORY https://github.com/BalazsJako/ImGuiColorTextEdit.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui_colortextedit)

# 5. MemoryEditor
FetchContent_Declare(
    imgui_club
    GIT_REPOSITORY https://github.com/ocornut/imgui_club.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui_club)

# 6. FileDialog
FetchContent_Declare(
    imgui_file_dialog
    GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(imgui_file_dialog)
if(NOT imgui_file_dialog_POPULATED)
    FetchContent_Populate(imgui_file_dialog)
endif()

# Build ImGui
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

# Build rlImGui
add_library(rlimgui STATIC
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(rlimgui PUBLIC ${rlimgui_SOURCE_DIR} ${imgui_SOURCE_DIR})
target_link_libraries(rlimgui PUBLIC raylib imgui)

# Build TextEditor
add_library(imgui_colortextedit_lib STATIC
    ${imgui_colortextedit_SOURCE_DIR}/TextEditor.cpp
)
target_include_directories(imgui_colortextedit_lib PUBLIC ${imgui_colortextedit_SOURCE_DIR})
target_link_libraries(imgui_colortextedit_lib PUBLIC imgui)

# Build MemoryEditor
add_library(imgui_memory_editor_lib INTERFACE)
target_include_directories(imgui_memory_editor_lib INTERFACE
    ${imgui_club_SOURCE_DIR}/imgui_memory_editor
)
target_link_libraries(imgui_memory_editor_lib INTERFACE imgui)

# Build FileDialog
add_library(imgui_filedialog_lib STATIC
    ${imgui_file_dialog_SOURCE_DIR}/ImGuiFileDialog.cpp
)
target_include_directories(imgui_filedialog_lib PUBLIC 
    ${imgui_file_dialog_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
)
target_link_libraries(imgui_filedialog_lib PUBLIC imgui)

file(GLOB_RECURSE STUDIO_SOURCES "src/*.cpp")
file(GLOB_RECURSE STUDIO_HEADERS "include/*.hpp")

add_executable(nWiiUStudio ${STUDIO_SOURCES} ${STUDIO_HEADERS})

target_include_directories(nWiiUStudio PRIVATE 
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../nWiiUAnalyzer/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../nWiiURecomp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../nWiiURuntime/include
)

target_link_libraries(nWiiUStudio PRIVATE
    raylib
    imgui
    rlimgui
    imgui_colortextedit_lib
    imgui_memory_editor_lib
    imgui_filedialog_lib
    nwiiu_recomp
    nwiiu_analyzer
    nwiiu_runtime
)

if (WIN32)
    target_link_libraries(nWiiUStudio PRIVATE winmm)
endif()
