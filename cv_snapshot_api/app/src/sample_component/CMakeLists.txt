set(TARGET_LIB sample_component)
set(OPENCV_ROOT "${CMAKE_HOME_DIRECTORY}/3rd_party")

add_library(${TARGET_LIB} MODULE
    sample_component.cc
    sample_component_network.cc
    sample_component_image.cc
    sample_component_log.cc
    frame_processor.cc
    environment_processor.cc
    sample_component_environment.cc
    sample_component_images.cc
    file_logger.cc
)

target_include_directories(${TARGET_LIB} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/includes
    ${APP_SUPPORT_PACKAGE}/app_network_manager/includes
    ${OPENCV_ROOT}/include
)

target_link_directories(${TARGET_LIB} PRIVATE
    ${CMAKE_HOME_DIRECTORY}/libs
    ${OPENCV_ROOT}/lib
)

target_link_libraries(${TARGET_LIB} PRIVATE
    opencv_imgcodecs
    opencv_imgproc
    opencv_core
    stdlib
    pthread
)

install(TARGETS ${TARGET_LIB}
    DESTINATION ${CMAKE_HOME_DIRECTORY}/libs/${TARGET_LIB}
)

install(DIRECTORY ${OPENCV_ROOT}/lib/
    DESTINATION ${CMAKE_HOME_DIRECTORY}/libs
    FILES_MATCHING PATTERN "libopencv_*.so*"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/manifests/
    DESTINATION ${CMAKE_HOME_DIRECTORY}/libs/${TARGET_LIB}
    FILES_MATCHING PATTERN "*manifest.json"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/manifests/
    DESTINATION ${CMAKE_HOME_DIRECTORY}/res/models
    FILES_MATCHING
    PATTERN "*instance*.json"
    PATTERN "*attribute*.json"
)
