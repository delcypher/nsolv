#   Copyright (c) Dan Liew 2012
# 
#    This file is part of NSolv.
#
#    NSolv is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    NSolv is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with NSolv.  If not, see <http://www.gnu.org/licenses/>

cmake_minimum_required(VERSION 2.8)
project(nsolv)

#The Version numbering
set (nsolv_VERSION_MAJOR 1)
set (nsolv_VERSION_MINOR 0)
set (nsolv_VERSION_STRING ${nsolv_VERSION_MAJOR}.${nsolv_VERSION_MINOR})

set(EXEC_NAME ${CMAKE_PROJECT_NAME})

# Set the possible values of build type for cmake-gui
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")

#Look for Boost
find_package(Boost 1.42 REQUIRED COMPONENTS program_options filesystem system)
include_directories(${Boost_INCLUDE_DIR})

#Look for the Realtime library
find_library(REALTIME_LIBRARY rt)

if(NOT REALTIME_LIBRARY)
	message(FATAL_ERROR "The Realtime library could not be found.")
endif()

#Look for pthreads
find_package(Threads REQUIRED)

#List source files
SET(NSOLV_SRC main.cpp SolverManager.cpp Solver.cpp)

#Configure the configuration file.
configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})

add_executable(${EXEC_NAME} ${NSOLV_SRC})
target_link_libraries(${EXEC_NAME} ${Boost_LIBRARIES} ${REALTIME_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS ${EXEC_NAME}
		RUNTIME DESTINATION bin
		)
