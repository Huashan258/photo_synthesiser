cmake_minimum_required(VERSION 3.24)
project(photo_synthesiser)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------- OpenCV ----------
# 预编译包解压位置（可用 -DOpenCV_ROOT=... 覆盖）
if(NOT DEFINED OpenCV_ROOT)
    set(OpenCV_ROOT "E:/opencv/extract5/opencv/build")
endif()
set(OpenCV_DIR "${OpenCV_ROOT}" CACHE PATH "OpenCV build 目录，内含 OpenCVConfig.cmake")

find_package(OpenCV REQUIRED)

message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV libs: ${OpenCV_LIBS}")

add_executable(photo_synthesiser main.cpp)

# 程序图标（Windows 资源，编译进 exe）
if(WIN32)
    target_sources(photo_synthesiser PRIVATE app.rc)
endif()

target_include_directories(photo_synthesiser PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(photo_synthesiser PRIVATE ${OpenCV_LIBS})

# Win32 对话框/COM/通用控件（可视化面板）
if(WIN32)
    target_link_libraries(photo_synthesiser PRIVATE ole32 shell32 comdlg32 comctl32)
    # 使用 Windows 子系统：双击启动不再弹出控制台黑框
    set_target_properties(photo_synthesiser PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# MSVC: 支持中文源码与控制台输出
if(MSVC)
    target_compile_options(photo_synthesiser PRIVATE /utf-8)
    target_compile_definitions(photo_synthesiser PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# 编译后自动把 OpenCV DLL 拷到 exe 旁边（MSVC 预编译包目录结构）
if(MSVC AND EXISTS "${OpenCV_ROOT}/x64/vc16/bin")
    add_custom_command(TARGET photo_synthesiser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OpenCV_ROOT}/x64/vc16/bin"
            "$<TARGET_FILE_DIR:photo_synthesiser>"
    )
endif()
