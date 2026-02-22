cmake_minimum_required(VERSION 3.15)
find_package(OpenGL REQUIRED)

# --- Dependencies: GLFW ---
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# --- ImGui (no root CMake; we add sources) ---
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.2
)
FetchContent_Populate(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
if(MSVC)
    target_compile_options(imgui PRIVATE /WX-)
endif()

# --- ImPlot ---
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        v0.16
)
FetchContent_Populate(implot)
add_library(implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui)

# --- qrsdp_ui executable ---
add_executable(qrsdp_ui main.cpp)
target_include_directories(qrsdp_ui PRIVATE ${CMAKE_SOURCE_DIR}/src ${imgui_SOURCE_DIR} ${implot_SOURCE_DIR})
target_link_libraries(qrsdp_ui PRIVATE simulator_lib implot glfw)
if(WIN32)
    target_link_libraries(qrsdp_ui PRIVATE opengl32)
else()
    target_link_libraries(qrsdp_ui PRIVATE pthread)
endif()
