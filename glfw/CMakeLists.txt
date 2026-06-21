add_library(bgui_backend_glfw STATIC bgui_backend_glfw.cpp)

if (BGUI_USE_OPENGL)
target_compile_definitions(bgui_backend_glfw PUBLIC BGUI_USE_GLFW BGUI_USE_OPENGL)
else()
target_compile_definitions(bgui_backend_glfw PUBLIC BGUI_USE_GLFW)
endif()

find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(bgui_backend_glfw PUBLIC glfw bgui_core)
target_include_directories(bgui_backend_glfw
    PUBLIC ${PROJECT_SOURCE_DIR}/backend
    ${PROJECT_SOURCE_DIR}/core/include
)