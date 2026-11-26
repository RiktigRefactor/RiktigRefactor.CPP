cmake_minimum_required(VERSION 3.20)
project(RiktigRefactorCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The tool is pinned to one clang version: its AST and binding behavior is what the tests verify.
# .mise.toml is the one place that names the version.
file(STRINGS .mise.toml RIKTIG_MISE_CLANGDEV_LINE REGEX "^\"conda:clangdev\" = ")
string(REGEX MATCH "([0-9]+)\\.[0-9]+\\.[0-9]+" RIKTIG_MISE_CLANGDEV_VERSION "${RIKTIG_MISE_CLANGDEV_LINE}")
set(RIKTIG_CLANG_VERSION ${CMAKE_MATCH_1})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS .mise.toml)
# Release archives link LLVM and clang statically. Development builds use the shared libraries.
option(RIKTIG_STATIC_LLVM "Link LLVM and clang statically" OFF)
if(RIKTIG_STATIC_LLVM)
# Static LLVM's exported targets name ZLIB::ZLIB and zstd::libzstd_static when it was built with them.
# LLVMConfig finds zlib itself; this makes it pick the static library.
set(ZLIB_USE_STATIC_LIBS ON)
# LLVM's Findzstd only recognizes zstd_static.lib on Windows, but vcpkg names the static library zstd.lib.
find_path(RIKTIG_ZSTD_INCLUDE_DIR zstd.h)
find_library(RIKTIG_ZSTD_STATIC_LIBRARY NAMES zstd_static ${CMAKE_STATIC_LIBRARY_PREFIX}zstd${CMAKE_STATIC_LIBRARY_SUFFIX})
if(RIKTIG_ZSTD_INCLUDE_DIR AND RIKTIG_ZSTD_STATIC_LIBRARY)
    add_library(zstd::libzstd_static STATIC IMPORTED)
    set_target_properties(zstd::libzstd_static PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${RIKTIG_ZSTD_INCLUDE_DIR}"
        IMPORTED_LOCATION "${RIKTIG_ZSTD_STATIC_LIBRARY}")
endif()
# The official Windows LLVM libraries use the static C runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
# The Linux archive carries its own C++ runtime, so it needs only glibc.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_link_options(-static-libstdc++ -static-libgcc)
endif()
endif()
find_package(Clang REQUIRED CONFIG
    HINTS $ENV{RIKTIG_LLVM_ROOT}/lib/cmake/clang $ENV{RIKTIG_LLVM_ROOT}/Library/lib/cmake/clang /usr/lib/llvm-${RIKTIG_CLANG_VERSION}/lib/cmake/clang)
if(NOT LLVM_VERSION_MAJOR EQUAL RIKTIG_CLANG_VERSION)
    message(FATAL_ERROR "Found LLVM ${LLVM_VERSION}, but this project is pinned to ${RIKTIG_CLANG_VERSION}.")
endif()
find_program(RIKTIG_CLANGXX clang++-${RIKTIG_CLANG_VERSION} clang++ HINTS ${LLVM_TOOLS_BINARY_DIR} REQUIRED)

# clang-cl reads -Wall as -Weverything, so it gets the MSVC spellings.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    if(NOT LLVM_ENABLE_RTTI)
        add_compile_options(/GR-)
    endif()
    add_compile_options(/W4 -Wno-unused-parameter)
else()
    if(NOT LLVM_ENABLE_RTTI)
        add_compile_options(-fno-rtti)
    endif()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

add_library(riktigrefactor-cpp STATIC
    src/riktig/Bindings.cpp
    src/riktig/EditMap.cpp
    src/riktig/NameChecks.cpp
    src/riktig/ExtractIife.cpp
    src/riktig/ExtractObjectFromArguments.cpp
    src/riktig/FunctionRefactoring.cpp
    src/riktig/Independence.cpp
    src/riktig/MakeMethodStatic.cpp
    src/riktig/MoveStaticMember.cpp
    src/riktig/ConvertToInstanceMethod.cpp
    src/riktig/Protocol.cpp
    src/riktig/RawTokens.cpp
    src/riktig/Rename.cpp
    src/riktig/Results.cpp
    src/riktig/SourceText.cpp
    src/riktig/Verification.cpp
    src/riktig/Workspace.cpp
)
target_include_directories(riktigrefactor-cpp PUBLIC src/riktig)
target_include_directories(riktigrefactor-cpp SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
# Clang's builtin headers. The build tree uses the path from the LLVM install; a release archive
# keeps them under lib/riktig-cpp/clang/<major> beside bin/ (see the install rules below).
target_compile_definitions(riktigrefactor-cpp PRIVATE
    RIKTIG_CLANG_RESOURCE_DIR="${LLVM_LIBRARY_DIR}/clang/${LLVM_VERSION_MAJOR}"
    RIKTIG_CLANG_RESOURCE_SUFFIX="lib/riktig-cpp/clang/${LLVM_VERSION_MAJOR}")
# Linking the component libraries pulls in their dependencies through clang's and LLVM's exported targets.
if(RIKTIG_STATIC_LLVM)
if(LLVM_ENABLE_SHARED_LIBS OR CLANG_LINK_CLANG_DYLIB OR LLVM_LINK_LLVM_DYLIB)
    message(FATAL_ERROR "riktig-cpp links LLVM and clang statically; point it at a static LLVM and clang build.")
endif()
target_link_libraries(riktigrefactor-cpp PUBLIC
    clangTooling clangFrontend clangUnifiedSymbolResolution clangAST clangLex clangBasic)
else()
    target_link_libraries(riktigrefactor-cpp PUBLIC clang-cpp)
    # Distro packages ship libLLVM as a shared library; the release tarballs fold LLVM into libclang-cpp instead.
    if(TARGET LLVM)
        target_link_libraries(riktigrefactor-cpp PUBLIC LLVM)
    endif()
endif()

add_executable(riktig-cpp src/cli/main.cpp)
target_link_libraries(riktig-cpp PRIVATE riktigrefactor-cpp)

# Release packaging: `cpack --config build/CPackConfig.cmake` makes an archive holding bin/riktig-cpp.
# RIKTIG_PACKAGE_VERSION is passed by .github/workflows/release.yml; VERSION holds the base for prereleases.
install(TARGETS riktig-cpp RUNTIME DESTINATION bin)
install(FILES LICENSE README.md DESTINATION .)
install(DIRECTORY "${LLVM_LIBRARY_DIR}/clang/${LLVM_VERSION_MAJOR}/include"
    DESTINATION lib/riktig-cpp/clang/${LLVM_VERSION_MAJOR})
if(NOT RIKTIG_PACKAGE_VERSION)
    file(STRINGS VERSION RIKTIG_PACKAGE_VERSION LIMIT_COUNT 1)
endif()
set(CPACK_PACKAGE_NAME riktig-cpp)
set(CPACK_PACKAGE_VERSION "${RIKTIG_PACKAGE_VERSION}")
set(CPACK_GENERATOR TGZ)
include(CPack)

enable_testing()
add_subdirectory(tests)
