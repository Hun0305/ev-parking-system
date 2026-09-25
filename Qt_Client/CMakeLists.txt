# Windows(MinGW) 와 Linux/WSL 양쪽에서 빌드된다. 차이는 FFmpeg 를 찾는 방식뿐이다.

cmake_minimum_required(VERSION 3.14)

project(smart_parking_qt_client VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Quick QuickWidgets Qml Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Quick QuickWidgets Qml Network)

# Qt Mqtt 애드온 모듈은 팀 계정 저장소에 없어서 설치가 불가능했다. 대신
# MqttServiceClient 가 QtNetwork(QTcpSocket) 위에 MQTT 3.1.1 을 직접 구현한다
# (src/services/mqttserviceclient.cpp) — 추가 모듈 설치 없이 항상 빌드된다.

set(DEFAULT_FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg")

if(DEFINED ENV{FFMPEG_ROOT})
    set(DEFAULT_FFMPEG_ROOT "$ENV{FFMPEG_ROOT}")
endif()

set(FFMPEG_ROOT "${DEFAULT_FFMPEG_ROOT}" CACHE PATH "FFmpeg shared build root")
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIBRARY_DIR "${FFMPEG_ROOT}/lib")
set(FFMPEG_BINARY_DIR "${FFMPEG_ROOT}/bin")

find_library(FFMPEG_AVFORMAT_LIBRARY NAMES avformat libavformat PATHS "${FFMPEG_LIBRARY_DIR}" NO_DEFAULT_PATH)
find_library(FFMPEG_AVCODEC_LIBRARY NAMES avcodec libavcodec PATHS "${FFMPEG_LIBRARY_DIR}" NO_DEFAULT_PATH)
find_library(FFMPEG_AVUTIL_LIBRARY NAMES avutil libavutil PATHS "${FFMPEG_LIBRARY_DIR}" NO_DEFAULT_PATH)
find_library(FFMPEG_SWSCALE_LIBRARY NAMES swscale libswscale PATHS "${FFMPEG_LIBRARY_DIR}" NO_DEFAULT_PATH)

# FFmpeg 는 두 가지 방식으로 들어온다.
#   1) Windows: 팀 공용 shared build 를 third_party/ffmpeg 에 풀어둔 self-contained 트리
#   2) Linux: 배포판 개발 패키지. 멀티아치 경로라 include/ + lib/ 짝이 안 맞으므로 pkg-config 로 찾는다
add_library(smart_parking_ffmpeg INTERFACE)

if(FFMPEG_AVFORMAT_LIBRARY AND FFMPEG_AVCODEC_LIBRARY
        AND FFMPEG_AVUTIL_LIBRARY AND FFMPEG_SWSCALE_LIBRARY)
    message(STATUS "FFmpeg found in FFMPEG_ROOT=${FFMPEG_ROOT}")
    target_include_directories(smart_parking_ffmpeg INTERFACE "${FFMPEG_INCLUDE_DIR}")
    target_link_libraries(smart_parking_ffmpeg
        INTERFACE
            "${FFMPEG_AVFORMAT_LIBRARY}"
            "${FFMPEG_AVCODEC_LIBRARY}"
            "${FFMPEG_AVUTIL_LIBRARY}"
            "${FFMPEG_SWSCALE_LIBRARY}")
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG_PC IMPORTED_TARGET
            libavformat libavcodec libavutil libswscale)
    endif()

    if(FFMPEG_PC_FOUND)
        message(STATUS "FFmpeg found via pkg-config (libavformat ${FFMPEG_PC_libavformat_VERSION})")
        target_link_libraries(smart_parking_ffmpeg INTERFACE PkgConfig::FFMPEG_PC)
    else()
        message(FATAL_ERROR
            "FFmpeg not found.\n"
            "  Windows: extract the shared build so that ${FFMPEG_ROOT}/include and "
            "${FFMPEG_ROOT}/lib exist, or pass -DFFMPEG_ROOT=<path>.\n"
            "  Linux:   sudo apt install libavformat-dev libavcodec-dev "
            "libavutil-dev libswscale-dev pkg-config")
    endif()
endif()

add_executable(smart_parking_qt_client
    src/main.cpp
    src/adapters/incomingmessageadapter.cpp
    src/adapters/incomingmessageadapter.h
    src/adapters/serverfireeventadapter.cpp
    src/adapters/serverfireeventadapter.h
    src/adapters/serverparkingeventadapter.cpp
    src/adapters/serverparkingeventadapter.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/controllers/parkingcontroller.cpp
    src/controllers/parkingcontroller.h
    src/diagnostics/diagnostictypes.h
    src/diagnostics/diagnosticsservice.cpp
    src/diagnostics/diagnosticsservice.h
    src/dialogs/slotevidencedialog.cpp
    src/dialogs/slotevidencedialog.h
    src/dialogs/firealarmpopup.cpp
    src/dialogs/firealarmpopup.h
    src/models/monitoringevent.cpp
    src/models/monitoringevent.h
    src/models/parkingcapturegroup.cpp
    src/models/parkingcapturegroup.h
    src/models/parkingstate.cpp
    src/models/parkingstate.h
    src/models/parkingzonelayout.cpp
    src/models/parkingzonelayout.h
    src/pages/dashboardpage.cpp
    src/pages/dashboardpage.h
    src/pages/debugpage.cpp
    src/pages/debugpage.h
    src/pages/evidencepage.cpp
    src/pages/evidencepage.h
    src/pages/eventspage.cpp
    src/pages/eventspage.h
    src/pages/imagecomparepage.cpp
    src/pages/imagecomparepage.h
    src/pages/parkingmappage.cpp
    src/pages/parkingmappage.h
    src/pages/settingspage.cpp
    src/pages/settingspage.h
    src/services/camerasettings.cpp
    src/services/camerasettings.h
    src/services/notificationcenter.cpp
    src/services/notificationcenter.h
    src/services/notificationpolicy.cpp
    src/services/notificationpolicy.h
    src/services/mqttserviceclient.cpp
    src/services/mqttserviceclient.h
    src/simulation/parkingmockdata.cpp
    src/simulation/parkingmockdata.h
    src/simulation/parkingsimulationservice.cpp
    src/simulation/parkingsimulationservice.h
    src/api/apiclient.cpp
    src/api/apiclient.h
    src/api/imageloader.cpp
    src/api/imageloader.h
    src/api/parkingmodels.h
    src/api/parkingresponseparser.cpp
    src/api/parkingresponseparser.h
    rtsp_legacy/RtspVideoItem.cpp
    rtsp_legacy/RtspVideoItem.h
)

target_link_libraries(smart_parking_qt_client
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Quick
        Qt${QT_VERSION_MAJOR}::QuickWidgets
        Qt${QT_VERSION_MAJOR}::Qml
        Qt${QT_VERSION_MAJOR}::Network
        smart_parking_ffmpeg
)

target_include_directories(smart_parking_qt_client
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/rtsp_legacy"
)

target_compile_definitions(smart_parking_qt_client
    PRIVATE SMART_PARKING_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config"
)

if (QT_VERSION_MAJOR EQUAL 6)
    qt_add_resources(smart_parking_qt_client rtsp_qml
        PREFIX "/"
        FILES
            qml/RtspChannel.qml
    )
else()
    qt5_add_resources(RTSP_QML_RESOURCES qml/rtsp_qml.qrc)
    target_sources(smart_parking_qt_client PRIVATE ${RTSP_QML_RESOURCES})
endif()

if (EXISTS "${FFMPEG_BINARY_DIR}")
    add_custom_command(TARGET smart_parking_qt_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${FFMPEG_BINARY_DIR}"
            "$<TARGET_FILE_DIR:smart_parking_qt_client>"
    )
endif()

set_target_properties(smart_parking_qt_client PROPERTIES
    WIN32_EXECUTABLE TRUE
)

include(CTest)
if (BUILD_TESTING)
    add_executable(parkingcontroller_retry_test
        tests/parkingcontroller_retry_test.cpp
        src/adapters/incomingmessageadapter.cpp
        src/adapters/incomingmessageadapter.h
        src/adapters/serverfireeventadapter.cpp
        src/adapters/serverfireeventadapter.h
        src/adapters/serverparkingeventadapter.cpp
        src/adapters/serverparkingeventadapter.h
        src/controllers/parkingcontroller.cpp
        src/controllers/parkingcontroller.h
        src/models/monitoringevent.cpp
        src/models/monitoringevent.h
        src/models/parkingstate.cpp
        src/models/parkingstate.h
        src/api/apiclient.cpp
        src/api/apiclient.h
        src/api/imageloader.cpp
        src/api/imageloader.h
        src/api/parkingmodels.h
        src/api/parkingresponseparser.cpp
        src/api/parkingresponseparser.h
        src/services/mqttserviceclient.cpp
        src/services/mqttserviceclient.h
    )
    target_include_directories(parkingcontroller_retry_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingcontroller_retry_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
    )
    add_test(NAME parkingcontroller_retry COMMAND parkingcontroller_retry_test)

    add_executable(mqttserviceclient_test
        tests/mqttserviceclient_test.cpp
        src/services/mqttserviceclient.cpp
        src/services/mqttserviceclient.h
    )
    target_include_directories(mqttserviceclient_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(mqttserviceclient_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Network
    )
    set_target_properties(mqttserviceclient_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME mqtt_service_client COMMAND mqttserviceclient_test success)
    add_test(NAME mqtt_service_client_rejected
        COMMAND mqttserviceclient_test rejected)
    add_test(NAME mqtt_settings_topics
        COMMAND mqttserviceclient_test settings)

    add_executable(parkingsimulationservice_test
        tests/parkingsimulationservice_test.cpp
        src/adapters/incomingmessageadapter.cpp
        src/adapters/incomingmessageadapter.h
        src/adapters/serverfireeventadapter.cpp
        src/adapters/serverfireeventadapter.h
        src/adapters/serverparkingeventadapter.cpp
        src/adapters/serverparkingeventadapter.h
        src/controllers/parkingcontroller.cpp
        src/controllers/parkingcontroller.h
        src/models/monitoringevent.cpp
        src/models/monitoringevent.h
        src/models/parkingstate.cpp
        src/models/parkingstate.h
        src/simulation/parkingmockdata.cpp
        src/simulation/parkingmockdata.h
        src/simulation/parkingsimulationservice.cpp
        src/simulation/parkingsimulationservice.h
        src/api/apiclient.cpp
        src/api/apiclient.h
        src/api/imageloader.cpp
        src/api/imageloader.h
        src/api/parkingmodels.h
        src/api/parkingresponseparser.cpp
        src/api/parkingresponseparser.h
        src/services/mqttserviceclient.cpp
        src/services/mqttserviceclient.h
    )
    target_include_directories(parkingsimulationservice_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingsimulationservice_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
    )
    set_target_properties(parkingsimulationservice_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME parking_simulation_service COMMAND parkingsimulationservice_test)

    add_executable(parkingeventflow_test
        tests/parkingeventflow_test.cpp
        src/adapters/incomingmessageadapter.cpp
        src/adapters/incomingmessageadapter.h
        src/adapters/serverfireeventadapter.cpp
        src/adapters/serverfireeventadapter.h
        src/adapters/serverparkingeventadapter.cpp
        src/adapters/serverparkingeventadapter.h
        src/controllers/parkingcontroller.cpp
        src/controllers/parkingcontroller.h
        src/models/monitoringevent.cpp
        src/models/monitoringevent.h
        src/models/parkingstate.cpp
        src/models/parkingstate.h
        src/services/notificationcenter.cpp
        src/services/notificationcenter.h
        src/services/notificationpolicy.cpp
        src/services/notificationpolicy.h
        src/services/mqttserviceclient.cpp
        src/services/mqttserviceclient.h
        src/api/apiclient.cpp
        src/api/apiclient.h
        src/api/imageloader.cpp
        src/api/imageloader.h
        src/api/parkingmodels.h
        src/api/parkingresponseparser.cpp
        src/api/parkingresponseparser.h
    )
    target_include_directories(parkingeventflow_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingeventflow_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
    )
    set_target_properties(parkingeventflow_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME parking_event_flow COMMAND parkingeventflow_test)

    add_executable(notificationcenter_test
        tests/notificationcenter_test.cpp
        src/models/monitoringevent.cpp
        src/models/monitoringevent.h
        src/services/notificationcenter.cpp
        src/services/notificationcenter.h
        src/services/notificationpolicy.cpp
        src/services/notificationpolicy.h
    )
    target_include_directories(notificationcenter_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(notificationcenter_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
    )
    set_target_properties(notificationcenter_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME notification_center COMMAND notificationcenter_test)

    add_executable(firealarmpopup_test
        tests/firealarmpopup_test.cpp
        src/dialogs/firealarmpopup.cpp
        src/dialogs/firealarmpopup.h
    )
    target_include_directories(firealarmpopup_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(firealarmpopup_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    set_target_properties(firealarmpopup_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME fire_alarm_popup COMMAND firealarmpopup_test)

    add_executable(incomingmessageadapter_test
        tests/incomingmessageadapter_test.cpp
        src/adapters/incomingmessageadapter.cpp
        src/adapters/incomingmessageadapter.h
        src/models/parkingstate.cpp
        src/models/parkingstate.h
        src/api/parkingmodels.h
    )
    target_include_directories(incomingmessageadapter_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(incomingmessageadapter_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
    )
    set_target_properties(incomingmessageadapter_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME incoming_message_adapter COMMAND incomingmessageadapter_test)

    add_executable(diagnosticsservice_test
        tests/diagnosticsservice_test.cpp
        src/diagnostics/diagnostictypes.h
        src/diagnostics/diagnosticsservice.cpp
        src/diagnostics/diagnosticsservice.h
        src/models/monitoringevent.cpp
        src/models/monitoringevent.h
    )
    target_include_directories(diagnosticsservice_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(diagnosticsservice_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
    )
    set_target_properties(diagnosticsservice_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME diagnostics_service COMMAND diagnosticsservice_test)

    add_executable(camerasettings_test
        tests/camerasettings_test.cpp
        src/services/camerasettings.cpp
        src/services/camerasettings.h
    )
    target_include_directories(camerasettings_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(camerasettings_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
    )
    set_target_properties(camerasettings_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME camera_settings COMMAND camerasettings_test)

    add_executable(parkingresponseparser_evidence_test
        tests/parkingresponseparser_evidence_test.cpp
        src/api/parkingmodels.h
        src/api/parkingresponseparser.cpp
        src/api/parkingresponseparser.h
    )
    target_include_directories(parkingresponseparser_evidence_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingresponseparser_evidence_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
    )
    set_target_properties(parkingresponseparser_evidence_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME parkingresponseparser_evidence COMMAND parkingresponseparser_evidence_test)

    add_executable(evidencepage_test
        tests/evidencepage_test.cpp
        src/pages/evidencepage.cpp
        src/pages/evidencepage.h
        src/pages/eventspage.cpp
        src/pages/eventspage.h
        src/models/monitoringevent.cpp
        src/models/monitoringevent.h
        src/models/parkingcapturegroup.cpp
        src/models/parkingcapturegroup.h
        src/models/parkingstate.cpp
        src/models/parkingstate.h
        src/api/imageloader.cpp
        src/api/imageloader.h
        src/api/parkingmodels.h
    )
    target_include_directories(evidencepage_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(evidencepage_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
    )
    set_target_properties(evidencepage_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME evidencepage COMMAND evidencepage_test)

    add_executable(imagecomparepage_test
        tests/imagecomparepage_test.cpp
        src/pages/imagecomparepage.cpp
        src/pages/imagecomparepage.h
        src/models/parkingcapturegroup.cpp
        src/models/parkingcapturegroup.h
        src/models/parkingstate.cpp
        src/models/parkingstate.h
        src/api/imageloader.cpp
        src/api/imageloader.h
        src/api/parkingmodels.h
    )
    target_include_directories(imagecomparepage_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(imagecomparepage_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
    )
    set_target_properties(imagecomparepage_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME imagecomparepage COMMAND imagecomparepage_test)

    add_executable(eventspage_filter_test
        tests/eventspage_filter_test.cpp
        src/pages/eventspage.cpp
        src/pages/eventspage.h
        src/models/monitoringevent.cpp
        src/models/monitoringevent.h
    )
    target_include_directories(eventspage_filter_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(eventspage_filter_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    set_target_properties(eventspage_filter_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME eventspage_filter COMMAND eventspage_filter_test)

    add_executable(parkingmappage_add_slot_test
        tests/parkingmappage_add_slot_test.cpp
        src/pages/parkingmappage.cpp
        src/pages/parkingmappage.h
        src/models/parkingstate.cpp
        src/models/parkingstate.h
        src/models/parkingzonelayout.cpp
        src/models/parkingzonelayout.h
    )
    target_include_directories(parkingmappage_add_slot_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingmappage_add_slot_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    set_target_properties(parkingmappage_add_slot_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME parkingmappage_add_slot COMMAND parkingmappage_add_slot_test)

    add_executable(parkingmappage_channel_names_test
        tests/parkingmappage_channel_names_test.cpp
        src/pages/parkingmappage.cpp
        src/pages/parkingmappage.h
        src/models/parkingstate.cpp
        src/models/parkingstate.h
        src/models/parkingzonelayout.cpp
        src/models/parkingzonelayout.h
    )
    target_include_directories(parkingmappage_channel_names_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingmappage_channel_names_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    set_target_properties(parkingmappage_channel_names_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME parkingmappage_channel_names COMMAND parkingmappage_channel_names_test)

    add_executable(parkingmappage_undo_test
        tests/parkingmappage_undo_test.cpp
        src/pages/parkingmappage.cpp
        src/pages/parkingmappage.h
        src/models/parkingstate.cpp
        src/models/parkingstate.h
        src/models/parkingzonelayout.cpp
        src/models/parkingzonelayout.h
    )
    target_include_directories(parkingmappage_undo_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingmappage_undo_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    set_target_properties(parkingmappage_undo_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME parkingmappage_undo COMMAND parkingmappage_undo_test)

    add_executable(parkingmappage_ev_status_test
        tests/parkingmappage_ev_status_test.cpp
        src/pages/parkingmappage.cpp
        src/pages/parkingmappage.h
        src/models/parkingstate.cpp
        src/models/parkingstate.h
        src/models/parkingzonelayout.cpp
        src/models/parkingzonelayout.h
    )
    target_include_directories(parkingmappage_ev_status_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingmappage_ev_status_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    set_target_properties(parkingmappage_ev_status_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME parkingmappage_ev_status COMMAND parkingmappage_ev_status_test)
    set(QT_GUI_DIR_VARIABLE "Qt${QT_VERSION_MAJOR}Gui_DIR")
    get_filename_component(QT_RUNTIME_ROOT
        "${${QT_GUI_DIR_VARIABLE}}/../../.." ABSOLUTE)
    set_tests_properties(parkingmappage_add_slot PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_RUNTIME_ROOT}/plugins/platforms"
        TIMEOUT 30
    )
    set_tests_properties(parkingmappage_channel_names PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_RUNTIME_ROOT}/plugins/platforms"
        TIMEOUT 30
    )
    set_tests_properties(parkingmappage_undo PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_RUNTIME_ROOT}/plugins/platforms"
        TIMEOUT 30
    )
    set_tests_properties(parkingmappage_ev_status PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_RUNTIME_ROOT}/plugins/platforms"
        TIMEOUT 30
    )
    set_tests_properties(evidencepage PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_RUNTIME_ROOT}/plugins/platforms"
        TIMEOUT 30
    )
    set_tests_properties(imagecomparepage PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_RUNTIME_ROOT}/plugins/platforms"
        TIMEOUT 30
    )
endif()
