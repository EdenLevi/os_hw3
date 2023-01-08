cmake_minimum_required(VERSION 3.21)
project(os_hw3)

set(CMAKE_CXX_STANDARD 14)

add_executable(os_hw3 client.c request.c request.h segel.c server.c output.c segel.h )
