cmake_minimum_required(VERSION 3.16)
project(hello_remarkable VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Same CMakeLists works for a desktop build (system Qt6) and a cross build
# (Qt6 comes from the reMarkable SDK sysroot once its env file is sourced).
find_package(Qt6 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(hello_remarkable src/main.cpp)

qt_add_qml_module(hello_remarkable
    URI remarkable_example
    VERSION 1.0
    QML_FILES src/Main.qml)

target_link_libraries(hello_remarkable PRIVATE Qt6::Quick)
