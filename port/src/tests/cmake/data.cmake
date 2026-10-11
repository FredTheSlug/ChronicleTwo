get_target_property(dc2_includes darkchronicle_tests INCLUDE_DIRECTORIES)
list(FIND dc2_includes "${CMAKE_SOURCE_DIR}/tools/dc2data" dc2_include_index)
if(dc2_include_index EQUAL -1)
    target_include_directories(darkchronicle_tests PRIVATE ${CMAKE_SOURCE_DIR}/tools/dc2data)
endif()

get_target_property(dc2_definitions darkchronicle_tests COMPILE_DEFINITIONS)
string(FIND "${dc2_definitions}" "DC2_DISC_DIR=" dc2_disc_index)
if(dc2_disc_index EQUAL -1)
    target_compile_definitions(darkchronicle_tests PRIVATE "DC2_DISC_DIR=\"${CMAKE_SOURCE_DIR}/rom/pal/extracted/iso\"")
endif()

target_compile_definitions(darkchronicle_tests PRIVATE "DC2_ISO_PATH=\"${CMAKE_SOURCE_DIR}/rom/pal/Dark Chronicle (PAL).iso\"")
