cmake_minimum_required(VERSION 3.16)

project(
    exec_path_args
        VERSION
            0.1.0 # beta
        HOMEPAGE_URL
            "https://github.com/Ruzovej/exec_path_args"
        DESCRIPTION
            "C++ wrapper/interface around execv function"
        LANGUAGES
            CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# TODO all interesting ones, e.g.:
# - https://github.com/google/sanitizers
# - https://hpc-wiki.info/hpc/Compiler_Sanitizers
# - https://clang.llvm.org/docs/index.html (search for `sanitizer`)
# - https://clang.llvm.org/docs/UsersManual.html#command-line-options (or here ...)
# - https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html (ditto)
option(BUILDCXX_ASAN "Enable AddressSanitizer" OFF)
option(BUILDCXX_MSAN "Enable MemorySanitizer" OFF) # Why this one doesn't work?! Maybe it's only for `clang`?
option(BUILDCXX_TSAN "Enable ThreadSanitizer" OFF)
option(BUILDCXX_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if (BUILDCXX_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address -fno-omit-frame-pointer)
endif()

if (BUILDCXX_MSAN)
    add_compile_options(-fsanitize=memory -fno-omit-frame-pointer)
    add_link_options(-fsanitize=memory -fno-omit-frame-pointer)
endif()

if (BUILDCXX_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread -fno-omit-frame-pointer)
endif()

if (BUILDCXX_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined -fno-omit-frame-pointer)
endif()

# TODO all interesting ones, e.g.:
# - https://gcc.gnu.org/onlinedocs/gcc/Option-Summary.html
# - https://clang.llvm.org/docs/DiagnosticsReference.html
#add_compile_options(-Wall -Wextra -Wpedantic)

# Check if this is being used as a subdirectory via FetchContent, add_subdirectory or similar ...
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
    set(EXECPATHARGS_TOP_LEVEL ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
else()
    set(EXECPATHARGS_TOP_LEVEL OFF)
endif()


# core library; source files:
# https://cmake.org/cmake/help/latest/command/file.html#filesystem
file(
    GLOB_RECURSE
        EXECPATHARGS_SOURCES
            CONFIGURE_DEPENDS
            "${exec_path_args_SOURCE_DIR}/src/*.cxx"
)

add_library(
    exec_path_args
        SHARED
            ${EXECPATHARGS_SOURCES}
)
target_include_directories(
    exec_path_args
        PUBLIC
            "${exec_path_args_SOURCE_DIR}/include"
        PRIVATE
            "${exec_path_args_SOURCE_DIR}/src/include"
)
#target_compile_definitions(
#    exec_path_args
#        PRIVATE
#            EXECPATHARGS_INTERNAL_IMPLEMENTATION
#)

if (EXECPATHARGS_TOP_LEVEL)
    add_subdirectory(tests/unit)
endif()
