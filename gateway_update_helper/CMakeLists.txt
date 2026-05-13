# 在线更新小助手：先于主进程退出显示提示，隐藏运行 gateway_apply_update.bat，结束后自删。
# 输出名 UPDATE.exe，由主工程嵌入 :/tools/UPDATE.exe。
cmake_minimum_required(VERSION 3.16)

if(NOT WIN32)
    return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(gateway_update_helper WIN32
    main.cpp
)

set_target_properties(gateway_update_helper PROPERTIES
    OUTPUT_NAME "UPDATE"
    WIN32_EXECUTABLE TRUE
)

if(MSVC)
    target_compile_options(gateway_update_helper PRIVATE /utf-8)
endif()

if(MSVC AND WIN32)
    # 与官方静态 Qt kit（/MT）一致，避免 LNK2038 / CRT 重复符号
    set_property(TARGET gateway_update_helper PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(gateway_update_helper PRIVATE Qt${QT_VERSION_MAJOR}::Widgets shell32)

# 静态 Qt + Windows：可执行文件须把 QWindowsIntegrationPlugin 链进最终二进制（否则 LNK 未解析符号或运行期无平台插件）。
if(WIN32 AND TARGET Qt5::QWindowsIntegrationPlugin)
    target_link_libraries(gateway_update_helper PRIVATE Qt5::QWindowsIntegrationPlugin)
endif()

if(MSVC AND QT_VERSION_MAJOR EQUAL 5)
    target_link_libraries(gateway_update_helper PRIVATE Qt5::WinMain)
endif()

if(MSVC AND WIN32)
    set(MSVC_LIB_X64_DIR "$ENV{VCToolsInstallDir}/lib/x64")
    set(WINSDK_UM_X64_DIR "$ENV{WindowsSdkDir}/lib/$ENV{WindowsSDKVersion}/um/x64")
    set(WINSDK_UCRT_X64_DIR "$ENV{UniversalCRTSdkDir}/lib/$ENV{UCRTVersion}/ucrt/x64")
    target_link_directories(gateway_update_helper PRIVATE
        "${MSVC_LIB_X64_DIR}"
        "${WINSDK_UM_X64_DIR}"
        "${WINSDK_UCRT_X64_DIR}"
    )
endif()
