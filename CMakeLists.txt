cmake_minimum_required(VERSION 3.16)
project(LengineCS LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# 1. Find SDL3 and SDL3_mixer packages
find_package(SDL3 REQUIRED)
find_package(SDL3_mixer REQUIRED)
find_package(SDL3_image REQUIRED)

# 2. Define the ImGui paths
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")

# 3. Explicitly list the clean ImGui source files
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

# 4. Create the main executable target
add_executable(LengineCS 
    "LEngine.cpp"
    ${IMGUI_SOURCES}
)

# 5. Tell the compiler where to find ImGui headers
target_include_directories(LengineCS PRIVATE 
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# 6. Link against SDL3 and SDL3_mixer 
target_link_libraries(LengineCS PRIVATE 
    SDL3::SDL3
    SDL3_mixer::SDL3_mixer
    SDL3_image::SDL3_image
)

# Automatically copy the default music folder to the executable's output directory
if(TARGET LengineCS)
    add_custom_command(TARGET LengineCS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/music"
        "$<TARGET_FILE_DIR:LengineCS>/music"
        COMMENT "Copying music library to build output directory..."
    )
endif()