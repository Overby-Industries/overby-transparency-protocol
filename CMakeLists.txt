cmake_minimum_required(VERSION 3.16)
project(otp LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(otp STATIC
    src/otp_ledger.cpp
    src/otp_registry.cpp
    src/otp_token.cpp
)

add_library(otp::otp ALIAS otp)

target_include_directories(otp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(otp PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(otp PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(otp_tests tests/otp_tests.cpp)
target_link_libraries(otp_tests PRIVATE otp)
if(MSVC)
    target_compile_options(otp_tests PRIVATE /utf-8)
endif()
add_test(NAME otp_tests COMMAND otp_tests)

# The bridge to OTF-1 and OTB-1 is optional: the core library above depends on
# no other Overby repository, and turning this on is the only thing that needs
# them. See bridge/CMakeLists.txt.
option(OTP_BUILD_BRIDGE "Build the OTF-1/OTB-1 bridge (needs the git submodules)" OFF)
if(OTP_BUILD_BRIDGE)
    add_subdirectory(bridge)
endif()

add_executable(otp_token_tests tests/otp_token_tests.cpp)
target_link_libraries(otp_token_tests PRIVATE otp)
if(MSVC)
    target_compile_options(otp_token_tests PRIVATE /utf-8)
endif()
add_test(NAME otp_token_tests COMMAND otp_token_tests)
