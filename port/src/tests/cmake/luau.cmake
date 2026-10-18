# Script decoder, command catalog and corpus survey that the Luau script replacement is built from.
add_test(NAME Luau.Tools
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_SOURCE_DIR}/tools/luau" -p "test_*.py")
set_tests_properties(Luau.Tools PROPERTIES LABELS cpu TIMEOUT 60)

# The checked-in catalog and report must match the published corpus; skipped without game data.
add_test(NAME Luau.Survey
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/luau/survey.py" --check)
set_tests_properties(Luau.Survey PROPERTIES LABELS cpu SKIP_RETURN_CODE 77 TIMEOUT 600)
