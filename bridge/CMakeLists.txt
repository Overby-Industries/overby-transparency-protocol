# The bridge between OTF-1 / OTB-1 and the ledger. Optional, and off by default:
# the ledger's core library depends on no other Overby repository, and building
# the bridge is the only thing here that needs them.
#
#     git submodule update --init
#     cmake -S . -B build -DOTP_BUILD_BRIDGE=ON
#
# They are taken as submodules under bridge/deps so the exact versions of the
# two formats the bridge was built and tested against are part of this repo,
# unless the parent project supplies either one itself.

# The bridge needs libotf1 and libotb1. A project that already provides them
# (because it uses one of them directly, as Starlifter OS uses libotf1) passes
# nothing and gets its own: the targets are only added here when they do not
# already exist, so two copies of one library never collide.
function(otp_bridge_need target dep subdirectory)
    if(TARGET ${target})
        return()
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep}/CMakeLists.txt")
        message(FATAL_ERROR
            "The bridge needs ${target}, and the ${dep} submodule is missing. Fetch it with:\n"
            "    git submodule update --init\n"
            "or provide ${target} from the parent project before adding this one.\n"
            "The ledger's core library does not need it.")
    endif()
    # Each brings its own library. Their tests and tools are not built when
    # they are not the top-level project.
    add_subdirectory(deps/${dep} ${CMAKE_BINARY_DIR}/bridge-deps/${subdirectory})
endfunction()

otp_bridge_need(otf1::otf1 telemetry-protocol otf1)
otp_bridge_need(otb1::otb1 transparency-protocol otb1)

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

if(OTP_BUILD_TESTS)
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
endif()
