#pragma once

#include "dl_feat_base.hpp"
#include "dl_feat_image_preprocessor.hpp"
#include "dl_feat_postprocessor.hpp"
#include "dl_recognition_database.hpp"
#include "dl_detect_define.hpp"

namespace human_face_recognition {

// MFN (MobileFaceNet) feature extractor
class MFN : public dl::feat::FeatImpl {
public:
    MFN(const char *model_name);
};

// Alias for MBF (same as MFN)
using MBF = MFN;

} // namespace human_face_recognition

// Feature extractor wrapper class
class HumanFaceFeat : public dl::feat::FeatWrapper {
public:
    typedef enum { MFN_S8_V1, MBF_S8_V1 } model_type_t;
    HumanFaceFeat(const char *sdcard_model_dir = nullptr,
                  model_type_t model_type = MFN_S8_V1);
protected:
    void load_model() override {} // Model loaded in constructor
};

// Main face recognizer class
class HumanFaceRecognizer {
private:
    HumanFaceFeat *m_feat_model;
    dl::recognition::DataBase *m_db;
    bool m_lazy_load;

public:
    /**
     * @brief Constructor
     * @param db_path Path to database file (Flash partition or SD card)
     * @param sdcard_model_dir Path to model on SD card (optional)
     * @param model_type Model type to use
     * @param lazy_load If true, delay model loading until first use
     */
    HumanFaceRecognizer(const char *db_path = nullptr,
                        const char *sdcard_model_dir = nullptr,
                        HumanFaceFeat::model_type_t model_type = HumanFaceFeat::MFN_S8_V1,
                        bool lazy_load = false);
    ~HumanFaceRecognizer();

    /**
     * @brief Recognize a face in the image
     * @param img Input image
     * @param detect_result Detection result with face box and landmarks
     * @return Recognition result with ID and similarity score, or nullptr if not found
     */
    dl::recognition::result_t *recognize(const dl::image::img_t &img,
                                         const dl::detect::result_t &detect_result);

    /**
     * @brief Enroll a new face into the database
     * @param img Input image
     * @param detect_result Detection result with face box and landmarks
     * @param id Optional custom ID (auto-generated if not provided)
     * @return Enrolled feature ID, or -1 on failure
     */
    int enroll(const dl::image::img_t &img,
               const dl::detect::result_t &detect_result,
               int id = -1);

    /**
     * @brief Clear all enrolled faces from database
     */
    void clear_all_feats();

    /**
     * @brief Delete a specific enrolled face
     * @param id ID of the face to delete
     * @return true if successful
     */
    bool delete_feat(int id);

    /**
     * @brief Delete the last enrolled face
     * @return true if successful
     */
    bool delete_last_feat();

    /**
     * @brief Get number of enrolled faces
     * @return Number of faces in database
     */
    int get_num_feats();

    /**
     * @brief Get the feature extraction model
     * @return Pointer to feature model
     */
    HumanFaceFeat *get_feat_model() { return m_feat_model; }
};
