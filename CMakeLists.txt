cmake_minimum_required(VERSION 3.20)
enable_testing()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(FSubFuscator)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "LLVM version: " ${LLVM_VERSION})
include(AddLLVM)
set(LLVM_OPTIONAL_SOURCES fsubfuscator.cpp)
set(LLVM_LINK_COMPONENTS core support irreader irprinter bitwriter passes vectorize transformutils instcombine scalaropts analysis)

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CMAKE_SOURCE_DIR})
add_compile_options(-fpic -ggdb)
add_llvm_pass_plugin(fsubfuscator_plugin FSubFuscatorPass.cpp FSubFuscatorPassPlugin.cpp)
add_llvm_executable(fsubfuscator fsubfuscator.cpp FSubFuscatorPass.cpp)
