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
include(FindPkgConfig)

if(EXISTS "/opt/homebrew/opt/icu4c@78/lib/pkgconfig")
    if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
        set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/opt/icu4c@78/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    else()
        set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/opt/icu4c@78/lib/pkgconfig")
    endif()
endif()

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
find_package(ICU QUIET COMPONENTS uc i18n)

if(NOT ICU_FOUND AND EXISTS "/opt/homebrew/opt/icu4c@78")
    set(ICU_ROOT "/opt/homebrew/opt/icu4c@78")
    find_package(ICU QUIET COMPONENTS uc i18n)
endif()

add_library(vitality_icu INTERFACE)
if(ICU_FOUND)
    if(EXISTS "/opt/homebrew/opt/icu4c@78/include/unicode/brkiter.h")
        target_include_directories(vitality_icu INTERFACE /opt/homebrew/opt/icu4c@78/include)
    elseif(ICU_INCLUDE_DIR)
        target_include_directories(vitality_icu INTERFACE ${ICU_INCLUDE_DIR})
    endif()
    target_link_libraries(vitality_icu INTERFACE ICU::uc ICU::i18n)
else()
    pkg_check_modules(ICU_UC REQUIRED IMPORTED_TARGET icu-uc)
    pkg_check_modules(ICU_I18N REQUIRED IMPORTED_TARGET icu-i18n)
    target_link_libraries(vitality_icu INTERFACE PkgConfig::ICU_UC PkgConfig::ICU_I18N)
endif()

find_package(harfbuzz CONFIG QUIET)
if(NOT TARGET harfbuzz::harfbuzz)
    pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
endif()

add_library(vitality_harfbuzz INTERFACE)
if(TARGET harfbuzz::harfbuzz)
    target_link_libraries(vitality_harfbuzz INTERFACE harfbuzz::harfbuzz)
elseif(TARGET PkgConfig::HARFBUZZ)
    target_link_libraries(vitality_harfbuzz INTERFACE PkgConfig::HARFBUZZ)
else()
    message(FATAL_ERROR "Failed to resolve a HarfBuzz target.")
endif()

function(vitality_target_runtime_location target output_var)
    foreach(property_name
        IMPORTED_LOCATION
        IMPORTED_LOCATION_RELEASE
        IMPORTED_LOCATION_RELWITHDEBINFO
        IMPORTED_LOCATION_MINSIZEREL
        IMPORTED_LOCATION_DEBUG
        IMPORTED_LOCATION_NOCONFIG
        LOCATION)
        get_target_property(location_value ${target} ${property_name})
        if(location_value AND NOT location_value STREQUAL "location_value-NOTFOUND")
            set(${output_var} "${location_value}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${output_var} "" PARENT_SCOPE)
endfunction()

if(APPLE)
    vitality_target_runtime_location(Qt6::Gui VITALITY_QT_GUI_LOCATION)
    if(TARGET harfbuzz::harfbuzz)
        vitality_target_runtime_location(harfbuzz::harfbuzz VITALITY_HARFBUZZ_LOCATION)
    endif()

    if(VITALITY_QT_GUI_LOCATION AND VITALITY_HARFBUZZ_LOCATION)
        execute_process(
            COMMAND otool -L "${VITALITY_QT_GUI_LOCATION}"
            OUTPUT_VARIABLE VITALITY_QT_GUI_OTOOL_OUTPUT
            RESULT_VARIABLE VITALITY_QT_GUI_OTOOL_RESULT
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(VITALITY_QT_GUI_OTOOL_RESULT EQUAL 0)
            string(REGEX MATCH "[^ \n\t]*libharfbuzz[^ \n\t]*" VITALITY_QT_LINKED_HARFBUZZ "${VITALITY_QT_GUI_OTOOL_OUTPUT}")
            if(VITALITY_QT_LINKED_HARFBUZZ)
                file(REAL_PATH "${VITALITY_QT_LINKED_HARFBUZZ}" VITALITY_QT_LINKED_HARFBUZZ_REAL)
                file(REAL_PATH "${VITALITY_HARFBUZZ_LOCATION}" VITALITY_PROJECT_HARFBUZZ_REAL)

                if(NOT VITALITY_QT_LINKED_HARFBUZZ_REAL STREQUAL VITALITY_PROJECT_HARFBUZZ_REAL)
                    message(FATAL_ERROR
                        "QtGui and Vitality are using different HarfBuzz libraries.\n"
                        "QtGui links: ${VITALITY_QT_LINKED_HARFBUZZ_REAL}\n"
                        "Vitality would link: ${VITALITY_PROJECT_HARFBUZZ_REAL}\n"
                        "Use one coherent package source for Qt and HarfBuzz. "
                        "On macOS/Homebrew, prefer Homebrew Qt and Homebrew HarfBuzz together.")
                endif()

                message(STATUS "Qt/HarfBuzz coherence check passed: ${VITALITY_PROJECT_HARFBUZZ_REAL}")
            else()
                message(WARNING "Could not determine the HarfBuzz library linked by QtGui from otool output.")
            endif()
        else()
            message(WARNING "Failed to inspect QtGui with otool for HarfBuzz coherence.")
        endif()
    else()
        message(WARNING "Skipping Qt/HarfBuzz coherence check because one or more runtime locations could not be resolved.")
    endif()
endif()

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

set(VITALITY_LAYOUT_PCH
    <algorithm>
    <cstdint>
    <limits>
    <optional>
    <string_view>
    <vector>
    <QString>
    <QTextLayout>
    <QTextOption>
)

set(VITALITY_UNICODE_PCH
    <algorithm>
    <cstdint>
    <limits>
    <memory>
    <optional>
    <string>
    <string_view>
    <vector>
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
    src/core/PositionConversions.h
)
target_compile_features(vitality_core PUBLIC cxx_std_20)
target_include_directories(vitality_core PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_library(vitality_unicode STATIC
    src/unicode/UnicodeLineOps.cpp
    src/unicode/UnicodeLineOps.h
)
target_compile_features(vitality_unicode PUBLIC cxx_std_20)
target_include_directories(vitality_unicode PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vitality_unicode
    PUBLIC
        vitality_core
        vitality_icu
        vitality_harfbuzz
)
if(VITALITY_ENABLE_PCH)
    target_precompile_headers(vitality_unicode PRIVATE ${VITALITY_UNICODE_PCH})
endif()

add_library(vitality_layout STATIC
    src/layout/LayoutCursorOps.cpp
    src/layout/LayoutCursorOps.h
    src/layout/QtLineLayout.cpp
    src/layout/QtLineLayout.h
)
target_compile_features(vitality_layout PUBLIC cxx_std_20)
target_include_directories(vitality_layout PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vitality_layout
    PUBLIC
        vitality_core
        vitality_unicode
        Qt6::Core
        Qt6::Gui
)
if(VITALITY_ENABLE_PCH)
    target_precompile_headers(vitality_layout PRIVATE ${VITALITY_LAYOUT_PCH})
endif()

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
target_link_libraries(vitality_buffer PUBLIC vitality_core vitality_unicode)
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
    src/ui/EditorNavigationModel.cpp
    src/ui/EditorNavigationModel.h
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
        vitality_layout
        vitality_unicode
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
        tests/unit/EditorScrollAreaTests.cpp
        tests/unit/StatusBarTextTests.cpp
        tests/unit/CursorModelTypesTests.cpp
        tests/unit/LayoutCursorOpsTests.cpp
        tests/unit/UnicodeDependencyTests.cpp
        tests/unit/UnicodeLineOpsTests.cpp
        tests/unit/Utf8QtTextMapperTests.cpp
    )
    target_compile_features(vitality_tests PRIVATE cxx_std_20)
    target_include_directories(vitality_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${rapidcheck_SOURCE_DIR}/extras/catch/include
    )
    target_link_libraries(vitality_tests
        PRIVATE
            vitality_ui
            vitality_buffer
            vitality_core
            vitality_layout
            vitality_unicode
            Catch2::Catch2WithMain
            rapidcheck
    )
    if(VITALITY_ENABLE_PCH)
        target_precompile_headers(vitality_tests PRIVATE ${VITALITY_TEST_PCH})
    endif()

    include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
    catch_discover_tests(vitality_tests)
endif()
