# superseded by Smoke.LaunchPassesMgInit now that mgInit is implemented
set_tests_properties(Smoke.LaunchAbortsAtFirstHardwareCall PROPERTIES DISABLED TRUE)

# Crashes after dataread are expected without game data in the smoke directories; only the mgInit path is judged.
set(SMOKE_MGLIB_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/smoke_mglib.cmake)
file(WRITE ${SMOKE_MGLIB_SCRIPT} [=[
file(MAKE_DIRECTORY "$ENV{DC2_DATA}")
execute_process(COMMAND "${EXE}" --headless --offscreen
    OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
set(log "${out}${err}")
message("${log}")
string(FIND "${log}" "dataread: indexed " index_at)
string(FIND "${log}" "mglib.cpp" mglib_at)
string(FIND "${log}" "mgInit: PlayStation 2 code is not implemented" abort_at)
if(index_at GREATER -1 AND mglib_at EQUAL -1 AND abort_at EQUAL -1)
    message("SMOKE_OK")
endif()
]=])
add_test(NAME Smoke.LaunchPassesMgInit
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:darkchronicle> -P ${SMOKE_MGLIB_SCRIPT})
set_tests_properties(Smoke.LaunchPassesMgInit PROPERTIES
    PASS_REGULAR_EXPRESSION "SMOKE_OK"
    ENVIRONMENT "DC2_SAVE=${CMAKE_BINARY_DIR}/smoke-mglib-save;DC2_DATA=${CMAKE_BINARY_DIR}/smoke-mglib-data"
    TIMEOUT 120 RUN_SERIAL TRUE)
