cmake_minimum_required(VERSION 3.0...3.25)

project(
    LearnWebGPU
    VERSION 0.1.0
    LANGUAGES CXX C
)

if (NOT EMSCRIPTEN)
    # Native build: use local glfw
    add_subdirectory(glfw)
else()
    # Emscripten build: mock GLFW target
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()

# Include dependencies
add_subdirectory(webgpu)
add_subdirectory(glfw3webgpu)

# Collect all .cpp and .h files in Math/
file(GLOB MATH_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/PanduMath/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PanduMath/*.h
)

# Create the library
add_library(PanduMath ${MATH_SOURCES})

target_include_directories(PanduMath PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/PanduMath)

# Main executable
add_executable(App main.cpp Utils.h Application.h Application.cpp)

# Compiler settings
target_compile_features(App PRIVATE cxx_std_17)
set_target_properties(App PROPERTIES
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR OFF
)

if (MSVC)
    target_compile_options(App PRIVATE /W4)
else()
    target_compile_options(App PRIVATE -Wall -Wextra -pedantic)
endif()

target_link_libraries(App PRIVATE webgpu glfw glfw3webgpu PanduMath)

# Copy WebGPU runtime binaries for native builds
if (NOT EMSCRIPTEN)
    target_copy_webgpu_binaries(App)
endif()


# Emscripten-specific options
if (EMSCRIPTEN)
    target_link_options(App PRIVATE
        -sUSE_GLFW=3
        --use-port=emdawnwebgpu # New Dawn WebGPU port
        -sASYNCIFY
        -sALLOW_MEMORY_GROWTH
        #--preload-file "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    )

    # Output HTML instead of just Wasm
    set_target_properties(App PROPERTIES SUFFIX ".html")
endif()


