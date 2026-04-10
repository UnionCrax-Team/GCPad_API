cmake_minimum_required(VERSION 3.15)

project(GCPad_Remap VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Input remapper - included in main build, not built separately
# (code is linked into gcpad via source include)
