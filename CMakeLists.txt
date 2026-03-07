cmake_minimum_required(VERSION 3.24)

project(vitality LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

add_library(vitality_core STATIC
    src/core/CoreTypes.h
)
target_compile_features(vitality_core PUBLIC cxx_std_20)
target_include_directories(vitality_core PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_library(vitality_buffer STATIC
    src/buffer/BufferTypes.h
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

add_executable(vitality
    src/app/main.cpp
)
target_compile_features(vitality PRIVATE cxx_std_20)
target_link_libraries(vitality PRIVATE vitality_ui)

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
        tests/property/TextBufferPropertyTests.cpp
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

    include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
    catch_discover_tests(vitality_tests)
endif()
