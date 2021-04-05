cmake_minimum_required(VERSION 3.16)
project(cdutt2021_circus)

set(CMAKE_CXX_STANDARD 14)

add_executable(cdutt2021_circus main.cpp)
target_compile_definitions(cdutt2021_circus PUBLIC LOCAL_RUN)