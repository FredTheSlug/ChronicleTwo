# darkchronicle_tests links the whole port and game so a test can exercise a replacement unit against the
# real game types. Each *_test.cpp holds GoogleTest cases; ctest runs one process per case, since the
# game's state is global.
include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.18.0
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS NAMES GTest)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
add_executable(darkchronicle_tests ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${TEST_SOURCES})
set_target_properties(darkchronicle_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(darkchronicle_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
ct_link_game(darkchronicle_tests)
kz_port_tests(darkchronicle_tests)
target_link_libraries(darkchronicle_tests PRIVATE GTest::gtest nlohmann_json::nlohmann_json)
target_include_directories(darkchronicle_tests PRIVATE ${CMAKE_SOURCE_DIR}/tools/dc2data)
target_compile_definitions(darkchronicle_tests PRIVATE "DC2_DISC_DIR=\"${CMAKE_SOURCE_DIR}/rom/pal/extracted/iso\"")

# A case is a unit test: it finishes in milliseconds, and one that does not is cut off. The clock
# cases measure short sleeps, which the parallel gfx cases skew, so they run on their own.
set(CT_TEST_TIMEOUT 5 CACHE STRING "Seconds a test case may run before it is cut off")
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER -PlatformClock.*
    PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT})
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER PlatformClock.*
    PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT} RUN_SERIAL TRUE)

# ctest reports a process that dies by abort() as failed even when PASS_REGULAR_EXPRESSION matches its
# output, so the executable runs under a script that only forwards its output and the regex judges that.
# Its shader compile loads the CPU, so it runs alone with the clock cases.
add_dependencies(darkchronicle_tests darkchronicle)
set(SMOKE_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/smoke_launch.cmake)
file(WRITE ${SMOKE_SCRIPT} [=[
file(MAKE_DIRECTORY "$ENV{DC2_DATA}")
execute_process(COMMAND "${EXE}" --headless --offscreen
    OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
set(log "${out}${err}")
message("${log}")
string(FIND "${log}" "dataread: indexed 0 files under " index_at)
string(FIND "${log}" "mgInit: PlayStation 2 code is not implemented on PC" abort_at)
if(index_at GREATER -1 AND abort_at GREATER index_at)
    message("SMOKE_OK")
endif()
]=])
add_test(NAME Smoke.LaunchAbortsAtFirstHardwareCall
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:darkchronicle> -P ${SMOKE_SCRIPT})
set_tests_properties(Smoke.LaunchAbortsAtFirstHardwareCall PROPERTIES
    PASS_REGULAR_EXPRESSION "SMOKE_OK"
    ENVIRONMENT "DC2_SAVE=${CMAKE_BINARY_DIR}/smoke-save;DC2_DATA=${CMAKE_BINARY_DIR}/smoke-data"
    TIMEOUT 120 RUN_SERIAL TRUE)

file(GLOB ct_stream_cmake CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake)
foreach(f IN LISTS ct_stream_cmake)
    include(${f})
endforeach()
