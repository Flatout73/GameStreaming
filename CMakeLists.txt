cmake_minimum_required(VERSION 3.15)
project(MyGame)

set(ENGINE_DIR "/Users/flatout/Documents/UniVien/CloudGaming/ViennaVulkanEngine")
link_directories("/Users/flatout/VulkanSDK/1.4.341.1/macOS/lib")
add_subdirectory(${ENGINE_DIR} ViennaVulkanEngine_build)

set(TARGET vve_mygame)
set(SOURCE game.cpp)
set(HEADERS )

add_executable(${TARGET} ${SOURCE} ${HEADERS})

if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    target_compile_options(${TARGET} PRIVATE -include unistd.h -fexperimental-library -DIMGUI_IMPL_VULKAN_NO_PROTOTYPES)
endif()

target_compile_features(${TARGET} PUBLIC cxx_std_20)

# set the repository root directory as the debug working directory for this target in VS
if(MSVC)
    set_property(TARGET ${TARGET} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

target_link_libraries (${TARGET} PUBLIC viennavulkanengine)

# if(APPLE)
#     add_custom_command(TARGET ${TARGET} POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E create_symlink "${ENGINE_DIR}/shaders" "$<TARGET_FILE_DIR:${TARGET}>/shaders"
#         COMMAND ${CMAKE_COMMAND} -E create_symlink "${ENGINE_DIR}/assets" "$<TARGET_FILE_DIR:${TARGET}>/assets"
#     )
# endif()

