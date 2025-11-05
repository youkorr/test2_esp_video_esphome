# ESP Camera Sensor Component

file(GLOB_RECURSE component_srcs
    "src/*.c"
    "sensor/*/*.c"
)

set(component_include_dirs
    "include"
    "sensor"
    "src"
)

set(component_requires
    driver
    esp_driver_cam
)

idf_component_register(
    SRCS ${component_srcs}
    INCLUDE_DIRS ${component_include_dirs}
    REQUIRES ${component_requires}
)

message(STATUS "ESP Camera Sensor component registered")
