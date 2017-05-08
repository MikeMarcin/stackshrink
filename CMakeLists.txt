cmake_minimum_required (VERSION 3.8)
set( CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../cmake-utils/Modules )

project (StackShrink)
add_executable(StackShrink stack_shrink.cpp)

set(Boost_USE_STATIC_LIBS        ON)
set(Boost_USE_MULTITHREADED      ON)

find_package( Boost 1.64.0 COMPONENTS REQUIRED context )
include_directories( ${Boost_INCLUDE_DIRS} )
add_definitions( -DBOOST_ALL_NO_LIB=1 )

add_executable(CoShrink co_shrink.cpp)
target_link_libraries( CoShrink ${Boost_LIBRARIES} )

add_executable(AweShrink awe_shrink.cpp)
target_link_libraries( AweShrink ${Boost_LIBRARIES} )
