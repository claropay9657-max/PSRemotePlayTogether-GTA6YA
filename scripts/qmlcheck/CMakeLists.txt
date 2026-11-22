# Compiles every QML file of the app with the Qt it is built against; see qmlcheck.cpp.
cmake_minimum_required(VERSION 3.16)
project(qmlcheck CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Quick)
add_executable(qmlcheck qmlcheck.cpp)
target_link_libraries(qmlcheck Qt6::Quick)
