cmake_minimum_required(VERSION 3.15)
project(SoundDNA VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(_USE_MATH_DEFINES)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  add_compile_options(-fpermissive)
endif()

set(IPLUG2_DIR "../iPlug2" CACHE PATH "Path to iPlug2")
list(APPEND CMAKE_MODULE_PATH "${IPLUG2_DIR}/Scripts/cmake")

include("${IPLUG2_DIR}/iPlug2.cmake")
find_package(iPlug2 REQUIRED)
include(IPlugPlugin)

iplug_add_plugin(SoundDNA
  SOURCES
    SoundDNA.cpp
    SoundDNA.h
    config.h
    resources/resource.h
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
  FORMATS APP VST3
  UI IGRAPHICS
)

# IPlugPlugin creates format-specific targets like SoundDNA-APP and SoundDNA-VST3
# We need to add our 'src' and 'resources' directory to their include paths.
if(TARGET SoundDNA-app)
    target_include_directories(SoundDNA-app PRIVATE src resources)
    target_link_libraries(SoundDNA-app PRIVATE opengl32)
endif()
if(TARGET SoundDNA-vst3)
    target_include_directories(SoundDNA-vst3 PRIVATE src resources)
    target_link_libraries(SoundDNA-vst3 PRIVATE opengl32)
endif()

add_executable(SoundDNA-cli src/main.cpp)
target_include_directories(SoundDNA-cli PRIVATE src resources)
target_link_libraries(SoundDNA-cli PRIVATE winmm ole32)
set_target_properties(SoundDNA-cli PROPERTIES OUTPUT_NAME "SoundDNA" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

