file(GLOB_RECURSE UI_SOURCES "ui/*.c" "ui/*.cpp")

idf_component_register(
    SRCS ${UI_SOURCES}
    INCLUDE_DIRS "ui"         
    REQUIRES lvgl
)