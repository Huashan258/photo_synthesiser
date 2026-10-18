cmake_minimum_required(VERSION 3.24)
project(photo_synthesiser VERSION 1.1.1 LANGUAGES CXX)

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

# ---------- Slint（现代化 UI）----------
# E:\slint 下的 Slint C++ SDK 解压目录（可用 -DSLINT_SDK=... 覆盖）
if(NOT DEFINED SLINT_SDK)
    set(SLINT_SDK "E:/slint/Slint-cpp 1.18.1")
endif()
list(APPEND CMAKE_PREFIX_PATH "${SLINT_SDK}/lib/cmake/Slint")
find_package(Slint REQUIRED)
message(STATUS "Slint version: ${Slint_VERSION}")

add_executable(photo_synthesiser main.cpp)

# 把 .slint 编译成 C++ 头文件（生成物在构建目录，自动加入 include 路径）
slint_target_sources(photo_synthesiser ui/app-window.slint)
target_link_libraries(photo_synthesiser PRIVATE Slint::Slint)

# 运行时需要 slint_cpp.dll 与 exe 同目录
add_custom_command(TARGET photo_synthesiser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SLINT_SDK}/lib/slint_cpp.dll"
        "$<TARGET_FILE_DIR:photo_synthesiser>"
)

# 版本号（CMakeLists 里的 project VERSION 是唯一来源，改一处即可）
target_compile_definitions(photo_synthesiser PRIVATE "APP_VERSION=L\"${PROJECT_VERSION}\"")

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
