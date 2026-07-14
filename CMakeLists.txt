cmake_minimum_required(VERSION 3.16)

project(SerialServer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Network)

add_executable(SerialServer
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/portserverworker.h
    src/portserverworker.cpp
    src/portrangeparser.h
    src/portrangeparser.cpp
    src/echostreamprocessor.h
    src/echostreamprocessor.cpp
)

target_include_directories(SerialServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(SerialServer PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)

include(CTest)
if(BUILD_TESTING)
    add_executable(ServerCoreTest
        tests/servercore_test.cpp
        src/portrangeparser.h
        src/portrangeparser.cpp
        src/echostreamprocessor.h
        src/echostreamprocessor.cpp
    )
    target_include_directories(ServerCoreTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(ServerCoreTest PRIVATE Qt${QT_VERSION_MAJOR}::Core)
    add_test(NAME ServerCoreTest COMMAND ServerCoreTest)

    add_executable(PortServerWorkerTest
        tests/portserverworker_test.cpp
        src/portserverworker.h
        src/portserverworker.cpp
        src/echostreamprocessor.h
        src/echostreamprocessor.cpp
    )
    target_include_directories(PortServerWorkerTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(PortServerWorkerTest PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
    )
    add_test(NAME PortServerWorkerTest COMMAND PortServerWorkerTest)
endif()
