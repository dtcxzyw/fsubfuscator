cmake_minimum_required(VERSION 3.20)
enable_testing()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)
set(CMAKE_FIND_PACKAGE_SORT_DIRECTION DEC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(FSubFuscator)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "LLVM version: " ${LLVM_VERSION})
include(AddLLVM)
set(LLVM_OPTIONAL_SOURCES fsubfuscator.cpp BitRepTest.cpp test.c test.cpp)
set(LLVM_LINK_COMPONENTS core support irreader irprinter bitwriter passes vectorize transformutils instcombine scalaropts analysis)

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CMAKE_SOURCE_DIR})
add_compile_options(-fpic -ggdb)
add_llvm_pass_plugin(fsubfuscator_plugin FSubFuscatorPass.cpp FSubFuscatorPassPlugin.cpp BitRep.cpp)
add_llvm_executable(fsubfuscator fsubfuscator.cpp FSubFuscatorPass.cpp BitRep.cpp)

set(LLVM_BINARY_HINT ${LLVM_INCLUDE_DIRS}/../bin)
message(STATUS "LLVM build hint: " ${LLVM_BINARY_HINT})
find_program(CLANG clang PATHS ${LLVM_BINARY_HINT} REQUIRED NO_DEFAULT_PATH)
message(STATUS "clang: " ${CLANG})
find_program(CLANGXX clang++ PATHS ${LLVM_BINARY_HINT} REQUIRED NO_DEFAULT_PATH)
message(STATUS "clang++: " ${CLANGXX})

configure_file(${CMAKE_SOURCE_DIR}/fsubcc.in ${CMAKE_BINARY_DIR}/fsubcc @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/fsub++.in ${CMAKE_BINARY_DIR}/fsub++ @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/opt-fsub.sh.in ${CMAKE_BINARY_DIR}/opt-fsub.sh @ONLY)

option(FSUBFUSCATOR_ENABLE_TESTS "Enable fsub fuscator tests" OFF)
if (${FSUBFUSCATOR_ENABLE_TESTS})
  enable_testing()
  find_program(OPT opt PATHS ${LLVM_BINARY_HINT} REQUIRED NO_DEFAULT_PATH)
  message(STATUS "opt: " ${OPT})
  find_program(ALIVE2 alive-tv REQUIRED)
  message(STATUS "alive2: " ${ALIVE2})
  include(GoogleTest)
  find_package(GTest CONFIG REQUIRED)

  add_llvm_executable(bitreptest BitRepTest.cpp BitRep.cpp)
  target_link_libraries(bitreptest PRIVATE GTest::gtest)
  gtest_discover_tests(bitreptest)
  
  set(FSUBFUSCATOR_EXECUTABLE $<TARGET_FILE:fsubfuscator>)
  set(FSUBFUSCATOR_PLUGIN $<TARGET_FILE:fsubfuscator_plugin>)

  add_test(NAME test_standalone_int1 COMMAND sh -c "${FSUBFUSCATOR_EXECUTABLE} -S -bitrep=Int1 test.ll -o ${CMAKE_BINARY_DIR}/test_int1.ll && ${ALIVE2} -tgt-unroll=16 test.ll ${CMAKE_BINARY_DIR}/test_int1.ll" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_standalone_invint1 COMMAND sh -c "${FSUBFUSCATOR_EXECUTABLE} -S -bitrep=InvInt1 test.ll -o ${CMAKE_BINARY_DIR}/test_invint1.ll && ${ALIVE2} -tgt-unroll=16 test.ll ${CMAKE_BINARY_DIR}/test_invint1.ll" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_standalone_mod3 COMMAND sh -c "${FSUBFUSCATOR_EXECUTABLE} -S -bitrep=Mod3 test.ll -o ${CMAKE_BINARY_DIR}/test_mod3.ll" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_standalone_fsub COMMAND ${FSUBFUSCATOR_EXECUTABLE} -S -bitrep=FSub test.ll -o ${CMAKE_BINARY_DIR}/test_fsub.ll WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_opt COMMAND ${OPT} -O3 -S -load-pass-plugin ${FSUBFUSCATOR_PLUGIN} test.ll -o ${CMAKE_BINARY_DIR}/test_opt.ll WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_clang COMMAND ${CLANG} -O3 -c -emit-llvm -S -fpass-plugin=${FSUBFUSCATOR_PLUGIN} test.c -o ${CMAKE_BINARY_DIR}/test_clang.ll WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  #add_test(NAME test_opt_wrapper COMMAND ${CMAKE_BINARY_DIR}/opt-fsub.sh -O3 -S test.ll -o ${CMAKE_BINARY_DIR}/test_opt_wrapper.ll WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_clang_wrapper COMMAND ${CMAKE_BINARY_DIR}/fsubcc -O3 -c -emit-llvm -S test.c -o ${CMAKE_BINARY_DIR}/test_clang_wrapper.ll WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_clangxx_wrapper COMMAND ${CMAKE_BINARY_DIR}/fsub++ -O3 -c -emit-llvm -S test.cpp -o ${CMAKE_BINARY_DIR}/test_clangxx_wrapper.ll WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

install(TARGETS fsubfuscator)
# TODO: install wrapper scripts
