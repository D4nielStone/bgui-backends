add_library(bgui_backend_vulkan STATIC bgui_backend_vulkan.cpp)

target_compile_definitions(bgui_backend_vulkan PUBLIC BGUI_USE_VULKAN)

find_package(Vulkan REQUIRED)
target_link_libraries(bgui_backend_vulkan PUBLIC  Vulkan::Vulkan bgui_core)
target_include_directories(bgui_backend_vulkan
    PUBLIC ${PROJECT_SOURCE_DIR}/backend
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/core/include
)