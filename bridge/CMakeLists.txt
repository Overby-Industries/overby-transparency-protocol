# The bridge between OTF-1 / OTB-1 and the ledger. Optional, and off by default:
# the ledger's core library depends on no other Overby repository, and building
# the bridge is the only thing here that needs them.
#
#     git submodule update --init
#     cmake -S . -B build -DOTP_BUILD_BRIDGE=ON
#
# They are taken as submodules under bridge/deps so the exact versions of the
# two formats the bridge was built and tested against are part of this repo.

foreach(dep telemetry-protocol transparency-protocol)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep}/CMakeLists.txt")
        message(FATAL_ERROR
            "The ${dep} submodule is missing. Fetch it with:\n"
            "    git submodule update --init\n"
            "The bridge needs it; the ledger's core library does not.")
    endif()
endforeach()

# Each brings its own library (otf1::otf1, otb1::otb1). Their tests and tools
# are not built when they are not the top-level project.
add_subdirectory(deps/telemetry-protocol ${CMAKE_BINARY_DIR}/bridge-deps/otf1)
add_subdirectory(deps/transparency-protocol ${CMAKE_BINARY_DIR}/bridge-deps/otb1)

add_library(otp_bridge STATIC
    src/otp_bridge.cpp
    src/sha256.cpp
)
add_library(otp::bridge ALIAS otp_bridge)

target_include_directories(otp_bridge
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(otp_bridge PUBLIC otp otf1::otf1 otb1::otb1)

if(MSVC)
    target_compile_options(otp_bridge PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(otp_bridge PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

add_executable(otp_bridge_tests tests/bridge_tests.cpp)
target_link_libraries(otp_bridge_tests PRIVATE otp_bridge)
target_include_directories(otp_bridge_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# The golden vectors and the example packets the tests read.
target_compile_definitions(otp_bridge_tests PRIVATE
    OTP_BRIDGE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(otp_bridge_tests PRIVATE /utf-8)
endif()
add_test(NAME otp_bridge_tests COMMAND otp_bridge_tests)
