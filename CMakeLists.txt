cmake_minimum_required(VERSION 3.10)
project(PowerCapslock C)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# sherpa-onnx 路径
set(SHERPA_ONNX_DIR ${CMAKE_SOURCE_DIR}/lib/sherpa-onnx-v1.12.36-win-x64-shared-MT-Release)

# 源文件
set(SOURCES
    src/main.c
    src/hook.c
    src/keymap.c
    src/tray.c
    src/config.c
    src/logger.c
    src/keyboard_layout.c
    src/voice.c
    src/audio.c
    resources/resource.rc
)

# 头文件
set(HEADERS
    src/hook.h
    src/keymap.h
    src/tray.h
    src/config.h
    src/logger.h
    src/keyboard_layout.h
    src/voice.h
    src/audio.h
)

# 可执行文件
add_executable(powercapslock WIN32 ${SOURCES} ${HEADERS})

# 包含目录
target_include_directories(powercapslock PRIVATE
    src
    ${SHERPA_ONNX_DIR}/include/sherpa-onnx/c-api
)

# 链接库
target_link_libraries(powercapslock
    user32
    kernel32
    shell32
    comctl32
    gdi32
    ole32
    mmdevapi
    ${SHERPA_ONNX_DIR}/lib/sherpa-onnx-c-api.lib
    ${SHERPA_ONNX_DIR}/lib/onnxruntime.lib
)

# 定义宏
target_compile_definitions(powercapslock PRIVATE
    IDI_ICON_NORMAL=101
    IDI_ICON_DISABLED=102
    USE_SHERPA_ONNX
)

# 构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 编译选项
if(MSVC)
    target_compile_options(powercapslock PRIVATE
        /W3
        /utf-8
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(powercapslock PRIVATE
            /O2
            /GL
        )
        set_target_properties(powercapslock PROPERTIES
            LINK_FLAGS "/LTCG"
        )
    endif()
else()
    target_compile_options(powercapslock PRIVATE
        -Wall
        -Wextra
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(powercapslock PRIVATE
            -O2
        )
    endif()
endif()

# 安装规则
install(TARGETS powercapslock DESTINATION bin)
install(FILES config/config.json DESTINATION config)
install(FILES resources/icon.ico resources/icon_disabled.ico DESTINATION resources)

# 创建 models 目录（用于放置语音识别模型）
install(CODE "file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/models\")")

# 打印配置信息
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
