cmake_minimum_required(VERSION 3.24)

project(vitality LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VITALITY_ENABLE_CCACHE "Use ccache as a compiler launcher when available" ON)
option(VITALITY_ENABLE_PCH "Use target-specific precompiled headers" ON)

if(VITALITY_ENABLE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    endif()
endif()

include(FetchContent)
include(CTest)
include(CheckCXXSourceCompiles)

check_cxx_source_compiles(
    "
    #include <functional>
    #include <string>

    int main() {
        return static_cast<int>(std::hash<std::string>{}(std::string(\"vitality\")));
    }
    "
    VITALITY_CXX_STDLIB_LINKS
)

if(NOT VITALITY_CXX_STDLIB_LINKS)
    message(FATAL_ERROR
        "The selected C++ toolchain cannot link a simple std::hash<std::string> program. "
        "This usually means the compiler and C++ standard library runtime do not match. "
        "On macOS, prefer Apple's clang/clang++ in CLion unless you have configured a fully "
        "consistent Homebrew LLVM + libc++ toolchain.")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

set(VITALITY_BUFFER_PCH
    <algorithm>
    <cassert>
    <cstddef>
    <cstdint>
    <memory>
    <optional>
    <span>
    <string>
    <string_view>
    <utility>
    <vector>
)

set(VITALITY_UI_PCH
    <algorithm>
    <cmath>
    <cstdint>
    <limits>
    <QColor>
    <QFont>
    <QFontDatabase>
    <QKeyEvent>
    <QPainter>
    <QPalette>
    <QScrollBar>
    <QString>
    <QTextLayout>
    <QWidget>
)

set(VITALITY_APP_PCH
    <memory>
    <QApplication>
)

set(VITALITY_BENCH_PCH
    <chrono>
    <cstdint>
    <iomanip>
    <iostream>
    <string>
    <string_view>
    <vector>
)

set(VITALITY_TEST_PCH
    <cstdint>
    <filesystem>
    <fstream>
    <sstream>
    <string>
    <string_view>
    <utility>
    <vector>
    <catch2/catch_test_macros.hpp>
    <rapidcheck/catch.h>
)

add_library(vitality_core STATIC
    src/core/CoreTypes.h
)
target_compile_features(vitality_core PUBLIC cxx_std_20)
target_include_directories(vitality_core PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_library(vitality_buffer STATIC
    src/buffer/BufferTypes.h
    src/buffer/internal/ImplicitTreapStorageCore.cpp
    src/buffer/internal/ImplicitTreapStorageCore.h
    src/buffer/TextStorage.cpp
    src/buffer/TextStorage.h
    src/buffer/TextBuffer.cpp
    src/buffer/TextBuffer.h
    src/file/FilePath.cpp
    src/file/FilePath.h
)
target_compile_features(vitality_buffer PUBLIC cxx_std_20)
target_include_directories(vitality_buffer PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vitality_buffer PUBLIC vitality_core)
if(VITALITY_ENABLE_PCH)
    target_precompile_headers(vitality_buffer PRIVATE ${VITALITY_BUFFER_PCH})
endif()

add_executable(vitality_text_storage_bench
    benchmarks/text_storage_bench.cpp
)
target_compile_features(vitality_text_storage_bench PRIVATE cxx_std_20)
target_include_directories(vitality_text_storage_bench PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vitality_text_storage_bench PRIVATE vitality_buffer vitality_core)
if(VITALITY_ENABLE_PCH)
    target_precompile_headers(vitality_text_storage_bench PRIVATE ${VITALITY_BENCH_PCH})
endif()

add_library(vitality_ui STATIC
    src/ui/EditorScrollArea.cpp
    src/ui/EditorScrollArea.h
    src/ui/StatusBarText.cpp
    src/ui/StatusBarText.h
    src/ui/Utf8QtTextMapper.cpp
    src/ui/Utf8QtTextMapper.h
)
target_compile_features(vitality_ui PUBLIC cxx_std_20)
target_include_directories(vitality_ui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vitality_ui
    PUBLIC
        vitality_buffer
        vitality_core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)
if(VITALITY_ENABLE_PCH)
    target_precompile_headers(vitality_ui PRIVATE ${VITALITY_UI_PCH})
endif()

add_executable(vitality
    src/app/main.cpp
)
target_compile_features(vitality PRIVATE cxx_std_20)
target_link_libraries(vitality PRIVATE vitality_ui)
if(VITALITY_ENABLE_PCH)
    target_precompile_headers(vitality PRIVATE ${VITALITY_APP_PCH})
endif()

if(BUILD_TESTING)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS ON CACHE BOOL "" FORCE)
    set(RC_ENABLE_CATCH OFF CACHE BOOL "" FORCE)
    set(RC_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(RC_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG dfc2dff8d70d083c60c1c6986030e5389a867a93
    )
    FetchContent_Declare(
        rapidcheck
        GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
        GIT_TAG b96a4e626ef4c7348dcd16c500353c2f997a9f3f
    )
    FetchContent_MakeAvailable(Catch2 rapidcheck)

    add_executable(vitality_tests
        tests/property/TextStorageEditPropertyTests.cpp
        tests/property/TextStoragePropertyTests.cpp
        tests/property/TextBufferPropertyTests.cpp
        tests/unit/ImplicitTreapStorageCoreTests.cpp
        tests/unit/TextStorageTests.cpp
        tests/unit/TextBufferTests.cpp
    )
    target_compile_features(vitality_tests PRIVATE cxx_std_20)
    target_include_directories(vitality_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${rapidcheck_SOURCE_DIR}/extras/catch/include
    )
    target_link_libraries(vitality_tests
        PRIVATE
            vitality_buffer
            vitality_core
            Catch2::Catch2WithMain
            rapidcheck
    )
    if(VITALITY_ENABLE_PCH)
        target_precompile_headers(vitality_tests PRIVATE ${VITALITY_TEST_PCH})
    endif()

    include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
    catch_discover_tests(vitality_tests)
endif()
