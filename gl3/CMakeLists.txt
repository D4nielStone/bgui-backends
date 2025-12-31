# GLAD
add_library(glad STATIC glad/glad.c)
target_include_directories(glad PUBLIC ${PROJECT_SOURCE_DIR}/backend/gl3 ${PROJECT_SOURCE_DIR}/backend/gl3/glad)
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})

# BGUI OpenGL backend
add_library(bgui_backend_gl3 STATIC
    bgui_backend_gl3.cpp
    gl3_shader.cpp
)

target_compile_definitions(bgui_backend_gl3 PUBLIC BGUI_USE_OPENGL)

target_link_libraries(bgui_backend_gl3
    PUBLIC glad bgui_core
)

target_include_directories(bgui_backend_gl3
    PUBLIC 
        ${PROJECT_SOURCE_DIR}/backend
        ${PROJECT_SOURCE_DIR}/backend/gl3
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/core/include
)