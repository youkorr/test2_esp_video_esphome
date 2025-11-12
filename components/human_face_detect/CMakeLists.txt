# CMakeLists.txt for human_face_detect component
# Embeds face detection models in SPIFFS partition

idf_component_register(
    SRCS "human_face_detect.cpp" "human_face_detect_espdl.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_psram spiffs esp-dl
)

# Create SPIFFS image with embedded models
set(MODEL_DIR "${COMPONENT_DIR}/models/p4")

# Check if models exist
if(EXISTS "${MODEL_DIR}/human_face_detect_msr_s8_v1.espdl" AND 
   EXISTS "${MODEL_DIR}/human_face_detect_mnp_s8_v1.espdl")
    
    message(STATUS "Found face detection models in ${MODEL_DIR}")
    
    # Create SPIFFS filesystem image containing models
    spiffs_create_partition_image(spiffs "${MODEL_DIR}" FLASH_IN_PROJECT)
    
    message(STATUS "Face detection models will be embedded in SPIFFS partition")
else()
    message(WARNING "Face detection models not found in ${MODEL_DIR}")
    message(WARNING "Face detection will be disabled at runtime")
endif()
