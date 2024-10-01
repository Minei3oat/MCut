cmake_minimum_required(VERSION 3.6)

project(MCut VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libswscale
    libavutil
)

set(PROJECT_SOURCES
    main.cpp
    mediafile.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(MCut
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(MCut
        ${PROJECT_SOURCES}
    )
endif()

target_link_libraries(MCut PRIVATE Qt${QT_VERSION_MAJOR}::Widgets PkgConfig::LIBAV)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(MCut)
endif()
