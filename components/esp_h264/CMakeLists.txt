# ESP H.264 Encoder Component

file(GLOB_RECURSE component_srcs
    "port/src/*.c"
    "sw/src/*.c"
    "interface/include/src/*.c"
)

set(component_include_dirs
    "interface/include"
    "port/include"
    "sw/include"
)

set(component_requires
    driver
)

idf_component_register(
    SRCS ${component_srcs}
    INCLUDE_DIRS ${component_include_dirs}
    REQUIRES ${component_requires}
)

message(STATUS "ESP H.264 component registered")
