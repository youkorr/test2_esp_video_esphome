# ESP SCCB Interface Component

file(GLOB_RECURSE component_srcs
    "src/*.c"
)

set(component_include_dirs
    "include"
)

set(component_requires
    driver
)

idf_component_register(
    SRCS ${component_srcs}
    INCLUDE_DIRS ${component_include_dirs}
    REQUIRES ${component_requires}
)

message(STATUS "ESP SCCB Interface component registered")
