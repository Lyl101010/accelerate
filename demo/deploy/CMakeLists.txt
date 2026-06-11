cmake_minimum_required(VERSION 3.15.1)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

project(demo)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(DEMO_BUNDLE_LIBCURL "Bundle cross-build libcurl into the deploy package" OFF)
if(ENABLE_ASAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,--allow-shlib-undefined")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wl,--allow-shlib-undefined")

add_link_options(
    -Bsymbolic
    -Wl,--allow-shlib-undefined
    -Wl,--no-as-needed
)

set(TARGET_LIB_ARCH aarch64)

# Link only the OpenCV modules used by the demo.  This avoids pulling in every
# installed OpenCV module as a direct runtime dependency.
set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/opencv-linux-aarch64/share/OpenCV" CACHE PATH "OpenCV dir" FORCE)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio highgui)
set(ZLIB_LIBRARY /usr/lib/aarch64-linux-gnu/libz.so CACHE FILEPATH "arm64 zlib library" FORCE)
find_package(ZLIB REQUIRED)
set(CURL_INCLUDE_DIR /usr/include/aarch64-linux-gnu CACHE PATH "arm64 curl include" FORCE)
set(CURL_LIBRARY /usr/lib/aarch64-linux-gnu/libcurl.so CACHE FILEPATH "arm64 curl library" FORCE)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

if(ZLIB_FOUND)
    include_directories(${ZLIB_INCLUDE_DIRS})
endif()

set(RKNN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/librknnrt)
set(LIBRKNNRT ${RKNN_PATH}/Linux/librknn_api/aarch64/librknnrt.so)
set(LIBRKNNRT_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknpu2/include)

set(RKLLM_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../rknn-llm-main/rkllm-runtime)
set(LIBRKLLMRT ${RKLLM_PATH}/Linux/librkllm_api/aarch64/librkllmrt.so)
set(LIBRKLLMRT_INCLUDES ${RKLLM_PATH}/Linux/librkllm_api/include)

set(rknpu_yolov8_file ${CMAKE_CURRENT_SOURCE_DIR}/src/rknpu2/yolov8.cc)

# ?????????
# ?????src ???????????????????????????????# - sensing/input???????????????????????
# - fusion??eaFusion ?????? RKNN ?????# - detection??OLOv8 RKNN ???????????
# - reasoning/kg??QLite ??????????????????
# - telemetry/transport??PS ??????????#
# ?????????????????????????? src/kg??? postprocess.cc ?????# ??? CMake ??????????????src ???????????????????set(DEMO_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/image_enc.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/postprocess.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/border_event_analyzer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/border_kg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kg/sqlite3.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/GPS.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/camera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fusion_inference.cpp
    ${rknpu_yolov8_file}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/input/preview_window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/input/video_recorder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/yolov8_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/send_info.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pruning/dart.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pruning/cdpruner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pruning/saint.cpp
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/utils utils.out)

set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")
link_libraries(m)

add_executable(demo
    ${DEMO_SOURCE_FILES}
)

target_include_directories(demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kg
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${LIBRKNNRT_INCLUDES}
    ${LIBRKLLMRT_INCLUDES}
    ${OpenCV_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
)

target_link_libraries(demo
    PRIVATE
    ${OpenCV_LIBS}
    ${LIBRKNNRT}
    ${LIBRKLLMRT}
    ${CURL_LIBRARIES}
    ${ZLIB_LIBRARIES}
    fileutils
    imagedrawing
    m
    ${CMAKE_DL_LIBS}
    Threads::Threads
)

set_target_properties(demo PROPERTIES
    LINK_FLAGS "-Wl,--no-undefined -Wl,--export-dynamic"
)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/install/demo_Linux_aarch64 CACHE PATH "Install prefix" FORCE)
endif()

function(install_runtime_library_patterns)
    foreach(_runtime_lib IN LISTS ARGN)
        set(_runtime_path "")

        if(TARGET "${_runtime_lib}")
            get_target_property(_runtime_path "${_runtime_lib}" IMPORTED_LOCATION_RELEASE)
            if(NOT _runtime_path)
                get_target_property(_runtime_path "${_runtime_lib}" IMPORTED_LOCATION)
            endif()
        elseif(IS_ABSOLUTE "${_runtime_lib}")
            set(_runtime_path "${_runtime_lib}")
        endif()

        if(NOT _runtime_path OR NOT EXISTS "${_runtime_path}")
            continue()
        endif()

        get_filename_component(_runtime_name "${_runtime_path}" NAME)
        if(NOT _runtime_name MATCHES "\\.so(\\.|$)")
            continue()
        endif()

        get_filename_component(_runtime_dir "${_runtime_path}" DIRECTORY)
        string(REGEX REPLACE "^(.*\\.so)(\\..*)?$" "\\1*" _runtime_pattern "${_runtime_name}")
        install(
            DIRECTORY "${_runtime_dir}/"
            DESTINATION lib
            FILES_MATCHING PATTERN "${_runtime_pattern}"
        )
    endforeach()
endfunction()

# ???????????# - demo ????????????????? libcurl??end_info ??????????????????
# - ??????????????? libcurl.so* ??? install/lib??#
# ?????# ????????? libcurl ?????????????????libldap_r-2.4.so.2??# ??? Ubuntu 22.04 ???????????????? demo ?????$ORIGIN/lib??# ????install/lib ?????? libcurl.so.4????????????????????# ???????????? main() ?????? libldap_r-2.4.so.2??#
# ????????????????????libcurl4?????????????????????
# ??? CMake ??????????? -DDEMO_BUNDLE_LIBCURL=ON?????????
# libcurl ?????????????set(EXTRA_RUNTIME_LIBS ${OpenCV_LIBS} ${ZLIB_LIBRARIES})
if(DEMO_BUNDLE_LIBCURL)
    list(APPEND EXTRA_RUNTIME_LIBS ${CURL_LIBRARIES})
endif()

foreach(_gcc_runtime libstdc++.so.6 libgcc_s.so.1 libgomp.so.1)
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${_gcc_runtime}
        OUTPUT_VARIABLE _gcc_runtime_path
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(IS_ABSOLUTE "${_gcc_runtime_path}" AND EXISTS "${_gcc_runtime_path}")
        list(APPEND EXTRA_RUNTIME_LIBS "${_gcc_runtime_path}")
    endif()
endforeach()

install(TARGETS demo DESTINATION .)
install(PROGRAMS ${LIBRKNNRT} ${LIBRKLLMRT} DESTINATION lib)
install_runtime_library_patterns(${EXTRA_RUNTIME_LIBS})

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/model/bus.jpg DESTINATION model)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/model/coco_80_labels_list.txt DESTINATION model)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/border_kg_snapshot.db DESTINATION model)
file(GLOB RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/model/*.rknn")
file(GLOB RKLLM_FILES "${CMAKE_CURRENT_SOURCE_DIR}/model/*.rkllm")
install(FILES ${RKNN_FILES} DESTINATION model)
install(FILES ${RKLLM_FILES} DESTINATION model)

set(ROOT_MODEL_FILES)
foreach(_model_file
        self_best_i8_ver01.rknn
        ztl_yolov8n_rk3588_i8_ver01.rknn
        Qwen2-VL-2B_vision_rk3588.rknn
        qwen2_vl_2b_instruct.rkllm)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_model_file}")
        list(APPEND ROOT_MODEL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/${_model_file}")
    endif()
endforeach()
if(ROOT_MODEL_FILES)
    install(FILES ${ROOT_MODEL_FILES} DESTINATION .)
endif()

foreach(_required_model
        Qwen2-VL-2B_vision_rk3588.rknn
        qwen2_vl_2b_instruct.rkllm)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_required_model}" AND
       NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/model/${_required_model}")
        message(WARNING
            "Required runtime model '${_required_model}' was not found under deploy/ or deploy/model/. "
            "The binary can still be built, but board-side VLM startup needs this file copied to the run directory "
            "or DEMO_VISION_MODEL/DEMO_LLM_MODEL set explicitly.")
    endif()
endforeach()
install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../data/demo.jpg DESTINATION .)
