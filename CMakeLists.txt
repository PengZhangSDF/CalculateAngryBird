cmake_minimum_required(VERSION 3.28)
project(CalculateAngeuBird LANGUAGES CXX)

# 强制使用 MinGW Makefiles 生成器
if(CMAKE_GENERATOR AND NOT CMAKE_GENERATOR MATCHES "MinGW Makefiles")
    message(WARNING "当前生成器不是 MinGW Makefiles，请使用: cmake -G \"MinGW Makefiles\" -S . -B build")
endif()

# ========== 基础配置 ==========
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

# ========== 静态链接配置 ==========
# 设置静态链接标志，避免依赖DLL
if(MINGW)
    # 完全静态链接：包括libgcc、libstdc++、pthread等
    # 注意：exe本身可能仍需要libwinpthread-1.dll，因为SFML DLL依赖它
    # 但我们可以尽量静态链接exe本身
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".lib")
    set(BUILD_SHARED_LIBS OFF)
    # 强制使用静态版本的stdc++
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
    message(STATUS "使用静态链接模式（libgcc、libstdc++），pthread和SFML依赖的DLL需要单独复制")
endif()

# ========== 1. Box2D 2.4.1 配置（回归手动方式，适配老版本） ==========
set(BOX2D_DIR "D:\\mingw64\\Box2D-2.4.1" CACHE PATH "Path to Box2D 2.4.1")
# 手动添加 Box2D 头文件目录
include_directories(${BOX2D_DIR}/include)
# 手动添加 Box2D 库目录
link_directories(${BOX2D_DIR}/lib)
# 验证 Box2D 目录是否存在（可选，增加容错）
if (NOT EXISTS ${BOX2D_DIR}/include/box2d/b2_world.h)
    message(FATAL_ERROR "Box2D 头文件缺失！请检查 BOX2D_DIR：${BOX2D_DIR}")
endif()
if (NOT EXISTS ${BOX2D_DIR}/lib/libBox2D.a)
    message(FATAL_ERROR "Box2D 静态库缺失！请检查 BOX2D_DIR：${BOX2D_DIR}")
endif()
message(STATUS "成功找到 Box2D 2.4.1：头文件=${BOX2D_DIR}/include，库=${BOX2D_DIR}/lib/libBox2D.a")

# ========== 2. SDL2 配置（手动方式，适配 MinGW，使用静态库） ==========
set(SDL2_DIR "D:\\mingw64\\SDL2-2.30.0\\x86_64-w64-mingw32")
include_directories(${SDL2_DIR}/include ${SDL2_DIR}/include/SDL2)
link_directories(${SDL2_DIR}/lib)
# 优先查找静态库
if(MINGW)
    # MinGW下静态库通常是libSDL2.a, libSDL2main.a
    set(SDL2_STATIC_LIB "libSDL2.a")
    set(SDL2_MAIN_STATIC_LIB "libSDL2main.a")
    if(EXISTS "${SDL2_DIR}/lib/${SDL2_STATIC_LIB}")
        message(STATUS "使用SDL2静态库: ${SDL2_STATIC_LIB}")
    else()
        message(WARNING "未找到SDL2静态库，将使用动态库")
    endif()
endif()

# ========== 3. SDL_ttf 配置（手动方式，适配 MinGW，使用静态库） ==========
set(SDL2_TTF_DIR "D:\\mingw64\\SDL2_ttf-2.22.0\\x86_64-w64-mingw32")
include_directories(${SDL2_TTF_DIR}/include ${SDL2_TTF_DIR}/include/SDL2)
link_directories(${SDL2_TTF_DIR}/lib)
# 优先查找静态库
if(MINGW)
    set(SDL2_TTF_STATIC_LIB "libSDL2_ttf.a")
    if(EXISTS "${SDL2_TTF_DIR}/lib/${SDL2_TTF_STATIC_LIB}")
        message(STATUS "使用SDL2_ttf静态库: ${SDL2_TTF_STATIC_LIB}")
    else()
        message(WARNING "未找到SDL2_ttf静态库，将使用动态库")
    endif()
endif()

# ========== 4. SFML 3.0.2 配置（尝试静态链接，如果失败则使用动态库） ==========
set(SFML_DIR "D:\\mingw64\\SFML-3.0.2\\lib\\cmake\\SFML" CACHE PATH "Path to SFML 3.0.2 cmake config")
# 注意：SFML静态链接需要额外的依赖库（Ogg、Vorbis等），可能难以完全静态链接
# 这里先尝试使用动态库，但会尽量静态链接其他库
find_package(SFML 3.0.2 EXACT COMPONENTS Graphics Audio REQUIRED)
if (NOT SFML_VERSION VERSION_EQUAL 3.0.2)
    message(FATAL_ERROR "找到的 SFML 版本不是 3.0.2！当前版本：${SFML_VERSION}")
endif()
message(STATUS "SFML使用动态链接（静态链接需要额外依赖库）")

# ========== 5. GLM 1.0.2 配置（手动方式） ==========
set(GLM_DIR "D:\\mingw64\\glm-1.0.2" CACHE PATH "Path to GLM 1.0.2")
include_directories(${GLM_DIR})

# ========== 6. 源文件配置（保留） ==========
file(GLOB GAME_SOURCES "src/*.cpp")
# 使用 WIN32 选项创建窗口应用程序（不显示控制台窗口）
add_executable(${PROJECT_NAME} WIN32
        ${GAME_SOURCES}
        main.cpp
)

# ========== 7. 编译定义 + 链接库（静态链接配置） ==========
target_compile_definitions(${PROJECT_NAME} PRIVATE SDL_MAIN_HANDLED)
target_include_directories(${PROJECT_NAME} PRIVATE src)

# 静态链接配置：优先使用静态库
if(MINGW)
    # 尝试使用静态库名称
    set(SDL2_LIB_NAME "SDL2")
    set(SDL2_MAIN_LIB_NAME "SDL2main")
    set(SDL2_TTF_LIB_NAME "SDL2_ttf")
    
    # 检查静态库是否存在，如果不存在则使用默认名称
    if(EXISTS "${SDL2_DIR}/lib/libSDL2.a")
        set(SDL2_LIB_NAME "libSDL2.a")
        set(SDL2_MAIN_LIB_NAME "libSDL2main.a")
    endif()
    if(EXISTS "${SDL2_TTF_DIR}/lib/libSDL2_ttf.a")
        set(SDL2_TTF_LIB_NAME "libSDL2_ttf.a")
    endif()
    
    # 链接顺序：SDL2main → SDL2 → SDL2_ttf → SFML → Box2D（静态链接）
    target_link_libraries(${PROJECT_NAME} PRIVATE
            # SDL2 库（优先静态库）
            ${SDL2_MAIN_LIB_NAME}
            ${SDL2_LIB_NAME}
            ${SDL2_TTF_LIB_NAME}
            # SFML 静态库（通过SFML_STATIC_LIBRARIES控制）
            SFML::Graphics
            SFML::Audio
            # Box2D 静态库（已经是静态库）
            Box2D
            # MinGW静态链接需要的额外库
            mingw32
            m
            # pthread使用静态版本（通过-static标志自动处理）
    )
else()
    # 非MinGW环境使用默认链接方式
    target_link_libraries(${PROJECT_NAME} PRIVATE
            SDL2main
            SDL2
            SDL2_ttf
            SFML::Graphics
            SFML::Audio
            Box2D
    )
endif()

# ========== 8. DLL复制（仅复制SFML DLL及其依赖，SDL2和SDL2_ttf使用静态库） ==========
if(MINGW)
    # 只复制SFML的DLL（因为SFML使用动态链接）
    # SDL2和SDL2_ttf使用静态库，不需要DLL
    set(SFML_BIN_DIR "D:/mingw64/SFML-3.0.2/bin")
    if(EXISTS "${SFML_BIN_DIR}/sfml-graphics-3.dll")
        # 复制SFML核心DLL
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SFML_BIN_DIR}/sfml-graphics-3.dll"
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
                COMMENT "Copying SFML Graphics DLL..."
        )
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SFML_BIN_DIR}/sfml-audio-3.dll"
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
                COMMENT "Copying SFML Audio DLL..."
        )
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SFML_BIN_DIR}/sfml-window-3.dll"
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
                COMMENT "Copying SFML Window DLL..."
        )
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SFML_BIN_DIR}/sfml-system-3.dll"
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
                COMMENT "Copying SFML System DLL..."
        )
        
        # 复制MinGW运行时DLL（SFML DLL依赖这些）
        # 这些DLL是MinGW编译器提供的，必须与exe一起分发
        set(MINGW_BIN_DIR "D:/mingw64/bin")
        
        if(EXISTS "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
                    $<TARGET_FILE_DIR:${PROJECT_NAME}>
                    COMMENT "Copying libgcc_s_seh-1.dll..."
            )
        else()
            message(WARNING "未找到 libgcc_s_seh-1.dll，请手动复制到输出目录")
        endif()
        
        if(EXISTS "${MINGW_BIN_DIR}/libstdc++-6.dll")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN_DIR}/libstdc++-6.dll"
                    $<TARGET_FILE_DIR:${PROJECT_NAME}>
                    COMMENT "Copying libstdc++-6.dll..."
            )
        else()
            message(WARNING "未找到 libstdc++-6.dll，请手动复制到输出目录")
        endif()
        
        # 复制libwinpthread DLL（如果SFML依赖它）
        if(EXISTS "${MINGW_BIN_DIR}/libwinpthread-1.dll")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN_DIR}/libwinpthread-1.dll"
                    $<TARGET_FILE_DIR:${PROJECT_NAME}>
                    COMMENT "Copying libwinpthread-1.dll..."
            )
        endif()
        
        message(STATUS "SDL2和SDL2_ttf使用静态链接，SFML使用动态链接（需要DLL和MinGW运行时）")
    else()
        message(WARNING "未找到SFML DLL目录，请手动复制SFML DLL到输出目录")
    endif()
endif()