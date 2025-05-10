cmake_minimum_required(VERSION 3.10.2)
project("whizwordz-lib")

add_library( whizwordz-lib
    SHARED
    AudioEngine.cpp )

find_library(log-lib log)
find_library(android-lib android)
find_library(aaudio-lib aaudio)

target_include_directories(whizwordz-lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(whizwordz-lib
    ${aaudio-lib}
    ${android-lib}
    ${log-lib})
