# 1. 设置CMake最低版本和项目信息
cmake_minimum_required(VERSION 3.15)
project(SDLplayerCore LANGUAGES CXX)

# 2. 设置C++标准为C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 3. 定义可执行文件名
set(EXECUTABLE_NAME "SDLPlayer")

# 4. 搜集自己的所有源文件
# glob_recursive会递归地查找所有匹配的文件，方便管理
file(GLOB_RECURSE PLAYER_SOURCES
    "src/source/*.cpp"
    "src/include/*.h"
)

# 5. --- 配置SDL2依赖 ---
# 定义SDL2库的路径
set(SDL2_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/include)
set(SDL2_LIBRARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/lib)

# 找到SDL2的主库和main库
find_library(SDL2_LIBRARY NAMES SDL2 SDL2.lib PATHS ${SDL2_LIBRARY_DIR})
find_library(SDL2_MAIN_LIBRARY NAMES SDL2main SDL2main.lib PATHS ${SDL2_LIBRARY_DIR})

# 6. --- 配置FFmpeg依赖 ---
# 定义FFmpeg库的路径
set(FFMPEG_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/include)
set(FFMPEG_LIBRARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/lib)

# 定义需要链接的FFmpeg库列表（注意：只写名字，不带.lib后缀）
set(FFMPEG_LIBRARIES avcodec avformat avutil swscale swresample avfilter)

# 7. 创建可执行目标
add_executable(${EXECUTABLE_NAME} ${PLAYER_SOURCES})

# 8. --- 链接依赖 ---
# 为可执行文件添加头文件搜索路径
target_include_directories(${EXECUTABLE_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include  # 自己的头文件
    ${SDL2_INCLUDE_DIR}                      # SDL2的头文件
    ${FFMPEG_INCLUDE_DIR}                    # FFmpeg的头文件
)

# 为可执行文件指定链接库的搜索路径
target_link_directories(${EXECUTABLE_NAME} PUBLIC
    ${SDL2_LIBRARY_DIR}
    ${FFMPEG_LIBRARY_DIR}
)

# 将所有库链接到可执行文件
target_link_libraries(${EXECUTABLE_NAME} PRIVATE
    ${SDL2_LIBRARY}
    ${SDL2_MAIN_LIBRARY}
    ${FFMPEG_LIBRARIES}
)

# 9. --- 【关键步骤】构建后自动复制DLL文件 ---
# 这个命令会在成功构建可执行文件后，将所有需要的DLL复制到exe旁边
# 这样就可以直接在build目录下运行或调试程序了
message(STATUS "Configuring post-build DLL copy...")

# 定义DLL源目录
set(SDL2_DLL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/bin)
set(FFMPEG_DLL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/bin)

# 获取所有DLL文件的列表
file(GLOB FFMPEG_DLLS "${FFMPEG_DLL_DIR}/*.dll")
file(GLOB SDL2_DLLS "${SDL2_DLL_DIR}/*.dll")

# 添加自定义命令
add_custom_command(
    TARGET ${EXECUTABLE_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${FFMPEG_DLLS}
    $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${SDL2_DLLS}
    $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
    COMMENT "Copying required DLLs to output directory..."
)
