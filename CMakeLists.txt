cmake_minimum_required(VERSION 3.16)
project(cdutt2021_circus)

set(CMAKE_CXX_STANDARD 14)

add_executable(player1 main.cpp)
target_compile_definitions(player1 PUBLIC LOCAL_RUN)
target_compile_definitions(player1 PUBLIC LOG_FILE="log1.txt")

add_executable(player2 main.cpp)
target_compile_definitions(player2 PUBLIC LOCAL_RUN)
target_compile_definitions(player2 PUBLIC LOG_FILE="log2.txt")


add_executable(default main.cpp)