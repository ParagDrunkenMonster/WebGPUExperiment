cmake_minimum_required(VERSION 3.0...3.25)

project(
    LearnWebGPU # name of the project, which will also be the name of the visual studio solution if you use it
    VERSION 0.1.0 # any version number
    LANGUAGES CXX C # programming languages used by the project
)

if (NOT EMSCRIPTEN)
    # Add the 'glfw' directory, which contains the definition of a 'glfw' target
    add_subdirectory(glfw)
else()
    # Create a mock 'glfw' target that just sets the `-sUSE_GLFW=3` link option:
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()


# Include webgpu directory, to define the 'webgpu' target
add_subdirectory(webgpu)
add_subdirectory(glfw3webgpu)


add_executable(App main.cpp Application.h Application.cpp)

# Compiler settings
target_compile_features(App PRIVATE cxx_std_17)
set_target_properties(App PROPERTIES
    #CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR OFF
)


if (MSVC)
    target_compile_options(App PRIVATE /W4)
else()
    target_compile_options(App PRIVATE -Wall -Wextra -pedantic)
endif()


target_link_libraries(App PRIVATE webgpu glfw glfw3webgpu)


# Copy webgpu runtime binaries next to executable (only for native)
if (NOT EMSCRIPTEN)
target_copy_webgpu_binaries(App)
endif()

# At the end of the CMakeLists.txt
if (EMSCRIPTEN)
    # Remove deprecated -sUSE_WEBGPU from all targets
    foreach(t ${CMAKE_PROJECT_NAME} App webgpu glfw3webgpu)
        get_target_property(existing_opts ${t} LINK_OPTIONS)
        if (existing_opts)
            list(FILTER existing_opts EXCLUDE REGEX "-sUSE_WEBGPU")
            set_target_properties(${t} PROPERTIES LINK_OPTIONS "${existing_opts}")
        endif()
    endforeach()

	# Add Emscripten-specific link options
	target_link_options(App PRIVATE
		-sUSE_GLFW=3 # Use Emscripten-provided GLFW
		#-sUSE_WEBGPU # Handle WebGPU symbols
        --use-port=emdawnwebgpu  # New WebGPU port (replaces -sUSE_WEBGPU)
		-sASYNCIFY # Required by WebGPU-C++
		-sALLOW_MEMORY_GROWTH
		#--preload-file "${CMAKE_CURRENT_SOURCE_DIR}/resources"
	)

	# Generate a full web page rather than a simple WebAssembly module
	set_target_properties(App PROPERTIES SUFFIX ".html")
endif()

