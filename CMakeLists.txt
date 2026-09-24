# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

# Guition JC1060P470C fork: default target ESP32-P4
# so `idf.py build` works without explicit `idf.py set-target esp32p4`
if(NOT DEFINED ENV{IDF_TARGET} AND NOT IDF_TARGET)
    set(ENV{IDF_TARGET} "esp32p4")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "2.5.0")
project(xiaozhi)
