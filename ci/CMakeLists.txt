
cmake_minimum_required( VERSION 3.8 FATAL_ERROR )

project( OpenFBX_Test
  LANGUAGES C CXX
)

add_executable( OpenFBX_Test
  ../src/miniz.h
  ../src/miniz.c
  ../src/ofbx.h
  ../src/ofbx.cpp
  main.cpp
)

target_compile_features( OpenFBX_Test PRIVATE cxx_std_11 )

target_include_directories( OpenFBX_Test
    PUBLIC
    ../src
)
