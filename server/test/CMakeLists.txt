cmake_minimum_required(VERSION 3.15)
project(myapp_test CXX)

add_executable(${PROJECT_NAME} test_main.cc)

# 与主工程一致：Drogon 为 Release 构建（/MD 运行时），Debug 配置也使用
# /MD 避免 LNK2038 链接错误；并把 vcpkg 动态依赖的 DLL 复制到 exe 目录。
if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED}/bin/jsoncpp.dll"
            "${VCPKG_INSTALLED}/bin/cares.dll"
            "${VCPKG_INSTALLED}/bin/z.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
endif()

# ##############################################################################
# If you include the drogon source code locally in your project, use this method
# to add drogon 
# target_link_libraries(${PROJECT_NAME} PRIVATE drogon)
#
# and comment out the following lines
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)

ParseAndAddDrogonTests(${PROJECT_NAME})
