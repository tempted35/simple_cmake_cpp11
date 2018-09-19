# Main project entry script, unique per project

CMAKE_MINIMUM_REQUIRED(VERSION 3.11)

add_definitions(-std=c++11 -g -ggdb)

# About this project
project(thread)

find_package(Boost REQUIRED COMPONENTS filesystem system)
find_package(Threads)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_executable(thread 
  threading.cpp)

target_include_directories(thread
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(thread 
  PRIVATE ${CMAKE_THREAD_LIBS_INIT} 
  PRIVATE ${Boost_LIBRARIES})

set(EXECUTABLE_OUTPUT_PATH ./build)

