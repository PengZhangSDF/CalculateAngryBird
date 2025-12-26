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

# ========== 2. SDL2 配置（手动方式，适配 MinGW） ==========
set(SDL2_DIR "D:\\mingw64\\SDL2-2.30.0\\x86_64-w64-mingw32")
include_directories(${SDL2_DIR}/include ${SDL2_DIR}/include/SDL2)
link_directories(${SDL2_DIR}/lib)

# ========== 3. SDL_ttf 配置（手动方式，适配 MinGW） ==========
set(SDL2_TTF_DIR "D:\\mingw64\\SDL2_ttf-2.22.0\\x86_64-w64-mingw32")
include_directories(${SDL2_TTF_DIR}/include ${SDL2_TTF_DIR}/include/SDL2)
link_directories(${SDL2_TTF_DIR}/lib)

# ========== 4. SFML 3.0.2 配置（保留标准化方式） ==========
set(SFML_DIR "D:\\mingw64\\SFML-3.0.2\\lib\\cmake\\SFML" CACHE PATH "Path to SFML 3.0.2 cmake config")
find_package(SFML 3.0.2 EXACT COMPONENTS Graphics Audio REQUIRED)
if (NOT SFML_VERSION VERSION_EQUAL 3.0.2)
    message(FATAL_ERROR "找到的 SFML 版本不是 3.0.2！当前版本：${SFML_VERSION}")
endif()

# ========== 5. GLM 1.0.2 配置（手动方式） ==========
set(GLM_DIR "D:\\mingw64\\glm-1.0.2" CACHE PATH "Path to GLM 1.0.2")
include_directories(${GLM_DIR})

# ========== 6. 源文件配置（保留） ==========
file(GLOB GAME_SOURCES "src/*.cpp")
add_executable(${PROJECT_NAME}
        ${GAME_SOURCES}
        main.cpp
)

# ========== 7. 编译定义 + 链接库（全手动库名，彻底解决链接问题） ==========
target_compile_definitions(${PROJECT_NAME} PRIVATE SDL_MAIN_HANDLED)
target_include_directories(${PROJECT_NAME} PRIVATE src)
# 链接顺序：SDL2main → SDL2 → SDL2_ttf → SFML → Box2D（MinGW 下全兼容）
target_link_libraries(${PROJECT_NAME} PRIVATE
        # SDL2 库（MinGW 固定名）
        SDL2main
        SDL2
        SDL2_ttf
        # SFML 标准化目标（官方配置完整）
        SFML::Graphics
        SFML::Audio
        # Box2D 手动库名（MinGW 下固定为 Box2D，对应 libBox2D.a）
        Box2D
)

# ========== 8. 自动复制 DLL（保留） ==========
if(MINGW)
    # 复制 SDL2 DLL
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${SDL2_DIR}/bin/SDL2.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Copying SDL2.dll..."
    )

    # 复制 SDL_ttf 及依赖 DLL
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SDL2_TTF_DIR}/bin"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Copying SDL2_ttf DLLs..."
    )
endif()