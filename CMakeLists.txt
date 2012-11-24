cmake_minimum_required(VERSION 2.8)
project(nsolv)

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


SET(NSOLV_SRC main.cpp SolverManager.cpp Solver.cpp)

add_executable(${EXEC_NAME} ${NSOLV_SRC})
target_link_libraries(${EXEC_NAME} ${Boost_LIBRARIES} ${REALTIME_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS ${EXEC_NAME}
		RUNTIME DESTINATION bin
		)
