# 1. 设置CMake最低版本和项目信息
cmake_minimum_required(VERSION 3.15)
project(SDLplayerCore VERSION 1.0.1 LANGUAGES CXX)

# 2. 设置C++标准为C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 3. 定义可执行文件名
set(EXECUTABLE_NAME "SDLPlayer")

# 4. 搜集源文件
# 注意: 如果添加新文件后编译报错，请尝试重新运行 CMake 配置
file(GLOB_RECURSE PLAYER_SOURCES
    "src/source/*.cpp"
    "src/include/*.h"
)

# 5. 配置依赖库路径 (手动模式)
set(SDL2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2")
set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg")

# --- SDL2 配置 ---
set(SDL2_INCLUDE_DIR "${SDL2_ROOT}/include")
set(SDL2_LIBRARY_DIR "${SDL2_ROOT}/lib")

# 查找 SDL2 核心库
find_library(SDL2_LIBRARY NAMES SDL2 SDL2.lib PATHS ${SDL2_LIBRARY_DIR} NO_DEFAULT_PATH)
find_library(SDL2_MAIN_LIBRARY NAMES SDL2main SDL2main.lib PATHS ${SDL2_LIBRARY_DIR} NO_DEFAULT_PATH)
# 查找 SDL2_ttf 库
find_library(SDL2_TTF_LIBRARY NAMES SDL2_ttf SDL2_ttf.lib PATHS ${SDL2_LIBRARY_DIR} NO_DEFAULT_PATH)

# --- FFmpeg 配置 ---
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIBRARY_DIR "${FFMPEG_ROOT}/lib")
# 定义需要链接的FFmpeg库组件
set(FFMPEG_LIBRARIES_NAMES avcodec avformat avutil swscale swresample avfilter)
set(FFMPEG_LIBRARIES "")
foreach(LIB_NAME ${FFMPEG_LIBRARIES_NAMES})
    find_library(FF_LIB_${LIB_NAME} NAMES ${LIB_NAME} ${LIB_NAME}.lib PATHS ${FFMPEG_LIBRARY_DIR} NO_DEFAULT_PATH)
    if(FF_LIB_${LIB_NAME})
        list(APPEND FFMPEG_LIBRARIES ${FF_LIB_${LIB_NAME}})
    else()
        message(WARNING "FFmpeg library not found: ${LIB_NAME}")
    endif()
endforeach()

# 6. 创建目标与链接
add_executable(${EXECUTABLE_NAME} ${PLAYER_SOURCES})

# 添加头文件路径
target_include_directories(${EXECUTABLE_NAME} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/include"
    "${SDL2_INCLUDE_DIR}"
    "${FFMPEG_INCLUDE_DIR}"
)

# 链接库文件
target_link_libraries(${EXECUTABLE_NAME} PRIVATE
    ${SDL2_MAIN_LIBRARY}
    ${SDL2_LIBRARY}
    ${SDL2_TTF_LIBRARY}  # 链接 TTF 库
    ${FFMPEG_LIBRARIES}
)

# 7. 构建后自动复制 DLL
message(STATUS "Configuring post-build DLL copy for Windows...")

if(WIN32)
    # 定义DLL源目录
    set(SDL2_BIN_DIR "${SDL2_ROOT}/bin")
    set(FFMPEG_BIN_DIR "${FFMPEG_ROOT}/bin")

    # 获取所有DLL
    file(GLOB ALL_DLLS 
        "${SDL2_BIN_DIR}/*.dll" 
        "${FFMPEG_BIN_DIR}/*.dll"
    )

    # 只有当找到DLL时才添加命令，防止报错
    if(ALL_DLLS)
        add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ALL_DLLS}
            $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
            COMMENT "✅ Auto-copying DLLs to executable directory..."
        )
    else()
        message(WARNING "⚠️  No DLLs found in third_party/bin folders. You might need to copy them manually or check paths.")
    endif()
endif()
