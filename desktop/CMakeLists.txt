cmake_minimum_required(VERSION 3.21)

project(PaperPilot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 让 CMake 在构建时自动处理：
# Q_OBJECT（moc）、PaperPilot.ui（uic）和 PaperPilot.qrc（rcc）。
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(PaperPilot
    WIN32
    PaperPilot/main.cpp
    PaperPilot/PaperPilot.h
    PaperPilot/PaperPilot.cpp
    PaperPilot/PaperPilot.ui
    PaperPilot/PaperPilot.qrc
    PaperPilot/mainfamework.h PaperPilot/mainfamework.cpp PaperPilot/mainfamework.ui

)

target_link_libraries(PaperPilot
    PRIVATE
        Qt6::Widgets
        Qt6::Network
)

# Windows 源码和 Designer 中的中文文本统一以 UTF-8 编译。
if(MSVC)
    target_compile_options(PaperPilot PRIVATE /utf-8)
endif()

set_target_properties(PaperPilot PROPERTIES
    WIN32_EXECUTABLE TRUE
)
