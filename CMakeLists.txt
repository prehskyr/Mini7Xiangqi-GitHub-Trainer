cmake_minimum_required(VERSION 3.20)
project(Mini7XiangqiGitHubTrainer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mini7_core
    src/rules.cpp
    src/mini7net.cpp
)
target_include_directories(mini7_core PUBLIC src)

add_executable(Mini7TrainerCLI
    src/trainer_cli.cpp
    src/uci_process.cpp
)
target_link_libraries(Mini7TrainerCLI PRIVATE mini7_core)

if(WIN32)
    target_compile_definitions(Mini7TrainerCLI PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

if(MSVC)
    target_compile_options(mini7_core PRIVATE /utf-8 /W4 /EHsc)
    target_compile_options(Mini7TrainerCLI PRIVATE /utf-8 /W4 /EHsc)
else()
    target_compile_options(mini7_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(Mini7TrainerCLI PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(Mini7TrainerCLI PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)


add_executable(MockUciEngine tests/mock_uci_engine.cpp)
set_target_properties(MockUciEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

enable_testing()
add_executable(rules_test tests/rules_test.cpp)
target_link_libraries(rules_test PRIVATE mini7_core)
add_test(NAME rules_test COMMAND rules_test)

add_executable(net_test tests/net_test.cpp)
target_link_libraries(net_test PRIVATE mini7_core)
add_test(NAME net_test COMMAND net_test)

# The inherited tests use assert() for validation and for a few setup calls.
# Keep assertions enabled even in Release builds.
if(MSVC)
    target_compile_options(rules_test PRIVATE /UNDEBUG)
    target_compile_options(net_test PRIVATE /UNDEBUG)
else()
    target_compile_options(rules_test PRIVATE -UNDEBUG)
    target_compile_options(net_test PRIVATE -UNDEBUG)
endif()
