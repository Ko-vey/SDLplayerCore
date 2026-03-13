# 1. 设置CMake最低版本和项目信息
cmake_minimum_required(VERSION 3.15)
project(SDLplayerCore VERSION 1.0.1 LANGUAGES CXX)

# 2. 设置C++标准为C++11
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 解决 FFmpeg 在 C++ 环境下的宏定义报错
add_compile_definitions(
    __STDC_CONSTANT_MACROS
    __STDC_LIMIT_MACROS
)

# 3. 定义可执行文件名
set(EXECUTABLE_NAME "SDLPlayer")

# 4. 搜集源文件
file(GLOB_RECURSE PLAYER_SOURCES
    "src/source/*.cpp"
    "src/include/*.h"
)

# 5. 配置依赖库 (跨平台适配核心逻辑)
if(WIN32)
    message(STATUS "⚙️ 配置 Windows 环境 (使用本地 third_party)...")
    
    set(SDL2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2")
    set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg")

    set(PLATFORM_INCLUDE_DIRS 
        "${SDL2_ROOT}/include" 
        "${FFMPEG_ROOT}/include"
    )

    # Windows 下严格从指定目录查找
    find_library(SDL2_LIBRARY NAMES SDL2 SDL2.lib PATHS "${SDL2_ROOT}/lib" NO_DEFAULT_PATH)
    find_library(SDL2_MAIN_LIBRARY NAMES SDL2main SDL2main.lib PATHS "${SDL2_ROOT}/lib" NO_DEFAULT_PATH)
    find_library(SDL2_TTF_LIBRARY NAMES SDL2_ttf SDL2_ttf.lib PATHS "${SDL2_ROOT}/lib" NO_DEFAULT_PATH)

    set(FFMPEG_LIBRARIES_NAMES avcodec avformat avutil swscale swresample avfilter)
    set(PLATFORM_LIBRARIES ${SDL2_MAIN_LIBRARY} ${SDL2_LIBRARY} ${SDL2_TTF_LIBRARY})
    
    foreach(LIB_NAME ${FFMPEG_LIBRARIES_NAMES})
        find_library(FF_LIB_${LIB_NAME} NAMES ${LIB_NAME} ${LIB_NAME}.lib PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        if(FF_LIB_${LIB_NAME})
            list(APPEND PLATFORM_LIBRARIES ${FF_LIB_${LIB_NAME}})
        else()
            message(WARNING "FFmpeg library not found: ${LIB_NAME}")
        endif()
    endforeach()

else()
    message(STATUS "🐧 配置 Linux/Unix 环境 (使用系统 pkg-config)...")
    
    # 引入 PkgConfig，这是 Linux 下查找库的工业标准
    find_package(PkgConfig REQUIRED)
    
    # 自动查找并提取系统的 SDL2 和 FFmpeg 路径
    pkg_check_modules(SDL2_PKG REQUIRED sdl2 SDL2_ttf)
    pkg_check_modules(FFMPEG_PKG REQUIRED libavcodec libavformat libavutil libswscale libswresample libavfilter)
    
    set(PLATFORM_INCLUDE_DIRS ${SDL2_PKG_INCLUDE_DIRS} ${FFMPEG_PKG_INCLUDE_DIRS})
    set(PLATFORM_LIBRARIES ${SDL2_PKG_LIBRARIES} ${FFMPEG_PKG_LIBRARIES})
endif()

# 6. 创建目标与链接
add_executable(${EXECUTABLE_NAME} ${PLAYER_SOURCES})

# 统一添加头文件路径
target_include_directories(${EXECUTABLE_NAME} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/include"
    ${PLATFORM_INCLUDE_DIRS}
)

# 统一链接库文件
target_link_libraries(${EXECUTABLE_NAME} PRIVATE
    ${PLATFORM_LIBRARIES}
)

# 7. 构建后自动复制 DLL (仅限 Windows)
if(WIN32)
    message(STATUS "Configuring post-build DLL copy for Windows...")
    set(SDL2_BIN_DIR "${SDL2_ROOT}/bin")
    set(FFMPEG_BIN_DIR "${FFMPEG_ROOT}/bin")

    file(GLOB ALL_DLLS "${SDL2_BIN_DIR}/*.dll" "${FFMPEG_BIN_DIR}/*.dll")

    if(ALL_DLLS)
        # 使用 foreach 循环，逐个文件生成纯净的拷贝命令，避免触发 pwsh.exe 依赖
        foreach(DLL_FILE ${ALL_DLLS})
            add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL_FILE}"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
            )
        endforeach()
        
        # 成功提示命令
        add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "✅ Auto-copying DLLs completed successfully."
        )
    endif()
endif()