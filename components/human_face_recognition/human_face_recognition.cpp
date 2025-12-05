#include "human_face_recognition.hpp"
#include "esp_log.h"

static const char *TAG = "human_face_recognition";

#if CONFIG_HUMAN_FACE_RECOGNITION_MODEL_IN_FLASH_RODATA
extern const uint8_t human_face_recognition_espdl[] asm("_binary_human_face_recognition_espdl_start");
static const char *model_path = (const char *)human_face_recognition_espdl;
#elif CONFIG_HUMAN_FACE_RECOGNITION_MODEL_IN_FLASH_PARTITION
static const char *model_path = "human_face_rec";
#endif

namespace human_face_recognition {

MFN::MFN(const char *model_name)
{
#if !CONFIG_HUMAN_FACE_RECOGNITION_MODEL_IN_SDCARD
    m_model = new dl::Model(
        model_path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_RECOGNITION_MODEL_LOCATION));
#else
    m_model =
        new dl::Model(model_name, static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_RECOGNITION_MODEL_LOCATION));
#endif

#if CONFIG_IDF_TARGET_ESP32P4
    // ESP32-P4: Use RGB_SWAP and RGB565_BIG_ENDIAN as per Waveshare example
    m_image_preprocessor = new dl::image::FeatImagePreprocessor(
        m_model, {127.5, 127.5, 127.5}, {0.0078125, 0.0078125, 0.0078125},
        dl::image::DL_IMAGE_CAP_RGB_SWAP | dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN);
#else
    m_image_preprocessor = new dl::image::FeatImagePreprocessor(
        m_model, {127.5, 127.5, 127.5}, {0.0078125, 0.0078125, 0.0078125}, dl::image::DL_IMAGE_CAP_RGB_SWAP);
#endif
    m_postprocessor = new dl::feat::FeatPostprocessor(m_model);

    ESP_LOGI(TAG, "MFN model loaded: %s", model_name);
}

} // namespace human_face_recognition

HumanFaceFeat::HumanFaceFeat(const char *sdcard_model_dir, model_type_t model_type)
{
    ESP_LOGI(TAG, "Initializing HumanFaceFeat, model_type=%d", (int)model_type);

    switch (model_type) {
    case model_type_t::MFN_S8_V1:
    case model_type_t::MBF_S8_V1:
#if CONFIG_HUMAN_FACE_RECOGNITION_MFN_S8_V1 || CONFIG_HUMAN_FACE_RECOGNITION_MBF_S8_V1
#if !CONFIG_HUMAN_FACE_RECOGNITION_MODEL_IN_SDCARD
        ESP_LOGI(TAG, "Loading MFN model from flash rodata...");
        m_model = new human_face_recognition::MFN("human_face_feat_mfn_s8_v1.espdl");
        if (m_model) {
            ESP_LOGI(TAG, "✅ MFN model loaded successfully!");
        } else {
            ESP_LOGE(TAG, "❌ Failed to create MFN model!");
        }
#else
        if (sdcard_model_dir) {
            char model_path[128];
            snprintf(model_path, sizeof(model_path), "%s/human_face_feat_mfn_s8_v1.espdl", sdcard_model_dir);
            m_model = new human_face_recognition::MFN(model_path);
        } else {
            ESP_LOGE(TAG, "SD card model directory not provided");
        }
#endif
#else
        ESP_LOGE(TAG, "MFN model not enabled in menuconfig");
#endif
        break;
    }
}

HumanFaceRecognizer::HumanFaceRecognizer(const char *db_path,
                                         const char *sdcard_model_dir,
                                         HumanFaceFeat::model_type_t model_type,
                                         bool lazy_load)
    : m_feat_model(nullptr), m_db(nullptr), m_lazy_load(lazy_load)
{
    ESP_LOGI(TAG, "Initializing HumanFaceRecognizer");

    if (!lazy_load) {
        m_feat_model = new HumanFaceFeat(sdcard_model_dir, model_type);
    }

    // Initialize database
    if (db_path != nullptr) {
        m_db = new dl::recognition::DB(db_path, m_feat_model ? m_feat_model->get_feat_len() : 512);
        ESP_LOGI(TAG, "Database initialized: %s, %d faces enrolled", db_path, m_db->get_num_feats());
    } else {
        // In-memory database
        m_db = new dl::recognition::DB(nullptr, m_feat_model ? m_feat_model->get_feat_len() : 512);
        ESP_LOGI(TAG, "In-memory database initialized");
    }
}

HumanFaceRecognizer::~HumanFaceRecognizer()
{
    if (m_feat_model) {
        delete m_feat_model;
        m_feat_model = nullptr;
    }
    if (m_db) {
        delete m_db;
        m_db = nullptr;
    }
}

dl::recognition::result_t *HumanFaceRecognizer::recognize(const dl::image::img_t &img,
                                                          const dl::detect::result_t &detect_result)
{
    if (m_feat_model == nullptr || m_db == nullptr) {
        ESP_LOGE(TAG, "Feature model or database not initialized");
        return nullptr;
    }

    // Extract landmarks from detection result
    std::vector<int> landmarks;
    for (int i = 0; i < 10; i++) {
        landmarks.push_back(detect_result.keypoint[i]);
    }

    // Extract face features
    dl::TensorBase *feat = m_feat_model->run(img, landmarks);
    if (feat == nullptr) {
        ESP_LOGE(TAG, "Failed to extract features");
        return nullptr;
    }

    // Query database
    dl::recognition::result_t *result = m_db->query(feat);

    return result;
}

int HumanFaceRecognizer::enroll(const dl::image::img_t &img,
                                const dl::detect::result_t &detect_result,
                                int id)
{
    if (m_feat_model == nullptr || m_db == nullptr) {
        ESP_LOGE(TAG, "Feature model or database not initialized");
        return -1;
    }

    // Extract landmarks
    std::vector<int> landmarks;
    for (int i = 0; i < 10; i++) {
        landmarks.push_back(detect_result.keypoint[i]);
    }

    // Extract face features
    dl::TensorBase *feat = m_feat_model->run(img, landmarks);
    if (feat == nullptr) {
        ESP_LOGE(TAG, "Failed to extract features for enrollment");
        return -1;
    }

    // Enroll in database
    int enrolled_id = m_db->enroll(feat, id);
    if (enrolled_id >= 0) {
        ESP_LOGI(TAG, "Face enrolled with ID: %d", enrolled_id);
    } else {
        ESP_LOGE(TAG, "Failed to enroll face");
    }

    return enrolled_id;
}

void HumanFaceRecognizer::clear_all_feats()
{
    if (m_db) {
        m_db->clear();
        ESP_LOGI(TAG, "All faces cleared from database");
    }
}

bool HumanFaceRecognizer::delete_feat(int id)
{
    if (m_db) {
        bool success = m_db->remove(id);
        if (success) {
            ESP_LOGI(TAG, "Face ID %d deleted", id);
        }
        return success;
    }
    return false;
}

bool HumanFaceRecognizer::delete_last_feat()
{
    if (m_db) {
        int num = m_db->get_num_feats();
        if (num > 0) {
            return m_db->remove(num - 1);
        }
    }
    return false;
}

int HumanFaceRecognizer::get_num_feats()
{
    if (m_db) {
        return m_db->get_num_feats();
    }
    return 0;
}
