# The offscreen renderer of the intro film; see README.md next to this file.
cmake_minimum_required(VERSION 3.16)
project(filmshot CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Quick Gui GuiPrivate)
add_executable(filmshot filmshot.cpp)
target_link_libraries(filmshot Qt6::Quick Qt6::Gui Qt6::GuiPrivate)
