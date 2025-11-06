cmake_minimum_required(VERSION 3.14)
project(KVServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Find MySQL Connector/C++
find_path(MYSQLCPPCONN_INCLUDE_DIR mysql_connection.h /usr/include/mysql-cppconn-8)
find_library(MYSQLCPPCONN_LIBRARY mysqlcppconn8)

# httplib (header-only)
add_library(httplib INTERFACE)
target_include_directories(httplib INTERFACE ./include)

add_executable(server server.cpp cache.hpp db.hpp thread_pool.hpp)
target_link_libraries(server PRIVATE httplib ${MYSQLCPPCONN_LIBRARY} pthread)
target_include_directories(server PRIVATE ${MYSQLCPPCONN_INCLUDE_DIR})

add_executable(loadgen load_generator.cpp)
target_link_libraries(loadgen PRIVATE pthread)