cmake_minimum_required( VERSION 3.5 )

project( VSyncSimulator )

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall" )
set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -fno-limit-debug-info -Wall" )

set( CMAKE_CXX_STANDARD 17 )

add_executable( ${PROJECT_NAME} "main.cpp" )

