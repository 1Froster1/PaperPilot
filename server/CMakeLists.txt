cmake_minimum_required(VERSION 3.5)
project(myapp CXX)

# Custom file extension configuration
if(NOT DEFINED DEFAULT_SOURCE_EXTENSION)
    set(DEFAULT_SOURCE_EXTENSION "cc" CACHE STRING "Default source file extension (e.g., cc, cpp, cxx)")
endif()
if(NOT DEFINED DEFAULT_HEADER_EXTENSION)
    set(DEFAULT_HEADER_EXTENSION "h" CACHE STRING "Default header file extension (e.g., h, hpp, hxx)")
endif()

include(CheckIncludeFileCXX)

check_include_file_cxx(any HAS_ANY)
check_include_file_cxx(string_view HAS_STRING_VIEW)
check_include_file_cxx(coroutine HAS_COROUTINE)
if (NOT "${CMAKE_CXX_STANDARD}" STREQUAL "")
    # Do nothing
elseif (HAS_ANY AND HAS_STRING_VIEW AND HAS_COROUTINE)
    set(CMAKE_CXX_STANDARD 20)
elseif (HAS_ANY AND HAS_STRING_VIEW)
    set(CMAKE_CXX_STANDARD 17)
else ()
    set(CMAKE_CXX_STANDARD 14)
endif ()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(MYAPP_VIEW_SOURCE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/views/LearningView.cc")
    list(APPEND MYAPP_VIEW_SOURCE views/LearningView.cc)
endif()

add_executable(${PROJECT_NAME}
    main.${DEFAULT_SOURCE_EXTENSION}
    routes.cpp
    tools.cpp
    ${MYAPP_VIEW_SOURCE}
)

# main.cc 使用相对路径读取 config.json。用 Visual Studio 启动时，
# 把调试工作目录固定为本 CMakeLists.txt 所在的 server 根目录。
if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# ##############################################################################
# If you include the drogon source code locally in your project, use this method
# to add drogon
# add_subdirectory(drogon)
# target_link_libraries(${PROJECT_NAME} PRIVATE drogon)
#
# and comment out the following lines
# Drogon was built with hiredis 1.5.0.  These cache entries let CMake find
# the matching headers and x64 static library whenever this VS solution is
# regenerated.  Application code still links only Drogon::Drogon.
if(WIN32)
    set(HIREDIS_ROOT_DIR "D:/github/hiredis-1.5.0/_install" CACHE PATH
        "Installation prefix of the hiredis used by Drogon")
    set(HIREDIS_INCLUDE_DIR "${HIREDIS_ROOT_DIR}/include" CACHE PATH
        "hiredis header directory" FORCE)
    set(HIREDIS_LIBRARY "${HIREDIS_ROOT_DIR}/lib/hiredis.lib" CACHE FILEPATH
        "x64 hiredis static library" FORCE)
endif()
find_package(Drogon CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)

# 登录示例使用 MySQL C API。显式声明头文件和导入库，确保 Visual Studio
# 与 Ninja 两种构建方式都能找到 mysql.h 和 libmysql.lib。
if(WIN32)
    find_path(MYSQL_INCLUDE_DIR mysql.h
        PATHS "C:/Program Files/MySQL/MySQL Server 8.0/include"
        REQUIRED)
    find_library(MYSQL_LIBRARY libmysql
        PATHS "C:/Program Files/MySQL/MySQL Server 8.0/lib"
        REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE ${MYSQL_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${MYSQL_LIBRARY})
endif()

# 仅当 Drogon 已带 hiredis/Redis 支持且 config.json 已配置 redis_clients 时启用。
# 本机 Redis 服务已在 127.0.0.1:6379 启动，编译真实的 PING 示例。
option(MYAPP_REDIS_READY "Enable the runnable Redis PING learning endpoint" ON)
if(MYAPP_REDIS_READY)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MYAPP_REDIS_READY=1)
endif()

# ##############################################################################

if (CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "c++17 or higher is required")
elseif (CMAKE_CXX_STANDARD LESS 20)
    message(STATUS "use c++17")
else ()
    message(STATUS "use c++20")
endif ()

aux_source_directory(controllers CTL_SRC)
aux_source_directory(filters FILTER_SRC)
aux_source_directory(plugins PLUGIN_SRC)
aux_source_directory(models MODEL_SRC)

# LearningView.cc/.h 已由 drogon_ctl 从 LearningView.csp 生成，并明确加入
# 源文件列表，确保 Visual Studio 会将该视图类链接进最终 exe。
# 修改 LearningView.csp 后，在项目根目录运行：
# D:\github\drogon-master\_install\bin\drogon_ctl.exe create view views\LearningView.csp -o views

target_include_directories(${PROJECT_NAME}
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                   ${CMAKE_CURRENT_SOURCE_DIR}/include
                                   ${CMAKE_CURRENT_SOURCE_DIR}/models)
target_sources(${PROJECT_NAME}
               PRIVATE
               ${SRC_DIR}
               ${CTL_SRC}
               ${FILTER_SRC}
               ${PLUGIN_SRC}
               ${MODEL_SRC})
# ##############################################################################
# uncomment the following line for dynamically loading views
# set_property(TARGET ${PROJECT_NAME} PROPERTY ENABLE_EXPORTS ON)

# ##############################################################################

add_subdirectory(test)
