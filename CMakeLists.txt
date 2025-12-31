add_library(bgui_backend INTERFACE)
enable_language(C)

target_link_libraries(bgui_backend INTERFACE bgui_core)
# FREETYPE backend
if(BGUI_USE_FREETYPE)
    add_subdirectory(freetype)
    target_link_libraries(bgui_backend INTERFACE bgui_backend_freetype)
endif()

# OpenGL backend
if(BGUI_USE_OPENGL)
    add_subdirectory(gl3)
    target_link_libraries(bgui_backend INTERFACE bgui_backend_gl3)
endif()

if(BGUI_USE_VULKAN)
    add_subdirectory(vulkan)
    target_link_libraries(bgui_backend INTERFACE bgui_backend_vulkan)
endif()

# GLFW backend
if(BGUI_USE_GLFW)
    add_subdirectory(glfw)
    target_link_libraries(bgui_backend INTERFACE bgui_backend_glfw)
endif()

# NULL backend (fallback)
if(NOT BGUI_USE_GLFW AND NOT BGUI_USE_OPENGL)
    add_subdirectory(null)
    target_link_libraries(bgui_backend INTERFACE bgui_backend_null)
endif()
