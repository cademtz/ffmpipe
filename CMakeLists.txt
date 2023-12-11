cmake_minimum_required(VERSION 3.20)

project(ffmpipe)

add_executable(ffmpipe example.cpp src/ffmpipe.cpp)
target_include_directories(ffmpipe PRIVATE include)
target_compile_features(ffmpipe PUBLIC cxx_std_17)