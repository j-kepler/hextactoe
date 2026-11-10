cmake_minimum_required(VERSION 3.16...4.4)

# ON for release builds: SDL3 is built from source and linked into the game,
# and the macOS app runs on Apple silicon and Intel Macs with macOS 11 or later.
# SDL3 is also built from source when it isn't installed.
option(HEXTACTOE_VENDORED_SDL "Build SDL3 from source and link it statically" OFF)
if(HEXTACTOE_VENDORED_SDL)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

project(HexTacToe VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT HEXTACTOE_VENDORED_SDL)
    find_package(SDL3 3.2 CONFIG QUIET COMPONENTS SDL3)
endif()
if(NOT SDL3_FOUND)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.18
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
endif()

set(SOURCES src/main.cpp src/game.cpp src/ai.cpp src/text.cpp)
if(APPLE)
    add_executable(hextactoe MACOSX_BUNDLE ${SOURCES} packaging/HexTacToe.icns)
    set_source_files_properties(packaging/HexTacToe.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(hextactoe PROPERTIES
        OUTPUT_NAME HexTacToe
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.j-kepler.hextactoe
        MACOSX_BUNDLE_ICON_FILE HexTacToe.icns
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
else()
    add_executable(hextactoe ${SOURCES})
endif()
target_link_libraries(hextactoe PRIVATE SDL3::SDL3)
