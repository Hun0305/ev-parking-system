# Windows(MinGW) ?� Linux/WSL ?�쪽?�서 빌드?�다. 차이??FFmpeg �?찾는 방식뿐이??

cmake_minimum_required(VERSION 3.14)

project(smart_parking_qt_client VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Quick QuickWidgets Qml Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Quick QuickWidgets Qml Network)

# Qt Mqtt ?�드??모듈?� ?� 계정 ?�?�소???�어???�치가 불�??�했?? ?�??
# MqttServiceClient 가 QtNetwork(QTcpSocket) ?�에 MQTT 3.1.1 ??직접 구현?�다
# (src/services/mqttserviceclient.cpp) ??추�? 모듈 ?�치 ?�이 ??�� 빌드?�다.

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

# FFmpeg ????가지 방식?�로 ?�어?�다.
#   1) Windows: ?� 공용 shared build �?third_party/ffmpeg ???�?�둔 self-contained ?�리
#   2) Linux: 배포??개발 ?�키지. 멀?�아�?경로??include/ + lib/ 짝이 ??맞으므�?pkg-config �?찾는??
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
    src/auth/authclient.cpp
    src/auth/authclient.h
    src/auth/authsession.h
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
    src/dialogs/logindialog.cpp
    src/dialogs/logindialog.h
    src/models/monitoringevent.cpp
    src/models/monitoringevent.h
    src/models/parkingcapturegroup.cpp
    src/models/parkingcapturegroup.h
    src/models/parkingstate.cpp
    src/models/parkingstate.h
    src/models/slotidmapper.cpp
    src/models/slotidmapper.h
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
    src/pages/ivasettingspage.cpp
    src/pages/ivasettingspage.h
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
    src/widgets/pagehelp.h
    src/simulation/parkingmockdata.cpp
    src/simulation/parkingmockdata.h
    src/simulation/parkingsimulationservice.cpp
    src/simulation/parkingsimulationservice.h
    src/api/apiclient.cpp
    src/api/apiclient.h
    src/api/imageloader.cpp
    src/api/imageloader.h
    src/api/urlorigin.h
    src/api/parkingmodels.h
    src/api/parkingroi.cpp
    src/api/parkingroi.h
    src/api/parkingresponseparser.cpp
    src/api/parkingresponseparser.h
    src/iva/ivaareamodels.h
    src/iva/ivaareacapabilityparser.cpp
    src/iva/ivaareacapabilityparser.h
    src/iva/ivaareaoptionsparser.cpp
    src/iva/ivaareaoptionsparser.h
    src/iva/ivaareaparser.cpp
    src/iva/ivaareaparser.h
    src/iva/ivaareaupdatebuilder.cpp
    src/iva/ivaareaupdatebuilder.h
    src/iva/ivavideocanvas.cpp
    src/iva/ivavideocanvas.h
    src/iva/wiseaiconfigclient.cpp
    src/iva/wiseaiconfigclient.h
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

if (QT_VERSION_MAJOR EQUAL 6)
    qt_add_resources(smart_parking_qt_client nav_icons
        PREFIX "/"
        FILES
            resources/icons/dashboard.svg
            resources/icons/parking-map.svg
            resources/icons/events.svg
            resources/icons/evidence.svg
            resources/icons/settings.svg
            resources/icons/iva-setup.svg
            resources/icons/debug.svg
            resources/icons/monitor-connected.svg
            resources/icons/monitor-connecting.svg
            resources/icons/monitor-disconnected.svg
            resources/icons/iva/detection-enter.svg
            resources/icons/iva/detection-generic.svg
            resources/icons/iva/detection-intrusion.svg
            resources/icons/iva/detection-loitering.svg
            resources/icons/iva/check.svg
            resources/icons/iva/object-bicycle.svg
            resources/icons/iva/object-bus.svg
            resources/icons/iva/object-car.svg
            resources/icons/iva/object-generic.svg
            resources/icons/iva/object-motorcycle.svg
            resources/icons/iva/object-person.svg
            resources/icons/iva/object-truck.svg
    )
else()
    qt5_add_resources(NAV_ICON_RESOURCES resources/navicons.qrc)
    target_sources(smart_parking_qt_client PRIVATE ${NAV_ICON_RESOURCES})
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
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_executable(pagehelp_test
        tests/pagehelp_test.cpp
        src/widgets/pagehelp.h
    )
    target_include_directories(pagehelp_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(pagehelp_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    set_target_properties(pagehelp_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME page_help COMMAND pagehelp_test)

    add_executable(debugpage_help_test
        tests/debugpage_help_test.cpp
        src/pages/debugpage.cpp
        src/pages/debugpage.h
    )
    target_include_directories(debugpage_help_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(debugpage_help_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    set_target_properties(debugpage_help_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME debugpage_help COMMAND debugpage_help_test)

    add_executable(dashboardpage_help_test
        tests/dashboardpage_help_test.cpp
        src/pages/dashboardpage.cpp
        src/pages/dashboardpage.h
        src/models/monitoringevent.cpp
        src/models/monitoringevent.h
        rtsp_legacy/RtspVideoItem.cpp
        rtsp_legacy/RtspVideoItem.h
    )
    target_include_directories(dashboardpage_help_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/rtsp_legacy"
    )
    target_link_libraries(dashboardpage_help_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Quick
        Qt${QT_VERSION_MAJOR}::QuickWidgets
        Qt${QT_VERSION_MAJOR}::Qml
        Qt${QT_VERSION_MAJOR}::Network
        smart_parking_ffmpeg
    )
    set_target_properties(dashboardpage_help_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    if (QT_VERSION_MAJOR EQUAL 6)
        qt_add_resources(dashboardpage_help_test dashboardpage_help_qml
            PREFIX "/"
            FILES
                qml/RtspChannel.qml
        )
    else()
        qt5_add_resources(DASHBOARDPAGE_HELP_QML_RESOURCES qml/rtsp_qml.qrc)
        target_sources(dashboardpage_help_test PRIVATE
            ${DASHBOARDPAGE_HELP_QML_RESOURCES})
    endif()
    add_test(NAME dashboardpage_help COMMAND dashboardpage_help_test)

    add_executable(parkingcontroller_retry_test
        tests/parkingcontroller_retry_test.cpp
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
        src/models/slotidmapper.cpp
        src/models/slotidmapper.h
        src/api/apiclient.cpp
        src/api/apiclient.h
        src/api/imageloader.cpp
        src/api/imageloader.h
        src/api/parkingmodels.h
        src/api/parkingroi.cpp
        src/api/parkingroi.h
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
        src/models/slotidmapper.cpp
        src/models/slotidmapper.h
        src/simulation/parkingmockdata.cpp
        src/simulation/parkingmockdata.h
        src/simulation/parkingsimulationservice.cpp
        src/simulation/parkingsimulationservice.h
        src/api/apiclient.cpp
        src/api/apiclient.h
        src/api/imageloader.cpp
        src/api/imageloader.h
        src/api/parkingmodels.h
        src/api/parkingroi.cpp
        src/api/parkingroi.h
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
        src/models/slotidmapper.cpp
        src/models/slotidmapper.h
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
        src/api/parkingroi.cpp
        src/api/parkingroi.h
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

    add_executable(apiclient_json_test
        tests/apiclient_json_test.cpp
        src/api/apiclient.cpp
        src/api/apiclient.h
    )
    target_include_directories(apiclient_json_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(apiclient_json_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
    )
    set_target_properties(apiclient_json_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME api_client_json COMMAND apiclient_json_test)

    add_executable(authclient_test
        tests/authclient_test.cpp
        src/auth/authclient.cpp
        src/auth/authclient.h
        src/auth/authsession.h
        src/api/urlorigin.h
    )
    target_include_directories(authclient_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(authclient_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::Test
    )
    set_target_properties(authclient_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME auth_client COMMAND authclient_test)

    add_executable(logindialog_test
        tests/logindialog_test.cpp
        src/auth/authclient.cpp
        src/auth/authclient.h
        src/auth/authsession.h
        src/api/urlorigin.h
        src/dialogs/logindialog.cpp
        src/dialogs/logindialog.h
    )
    target_include_directories(logindialog_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(logindialog_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::Test
    )
    set_target_properties(logindialog_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME login_dialog COMMAND logindialog_test)

    add_executable(imageloader_auth_test
        tests/imageloader_auth_test.cpp
        src/api/imageloader.cpp
        src/api/imageloader.h
        src/api/urlorigin.h
    )
    target_include_directories(imageloader_auth_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(imageloader_auth_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
    )
    set_target_properties(imageloader_auth_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME image_loader_auth COMMAND imageloader_auth_test)

    add_executable(settingspage_overstay_test
        tests/settingspage_overstay_test.cpp
        src/pages/settingspage.cpp
        src/pages/settingspage.h
    )
    target_include_directories(settingspage_overstay_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(settingspage_overstay_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    set_target_properties(settingspage_overstay_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME settings_page_overstay COMMAND settingspage_overstay_test)

    add_executable(overstaycontroller_test
        tests/overstaycontroller_test.cpp
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
        src/models/slotidmapper.cpp
        src/models/slotidmapper.h
        src/api/apiclient.cpp
        src/api/apiclient.h
        src/api/imageloader.cpp
        src/api/imageloader.h
        src/api/parkingmodels.h
        src/api/parkingroi.cpp
        src/api/parkingroi.h
        src/api/parkingresponseparser.cpp
        src/api/parkingresponseparser.h
        src/services/mqttserviceclient.cpp
        src/services/mqttserviceclient.h
    )
    target_include_directories(overstaycontroller_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(overstaycontroller_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
    )
    set_target_properties(overstaycontroller_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME overstay_controller COMMAND overstaycontroller_test)

    add_executable(parkingroi_codec_test
        tests/parkingroi_codec_test.cpp
        src/api/parkingroi.cpp
        src/api/parkingroi.h
    )
    target_include_directories(parkingroi_codec_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingroi_codec_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
    )
    set_target_properties(parkingroi_codec_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME parkingroi_codec COMMAND parkingroi_codec_test)

    add_executable(parkingroicontroller_test
        tests/parkingroicontroller_test.cpp
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
        src/models/slotidmapper.cpp
        src/models/slotidmapper.h
        src/api/apiclient.cpp
        src/api/apiclient.h
        src/api/imageloader.cpp
        src/api/imageloader.h
        src/api/parkingmodels.h
        src/api/parkingroi.cpp
        src/api/parkingroi.h
        src/api/parkingresponseparser.cpp
        src/api/parkingresponseparser.h
        src/services/mqttserviceclient.cpp
        src/services/mqttserviceclient.h
    )
    target_include_directories(parkingroicontroller_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingroicontroller_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
    )
    set_target_properties(parkingroicontroller_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME parking_roi_controller COMMAND parkingroicontroller_test)

    add_executable(eventevidencecontroller_test
        tests/eventevidencecontroller_test.cpp
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
        src/models/slotidmapper.cpp
        src/models/slotidmapper.h
        src/api/apiclient.cpp
        src/api/apiclient.h
        src/api/imageloader.cpp
        src/api/imageloader.h
        src/api/parkingmodels.h
        src/api/parkingroi.cpp
        src/api/parkingroi.h
        src/api/parkingresponseparser.cpp
        src/api/parkingresponseparser.h
        src/services/mqttserviceclient.cpp
        src/services/mqttserviceclient.h
    )
    target_include_directories(eventevidencecontroller_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(eventevidencecontroller_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
    )
    set_target_properties(eventevidencecontroller_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME event_evidence_controller COMMAND eventevidencecontroller_test)

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

    add_executable(ivaareaparser_test
        tests/ivaareaparser_test.cpp
        src/iva/ivaareamodels.h
        src/iva/ivaareaparser.cpp
        src/iva/ivaareaparser.h
    )
    target_include_directories(ivaareaparser_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(ivaareaparser_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
    )
    set_target_properties(ivaareaparser_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME ivaarea_parser COMMAND ivaareaparser_test)

    add_executable(ivaareacapabilityparser_test
        tests/ivaareacapabilityparser_test.cpp
        src/iva/ivaareamodels.h
        src/iva/ivaareacapabilityparser.cpp
        src/iva/ivaareacapabilityparser.h
    )
    target_include_directories(ivaareacapabilityparser_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(ivaareacapabilityparser_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
    )
    set_target_properties(ivaareacapabilityparser_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME ivaarea_capability_parser COMMAND ivaareacapabilityparser_test)

    add_executable(ivaareaupdatebuilder_test
        tests/ivaareaupdatebuilder_test.cpp
        src/iva/ivaareamodels.h
        src/iva/ivaareaoptionsparser.cpp
        src/iva/ivaareaoptionsparser.h
        src/iva/ivaareaupdatebuilder.cpp
        src/iva/ivaareaupdatebuilder.h
    )
    target_include_directories(ivaareaupdatebuilder_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(ivaareaupdatebuilder_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
    )
    set_target_properties(ivaareaupdatebuilder_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME ivaarea_update_builder COMMAND ivaareaupdatebuilder_test)

    add_executable(wiseaiconfigclient_test
        tests/wiseaiconfigclient_test.cpp
        src/iva/ivaareamodels.h
        src/iva/ivaareacapabilityparser.cpp
        src/iva/ivaareacapabilityparser.h
        src/iva/ivaareaoptionsparser.cpp
        src/iva/ivaareaoptionsparser.h
        src/iva/ivaareaparser.cpp
        src/iva/ivaareaparser.h
        src/iva/ivaareaupdatebuilder.cpp
        src/iva/ivaareaupdatebuilder.h
        src/iva/wiseaiconfigclient.cpp
        src/iva/wiseaiconfigclient.h
    )
    target_include_directories(wiseaiconfigclient_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(wiseaiconfigclient_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
    )
    set_target_properties(wiseaiconfigclient_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME wiseai_config_client COMMAND wiseaiconfigclient_test)

    add_executable(ivavideocanvas_test
        tests/ivavideocanvas_test.cpp
        src/iva/ivaareamodels.h
        src/iva/ivavideocanvas.cpp
        src/iva/ivavideocanvas.h
    )
    target_include_directories(ivavideocanvas_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(ivavideocanvas_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    set_target_properties(ivavideocanvas_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME iva_video_canvas COMMAND ivavideocanvas_test)

    add_executable(ivasettingspage_test
        tests/ivasettingspage_test.cpp
        src/api/parkingroi.cpp
        src/api/parkingroi.h
        src/iva/ivaareamodels.h
        src/iva/ivavideocanvas.cpp
        src/iva/ivavideocanvas.h
        src/models/parkingzonelayout.cpp
        src/models/parkingzonelayout.h
        src/pages/ivasettingspage.cpp
        src/pages/ivasettingspage.h
    )
    target_include_directories(ivasettingspage_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(ivasettingspage_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    if (QT_VERSION_MAJOR EQUAL 6)
        qt_add_resources(ivasettingspage_test iva_item_icons
            PREFIX "/"
            FILES
                resources/icons/iva/detection-enter.svg
                resources/icons/iva/detection-generic.svg
                resources/icons/iva/detection-intrusion.svg
                resources/icons/iva/detection-loitering.svg
                resources/icons/iva/check.svg
                resources/icons/iva/object-bicycle.svg
                resources/icons/iva/object-bus.svg
                resources/icons/iva/object-car.svg
                resources/icons/iva/object-generic.svg
                resources/icons/iva/object-motorcycle.svg
                resources/icons/iva/object-person.svg
                resources/icons/iva/object-truck.svg
        )
    else()
        qt5_add_resources(IVA_ITEM_ICON_RESOURCES resources/ivaitemicons.qrc)
        target_sources(ivasettingspage_test PRIVATE ${IVA_ITEM_ICON_RESOURCES})
    endif()
    set_target_properties(ivasettingspage_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    add_test(NAME iva_settings_page COMMAND ivasettingspage_test)

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
        src/models/slotidmapper.cpp
        src/models/slotidmapper.h
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
        src/models/slotidmapper.cpp
        src/models/slotidmapper.h
        src/models/parkingzonelayout.cpp
        src/models/parkingzonelayout.h
        src/models/monitoringevent.cpp
        src/models/monitoringevent.h
        src/api/imageloader.cpp
        src/api/imageloader.h
    )
    target_include_directories(parkingmappage_add_slot_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingmappage_add_slot_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
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
        src/models/slotidmapper.cpp
        src/models/slotidmapper.h
        src/models/parkingzonelayout.cpp
        src/models/parkingzonelayout.h
        src/models/monitoringevent.cpp
        src/models/monitoringevent.h
        src/api/imageloader.cpp
        src/api/imageloader.h
    )
    target_include_directories(parkingmappage_channel_names_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingmappage_channel_names_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
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
        src/models/slotidmapper.cpp
        src/models/slotidmapper.h
        src/models/parkingzonelayout.cpp
        src/models/parkingzonelayout.h
        src/models/monitoringevent.cpp
        src/models/monitoringevent.h
        src/api/imageloader.cpp
        src/api/imageloader.h
    )
    target_include_directories(parkingmappage_undo_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingmappage_undo_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
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
        src/models/slotidmapper.cpp
        src/models/slotidmapper.h
        src/models/parkingzonelayout.cpp
        src/models/parkingzonelayout.h
        src/models/monitoringevent.cpp
        src/models/monitoringevent.h
        src/api/imageloader.cpp
        src/api/imageloader.h
    )
    target_include_directories(parkingmappage_ev_status_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(parkingmappage_ev_status_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
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
    set_tests_properties(settings_page_overstay PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_RUNTIME_ROOT}/plugins/platforms"
        TIMEOUT 30
    )
    set_tests_properties(login_dialog PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_RUNTIME_ROOT}/plugins/platforms"
        TIMEOUT 30
    )
    set_tests_properties(image_loader_auth PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_RUNTIME_ROOT}/plugins/platforms"
        TIMEOUT 30
    )
    set_tests_properties(page_help PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_RUNTIME_ROOT}/plugins/platforms"
        TIMEOUT 30
    )
    set_tests_properties(debugpage_help PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_RUNTIME_ROOT}/plugins/platforms"
        TIMEOUT 30
    )
    set_tests_properties(dashboardpage_help PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_RUNTIME_ROOT}/plugins/platforms;QSG_RHI_BACKEND=software"
        TIMEOUT 30
    )

    add_executable(slotidmapper_test
        tests/slotidmapper_test.cpp
        src/models/slotidmapper.cpp
        src/models/slotidmapper.h
    )
    target_include_directories(slotidmapper_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(slotidmapper_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Test
    )
    set_target_properties(slotidmapper_test PROPERTIES
        AUTOMOC_COMPILER_PREDEFINES OFF
    )
    # QTEST_GUILESS_MAIN links this target against QtTest at runtime, unlike
    # the other small helper tests that only use header-level QTest macros.
    # Keep the matching Qt runtime next to the executable so CTest and a
    # direct test launch use the same deterministic DLL search path.
    add_custom_command(TARGET slotidmapper_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt${QT_VERSION_MAJOR}::Test>"
            "$<TARGET_FILE_DIR:slotidmapper_test>"
    )
    add_test(NAME slotidmapper COMMAND slotidmapper_test)
endif()
