set(src_dirs ".")

set(include_dirs ".")

set(requires esp-dl)

idf_component_register(SRC_DIRS ${src_dirs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES ${requires})

# Pack model files
set(models_dir ${COMPONENT_DIR}/models/p4)
set(packed_model ${BUILD_DIR}/espdl_models/human_face_recognition.espdl)

if(CONFIG_HUMAN_FACE_RECOGNITION_MFN_S8_V1)
    if(CONFIG_HUMAN_FACE_RECOGNITION_MODEL_IN_FLASH_RODATA)
        list(APPEND models ${models_dir}/human_face_feat_mfn_s8_v1.espdl)
    endif()
endif()

if(models)
    set(pack_cmd ${python} ${COMPONENT_DIR}/pack_model.py)
    list(APPEND pack_cmd -i ${models})
    list(APPEND pack_cmd -o ${packed_model})
    add_custom_command(
        OUTPUT ${packed_model}
        COMMAND ${pack_cmd}
        DEPENDS ${models}
        VERBATIM
    )
    add_custom_target(human_face_recognition_model ALL DEPENDS ${packed_model})
    add_dependencies(flash human_face_recognition_model)
    if(CONFIG_HUMAN_FACE_RECOGNITION_MODEL_IN_FLASH_RODATA)
        target_add_binary_data(${COMPONENT_LIB} ${packed_model} BINARY RENAME_TO human_face_recognition_espdl)
    endif()
endif()
