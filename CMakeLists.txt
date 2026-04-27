cmake_minimum_required(VERSION 3.16)

project(cs VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Sql Concurrent LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Sql Concurrent LinguistTools)

set(TS_FILES cs_zh_CN.ts)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        appconfig.cpp
        appconfig.h
        applogger.cpp
        applogger.h
        startupservice.cpp
        startupservice.h
        rabbitmqservice.cpp
        rabbitmqservice.h
        gatewayapiclient.cpp
        gatewayapiclient.h
        filemonitorservice.cpp
        filemonitorservice.h
        rabbitmqdispatcher.cpp
        rabbitmqdispatcher.h
        installscriptprocessor.cpp
        installscriptprocessor.h
        rechargeprocessor.cpp
        rechargeprocessor.h
        qrcodeencoder.cpp
        qrcodeencoder.h
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
        partitionmanagepage.cpp
        partitionmanagepage.h
        partitionmanagepage.ui
        partitiontemplatepage.cpp
        partitiontemplatepage.h
        partitiontemplatepage.ui
        groupmanagepage.cpp
        groupmanagepage.h
        groupmanagepage.ui
        scriptpage.cpp
        scriptpage.h
        scriptpage.ui
        settingspage.cpp
        settingspage.h
        settingspage.ui
        changegatewayiddialog.cpp
        changegatewayiddialog.h
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

if(CMAKE_GENERATOR MATCHES "Ninja")
    set_property(GLOBAL PROPERTY JOB_POOLS compile_pool=1)
    set_property(TARGET cs PROPERTY JOB_POOL_COMPILE compile_pool)
endif()

if(MSVC AND WIN32)
    set(MSVC_LIB_X64_DIR "$ENV{VCToolsInstallDir}/lib/x64")
    set(WINSDK_UM_X64_DIR "$ENV{WindowsSdkDir}/lib/$ENV{WindowsSDKVersion}/um/x64")
    set(WINSDK_UCRT_X64_DIR "$ENV{UniversalCRTSdkDir}/lib/$ENV{UCRTVersion}/ucrt/x64")
    target_link_directories(cs PRIVATE
        "${MSVC_LIB_X64_DIR}"
        "${WINSDK_UM_X64_DIR}"
        "${WINSDK_UCRT_X64_DIR}"
    )
endif()

target_link_libraries(cs PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Concurrent bcrypt wbemuuid wtsapi32 ole32 oleaut32 uuid)

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

include(GNUInstallDirs)
install(TARGETS cs
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(cs)
endif()
