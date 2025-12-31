add_library(bgui_backend_freetype STATIC bgui_backend_freetype.cpp)

target_compile_definitions(bgui_backend_freetype PUBLIC BGUI_USE_FREETYPE)

find_package(Freetype REQUIRED)
target_link_libraries(bgui_backend_freetype PUBLIC Freetype::Freetype bgui_core)
target_include_directories(bgui_backend_freetype
    PUBLIC ${PROJECT_SOURCE_DIR}/backend/
    ${PROJECT_SOURCE_DIR}/core/include
)