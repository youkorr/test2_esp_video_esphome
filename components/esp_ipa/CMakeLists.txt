set(include_dirs "include")

idf_component_register(SRCS "src/version.c"
                       INCLUDE_DIRS ${include_dirs})

# Note: The prebuilt library lib/${idf_target}/libesp_ipa.a is not present
# in this repository. The component will build without the prebuilt library,
# providing only basic version info functionality.
# ISP pipeline features requiring the prebuilt library will not be available.





