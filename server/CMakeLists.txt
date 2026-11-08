# 3.15 起 CMP0091 默认为 NEW，MSVC_RUNTIME_LIBRARY 属性才会生效
# （本机 Drogon 为 Release 构建，Debug 配置也需用 /MD 运行时）。
cmake_minimum_required(VERSION 3.15)
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
# 本机 Drogon 只以 Release 编译安装（_install 下仅 drogon.lib，/MD 运行时），
# 因此即使 VS 中选择 Debug 配置，也统一使用 /MD 运行时（同时使
# _ITERATOR_DEBUG_LEVEL=0），否则链接时报 LNK2038 运行时不匹配。
if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
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
    # Drogon 与 Trantor 从源码构建安装于 _install，固化路径避免重新配置时找不到。
    set(Drogon_DIR "D:/github/drogon-master/_install/lib/cmake/Drogon" CACHE PATH
        "CMake config directory of Drogon")
    set(Trantor_DIR "D:/github/drogon-master/_install/lib/cmake/Trantor" CACHE PATH
        "CMake config directory of Trantor")
    set(HIREDIS_ROOT_DIR "D:/github/hiredis-1.5.0/_install" CACHE PATH
        "Installation prefix of the hiredis used by Drogon")
    set(HIREDIS_INCLUDE_DIR "${HIREDIS_ROOT_DIR}/include" CACHE PATH
        "hiredis header directory" FORCE)
    set(HIREDIS_LIBRARY "${HIREDIS_ROOT_DIR}/lib/hiredis.lib" CACHE FILEPATH
        "x64 hiredis static library" FORCE)
    # Drogon/Trantor 依赖 jsoncpp、c-ares、zlib，本机均通过 vcpkg 提供。
    # jsoncpp 必须走 CONFIG 模式（vcpkg 导出的 jsoncpp_lib target 名字与
    # Drogon 导出的接口一致，才能传播 include 目录），所以把 vcpkg
    # installed 加入 CMAKE_PREFIX_PATH；c-ares 与 zlib 则直接固化路径。
    set(VCPKG_INSTALLED "D:/github/drogon-master/vcpkg/installed/x64-windows" CACHE PATH
        "vcpkg x64-windows installation root")
    set(CMAKE_PREFIX_PATH "${VCPKG_INSTALLED};${CMAKE_PREFIX_PATH}" CACHE PATH
        "Prefix path including vcpkg installed packages" FORCE)
    set(C-ARES_INCLUDE_DIRS "${VCPKG_INSTALLED}/include" CACHE PATH
        "c-ares header directory from vcpkg" FORCE)
    set(C-ARES_LIBRARIES "${VCPKG_INSTALLED}/lib/cares.lib" CACHE FILEPATH
        "x64 c-ares static library" FORCE)
    set(ZLIB_INCLUDE_DIR "${VCPKG_INSTALLED}/include" CACHE PATH
        "zlib header directory from vcpkg" FORCE)
    set(ZLIB_LIBRARY "${VCPKG_INSTALLED}/lib/z.lib" CACHE FILEPATH
        "x64 zlib static library" FORCE)
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
    # 运行时需要 libmysql.dll，构建完成后自动复制到 exe 目录，
    # 避免启动时报"找不到 libmysql.dll"。
    get_filename_component(MYSQL_LIB_DIR "${MYSQL_LIBRARY}" DIRECTORY)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MYSQL_LIB_DIR}/libmysql.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    # Drogon/Trantor 通过 vcpkg 动态链接 jsoncpp/c-ares/zlib，
    # 同样把对应 DLL 复制到 exe 目录，避免运行时报"找不到 xxx.dll"。
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED}/bin/jsoncpp.dll"
            "${VCPKG_INSTALLED}/bin/cares.dll"
            "${VCPKG_INSTALLED}/bin/z.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
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
