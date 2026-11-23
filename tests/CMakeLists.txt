add_executable(riktig-cpp-case-runner
    runner/CaseRunner.cpp
    runner/NaiveRename.cpp
)
target_link_libraries(riktig-cpp-case-runner PRIVATE riktigrefactor-cpp)
target_compile_definitions(riktig-cpp-case-runner PRIVATE RIKTIG_TEST_CXX="${RIKTIG_CLANGXX}")

file(GLOB_RECURSE case_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cases/*/case.json")
foreach(case_file IN LISTS case_files)
    get_filename_component(case_directory "${case_file}" DIRECTORY)
    file(RELATIVE_PATH case_name "${CMAKE_CURRENT_SOURCE_DIR}/cases" "${case_directory}")
    add_test(NAME "${case_name}" COMMAND riktig-cpp-case-runner "${case_directory}")
endforeach()
