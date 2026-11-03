cmake_minimum_required(VERSION 3.28)
project(nbody VERSION 0.1.0 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJC OBJCXX)  # OBJC too, or GLFW's .m sources get claimed by OBJCXX
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(NBODY_BUILD_APP "Build the Metal viewer (macOS only)" ${APPLE})
option(NBODY_BUILD_TESTS "Build unit tests" ON)

include(cmake/Dependencies.cmake)

add_library(nbody_warnings INTERFACE)
target_compile_options(nbody_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)

add_library(nbody_core STATIC
  src/sim/simulation.cpp
  src/sim/scenarios.cpp
  src/sim/sim_runner.cpp
  src/render/camera.cpp
)
target_include_directories(nbody_core PUBLIC src)
target_link_libraries(nbody_core PUBLIC glm_headers PRIVATE nbody_warnings)

if(NBODY_BUILD_APP)
  # Only the Command Line Tools are required: there is no offline `metal` compiler, so the
  # shader source is baked into a header and compiled by the Metal driver at startup.
  set(SHADER_SRC ${CMAKE_SOURCE_DIR}/src/render/shaders/bodies.metal)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SHADER_SRC})
  file(READ ${SHADER_SRC} SHADER_SOURCE)
  configure_file(cmake/embedded_shaders.hpp.in ${CMAKE_BINARY_DIR}/generated/embedded_shaders.hpp @ONLY)

  add_executable(nbody
    src/app/main.cpp
    src/app/app.cpp
    src/render/renderer_metal.mm
  )
  target_include_directories(nbody PRIVATE ${CMAKE_BINARY_DIR}/generated)
  target_link_libraries(nbody PRIVATE nbody_core imgui_metal_glfw nbody_warnings)
  set_source_files_properties(src/render/renderer_metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(nbody PRIVATE
    "-framework Metal" "-framework QuartzCore" "-framework Cocoa" "-framework ImageIO" "-framework CoreGraphics")
endif()

if(NBODY_BUILD_TESTS)
  enable_testing()
  add_executable(nbody_tests
    tests/main.cpp
    tests/test_physics.cpp
    tests/test_triple_buffer.cpp
    tests/test_camera.cpp
    tests/test_sim_runner.cpp
  )
  target_link_libraries(nbody_tests PRIVATE nbody_core doctest_headers nbody_warnings)
  add_test(NAME nbody_tests COMMAND nbody_tests)
endif()
