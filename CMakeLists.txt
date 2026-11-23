cmake_minimum_required(VERSION 3.20)
project(RiktigRefactorCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The tool is pinned to one clang version: its AST and binding behavior is what the tests verify.
set(RIKTIG_CLANG_VERSION 18)
find_package(Clang REQUIRED CONFIG
    HINTS $ENV{RIKTIG_LLVM_ROOT}/lib/cmake/clang /usr/lib/llvm-${RIKTIG_CLANG_VERSION}/lib/cmake/clang)
if(NOT LLVM_VERSION_MAJOR EQUAL RIKTIG_CLANG_VERSION)
    message(FATAL_ERROR "Found LLVM ${LLVM_VERSION}, but this project is pinned to ${RIKTIG_CLANG_VERSION}.")
endif()
find_program(RIKTIG_CLANGXX clang++-${RIKTIG_CLANG_VERSION} clang++ HINTS ${LLVM_TOOLS_BINARY_DIR} REQUIRED)

if(NOT LLVM_ENABLE_RTTI)
    add_compile_options(-fno-rtti)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

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
    src/riktig/MoveToInstanceMethod.cpp
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
target_compile_definitions(riktigrefactor-cpp PRIVATE
    RIKTIG_CLANG_RESOURCE_DIR="${LLVM_LIBRARY_DIR}/clang/${LLVM_VERSION_MAJOR}")
target_link_libraries(riktigrefactor-cpp PUBLIC clang-cpp)
# Distro packages ship libLLVM as a shared library; the release tarballs fold LLVM into libclang-cpp instead.
if(TARGET LLVM)
    target_link_libraries(riktigrefactor-cpp PUBLIC LLVM)
endif()

add_executable(riktig-cpp src/cli/main.cpp)
target_link_libraries(riktig-cpp PRIVATE riktigrefactor-cpp)

enable_testing()
add_subdirectory(tests)
