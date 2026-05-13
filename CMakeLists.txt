cmake_minimum_required(VERSION 3.16)
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(cs VERSION 0.1 LANGUAGES CXX)

# Qt5 静态库若用 configure -static-runtime 构建，则为 /MT；应用侧须一致，否则 LNK2038
option(GATEWAY_QT5_MT_RUNTIME "Qt5: link with /MT (match Qt built with -static-runtime)" OFF)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Sql Concurrent LinguistTools Svg)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Sql Concurrent LinguistTools Svg)

set(TS_FILES cs_zh_CN.ts)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        appconfig.cpp
        appconfig.h
        appversion.cpp
        appversion.h
        applogger.cpp
        applogger.h
        startupservice.cpp
        startupservice.h
        startupsplash.cpp
        startupsplash.h
        gatewayhttpserver.cpp
        gatewayhttpserver.h
        gatewaysingleinstance.cpp
        gatewaysingleinstance.h
        rabbitmqservice.cpp
        rabbitmqservice.h
        gatewayapiclient.cpp
        gatewayapiclient.h
        gatewayupdatecontroller.cpp
        gatewayupdatecontroller.h
        gatewaypresencecontroller.cpp
        gatewaypresencecontroller.h
        filemonitorservice.cpp
        filemonitorservice.h
        rabbitmqdispatcher.cpp
        rabbitmqdispatcher.h
        installscriptprocessor.cpp
        installscriptprocessor.h
        rechargeprocessor.cpp
        rechargeprocessor.h
        qrcodeencoder.cpp
        qrcodeencoder_matrix_build.cpp
        qrcodeencoder_matrix_build.h
        qrcodeencoder.h
        checkboxstyle.h
        checkbox_resources.qrc
        radiostyle.h
        radio_resources.qrc
        legacycryptoutil.cpp
        legacycryptoutil.h
        machinecode.cpp
        machinecode.h
        logpage.cpp
        logpage.h
        logpage.ui
        orderpage.cpp
        orderpage.h
        orderpage.ui
        partitionpage.cpp
        partitionpage.h
        partitionpage.ui
        partitiondialog.cpp
        partitiondialog.h
        partitionmanagepage.cpp
        partitionmanagepage.h
        partitionmanagepage.ui
        partitionpathcache.cpp
        partitionpathcache.h
        partitiontemplatepage.cpp
        partitiontemplatepage.h
        partitiontemplatepage.ui
        templatedialog.cpp
        templatedialog.h
        groupmanagepage.cpp
        groupmanagepage.h
        groupmanagepage.ui
        scriptpage.cpp
        scriptpage.h
        scriptpage.ui
        settingspage.cpp
        settingspage.h
        settingspage.ui
        pageutils.h
        ${TS_FILES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(cs
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET cs APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    if(ANDROID)
        add_library(cs SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(cs
            ${PROJECT_SOURCES}
        )
    endif()

    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

# 导航栏图标嵌入 exe：按源码 win/ 下实际存在的文件生成 .qrc（前缀 :/nav/），换图后需重新配置/编译
set(GATEWAY_NAV_ICONS_QRC "${CMAKE_CURRENT_BINARY_DIR}/nav_icons_embed.qrc")
set(_nav_embed_lines "<RCC>\n  <qresource prefix=\"/nav\">\n")
set(_nav_embed_names nav_system_log nav_partition nav_order nav_script nav_settings 1 2 3 4 5)
foreach(_n ${_nav_embed_names})
    foreach(_ext IN ITEMS png ico)
        set(_embed_f "${CMAKE_CURRENT_SOURCE_DIR}/win/${_n}.${_ext}")
        if(EXISTS "${_embed_f}")
            file(TO_CMAKE_PATH "${_embed_f}" _embed_f_norm)
            string(REPLACE "\\" "/" _embed_f_slash "${_embed_f_norm}")
            string(APPEND _nav_embed_lines "    <file alias=\"${_n}.${_ext}\">${_embed_f_slash}</file>\n")
        endif()
    endforeach()
endforeach()
string(APPEND _nav_embed_lines "  </qresource>\n</RCC>\n")
file(WRITE "${GATEWAY_NAV_ICONS_QRC}" "${_nav_embed_lines}")
target_sources(cs PRIVATE "${GATEWAY_NAV_ICONS_QRC}")

if(WIN32)
    add_subdirectory(gateway_update_helper)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/tools_update_embed.qrc"
"<RCC>\n  <qresource prefix=\"/tools\">\n    <file alias=\"UPDATE.exe\">UPDATE_for_embed.exe</file>\n  </qresource>\n</RCC>\n")
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/UPDATE_for_embed.exe"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:gateway_update_helper> "${CMAKE_CURRENT_BINARY_DIR}/UPDATE_for_embed.exe"
        DEPENDS gateway_update_helper
        COMMENT "Stage UPDATE.exe for embedding in gateway binary"
    )
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/tools_update_embed.qrc"
        PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/UPDATE_for_embed.exe")
    target_sources(cs PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/tools_update_embed.qrc")
    message(STATUS "Gateway: embedding UPDATE.exe (online update helper)")
    add_dependencies(cs gateway_update_helper)
    # Qt6 静态：对子目录中的 GUI 可执行目标做 finalize，自动链入 Windows 平台插件等（与主程序一致）。
    if(QT_VERSION_MAJOR EQUAL 6 AND COMMAND qt_finalize_executable)
        qt_finalize_executable(gateway_update_helper)
    endif()
endif()

# 在线更新 zip：嵌入 7-Zip 控制台工具（可选）。优先 7za（含 zip）；仅 7zr 时精简版常不含 zip 格式。
# Qt5 静态再链 libzip/zlib 易与 Qt 内置 zlib 冲突，故用子进程解压。
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/7za.exe")
    target_sources(cs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools/tools_embed_7za.qrc")
    message(STATUS "Gateway: embedding tools/7za.exe for zip extract (online update)")
elseif(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/7zr.exe")
    target_sources(cs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools/tools_embed_7zr.qrc")
    message(STATUS "Gateway: embedding tools/7zr.exe (minimal 7zr may lack ZIP — prefer tools/7za.exe from 7-Zip Extra)")
endif()

if(CMAKE_GENERATOR MATCHES "Ninja")
    set_property(GLOBAL PROPERTY JOB_POOLS compile_pool=1)
    set_property(TARGET cs PROPERTY JOB_POOL_COMPILE compile_pool)
endif()

if(MSVC AND WIN32)
    # 源码含中文注释/字符串：与 MSVC 默认代码页一致，避免 C4819 及后续级联解析错误
    target_compile_options(cs PRIVATE /utf-8)
    set(MSVC_LIB_X64_DIR "$ENV{VCToolsInstallDir}/lib/x64")
    set(WINSDK_UM_X64_DIR "$ENV{WindowsSdkDir}/lib/$ENV{WindowsSDKVersion}/um/x64")
    set(WINSDK_UCRT_X64_DIR "$ENV{UniversalCRTSdkDir}/lib/$ENV{UCRTVersion}/ucrt/x64")
    target_link_directories(cs PRIVATE
        "${MSVC_LIB_X64_DIR}"
        "${WINSDK_UM_X64_DIR}"
        "${WINSDK_UCRT_X64_DIR}"
    )
endif()

target_link_libraries(cs PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Svg bcrypt wbemuuid wtsapi32 ole32 oleaut32 uuid)

if(MSVC AND WIN32 AND QT_VERSION_MAJOR EQUAL 5 AND GATEWAY_QT5_MT_RUNTIME)
    set_property(TARGET cs PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# 本地校验：与老网关 QRCoder 路径生成的 Mir 文本逐字节对比（tools/qr_mir_parity/VerifyQrMirParity.ps1）
if(NOT ANDROID)
    add_executable(qr_mir_dump
        tools/qr_mir_parity/dump_main.cpp
        qrcodeencoder.cpp
        qrcodeencoder_matrix_build.cpp
    )
    target_include_directories(qr_mir_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(qr_mir_dump PRIVATE Qt${QT_VERSION_MAJOR}::Core)
    if(MSVC AND WIN32)
        target_compile_options(qr_mir_dump PRIVATE /utf-8)
    endif()
    if(MSVC AND WIN32 AND QT_VERSION_MAJOR EQUAL 5 AND GATEWAY_QT5_MT_RUNTIME)
        set_property(TARGET qr_mir_dump PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# Windows：资源管理器中主程序（支付网关.exe）的图标需嵌入 PE（与运行时从目录加载的 QIcon 无关）
# 优先使用源码目录下的 1.ico，其次 2.ico（同目录或 win/ 子目录）
if(WIN32)
    set(_gateway_exe_icon "")
    foreach(_cand IN ITEMS
            "${CMAKE_CURRENT_SOURCE_DIR}/1.ico"
            "${CMAKE_CURRENT_SOURCE_DIR}/2.ico"
            "${CMAKE_CURRENT_SOURCE_DIR}/win/1.ico"
            "${CMAKE_CURRENT_SOURCE_DIR}/win/2.ico")
        if(EXISTS "${_cand}")
            set(_gateway_exe_icon "${_cand}")
            break()
        endif()
    endforeach()
    if(_gateway_exe_icon)
        string(REPLACE "\\" "/" _gateway_exe_icon_rc "${_gateway_exe_icon}")
        set(_gateway_exe_icon_rc_file "${CMAKE_CURRENT_BINARY_DIR}/cs_exe_icon.rc")
        file(WRITE "${_gateway_exe_icon_rc_file}" "IDI_ICON1 ICON \"${_gateway_exe_icon_rc}\"\n")
        target_sources(cs PRIVATE "${_gateway_exe_icon_rc_file}")
    endif()
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.cs)
endif()
set_target_properties(cs PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
if(WIN32)
    # 资源管理器与在线更新替换后的主程序名（CMake 目标名仍为 cs）
    set_property(TARGET cs PROPERTY OUTPUT_NAME "支付网关")
endif()

include(GNUInstallDirs)
install(TARGETS cs
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(cs)
endif()
