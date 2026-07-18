cmake_minimum_required(VERSION 3.24)

project(AgentLatch VERSION 0.2.0 LANGUAGES CXX RC)

set(AGENTLATCH_VERSION_LABEL "${PROJECT_VERSION}" CACHE STRING "User-visible AgentLatch version label")

configure_file(
    resources/version.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
    @ONLY
)

add_executable(AgentLatch WIN32
    src/main.cpp
    src/app.cpp
    src/ui.cpp
    src/types.cpp
    src/win_util.cpp
    src/settings.cpp
    src/power_request.cpp
    src/latch_registry.cpp
    src/agent_detector.cpp
    src/ipc.cpp
    src/hook_bridge.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
)

target_compile_features(AgentLatch PRIVATE cxx_std_20)
target_include_directories(AgentLatch PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources"
)
target_compile_definitions(AgentLatch PRIVATE
    AGENTLATCH_VERSION="${AGENTLATCH_VERSION_LABEL}"
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)
target_link_libraries(AgentLatch PRIVATE
    advapi32
    dwmapi
    gdi32
    kernel32
    shell32
    user32
)

if(MINGW)
    # Some portable MinGW distributions omit the implicit C++ runtime when
    # CMake archives a large WIN32 target before linking it. Keep the runtime
    # explicit and static so the release remains a single executable.
    target_link_libraries(AgentLatch PRIVATE stdc++)
endif()

if(MSVC)
    target_compile_options(AgentLatch PRIVATE
        /O1
        /GL
        /GS
        /guard:cf
        /sdl
        /W4
        /WX
        /permissive-
        /EHsc
        /utf-8
    )
    target_link_options(AgentLatch PRIVATE
        /LTCG
        /OPT:REF
        /OPT:ICF
        /DYNAMICBASE
        /NXCOMPAT
        /HIGHENTROPYVA
        /guard:cf
        /MANIFEST:NO
    )
else()
    target_compile_options(AgentLatch PRIVATE
        -Os
        -fabi-version=19
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -ffunction-sections
        -fdata-sections
    )
    target_link_options(AgentLatch PRIVATE
        -municode
        -mwindows
        -static-libgcc
        -static-libstdc++
        -Wl,--gc-sections
        -Wl,--dynamicbase
        -Wl,--nxcompat
        -Wl,--high-entropy-va
    )
endif()

set_target_properties(AgentLatch PROPERTIES
    OUTPUT_NAME "AgentLatch"
)
