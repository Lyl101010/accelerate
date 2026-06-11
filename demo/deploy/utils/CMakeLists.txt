cmake_minimum_required(VERSION 3.15)

project(rknn_model_zoo_utils)

# 保留文件工具库
add_library(fileutils STATIC
    file_utils.c
)
target_include_directories(fileutils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 保留图像绘制库（依赖image_drawing.h）
add_library(imagedrawing STATIC
    image_drawing.c
)
target_include_directories(imagedrawing PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 保留音频工具库（如需使用）
add_library(audioutils STATIC
    audio_utils.c
)

target_link_libraries(audioutils
    ${LIBSNDFILE}
)

target_include_directories(audioutils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBSNDFILE_INCLUDES}
)

# 移除所有与imageutils相关的配置
# 1. 删除imageutils库编译
# 2. 清理RGA相关定义和依赖
# 3. 移除LIBJPEG相关配置

# 注意：如需保留部分RGA功能，需单独维护，但不关联到imageutils
