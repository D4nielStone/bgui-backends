add_library(bgui_backend_null STATIC bgui_backend_null.cpp)

target_compile_definitions(bgui_backend_null PUBLIC BGUI_USE_NULL)

target_include_directories(bgui_backend_null
    PUBLIC ${PROJECT_SOURCE_DIR}/backend
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/core/include
)