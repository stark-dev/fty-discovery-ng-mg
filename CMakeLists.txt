cmake_minimum_required(VERSION 3.13)

########################################################################################################################

project(fty-discovery-ng
    VERSION 1.0.0
    DESCRIPTION "New fty discovery"
)

########################################################################################################################
find_package(fty-cmake PATHS ${CMAKE_BINARY_DIR}/fty-cmake)
########################################################################################################################

add_subdirectory(common)
add_subdirectory(server)
add_subdirectory(rest)

if (BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

########################################################################################################################
