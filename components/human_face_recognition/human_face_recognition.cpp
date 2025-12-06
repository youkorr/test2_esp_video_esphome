#include "human_face_recognition.hpp"
#include "esp_log.h"

static const char *TAG = "human_face_recognition";

// Feature length for MobileFaceNet
#define MFN_FEAT_LEN 512

#if CONFIG_HUMAN_FACE_FEAT_MODEL_IN_FLASH_RODATA
extern const uint8_t human_face_feat_espdl[] asm("_binary_human_face_feat_mfn_s8_v1_espdl_start");
static const char *model_data = (const char *)human_face_feat_espdl;
#endif

namespace human_face_recognition {

MFN::MFN(const char *model_name)
{
#if CONFIG_HUMAN_FACE_FEAT_MODEL_IN_FLASH_RODATA
    m_model = new dl::Model(
        model_data, model_name, fbs::MODEL_LOCATION_IN_FLASH_RODATA);
#else
    m_model = new dl::Model(model_name, fbs::MODEL_LOCATION_IN_FLASH_PARTITION);
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
#if CONFIG_HUMAN_FACE_FEAT_MFN_S8_V1
        if (sdcard_model_dir) {
            char model_path[128];
            snprintf(model_path, sizeof(model_path), "%s/human_face_feat_mfn_s8_v1.espdl", sdcard_model_dir);
            ESP_LOGI(TAG, "Loading MFN model from SD card: %s", model_path);
            m_model = new human_face_recognition::MFN(model_path);
        } else {
            ESP_LOGI(TAG, "Loading MFN model from flash rodata...");
            m_model = new human_face_recognition::MFN("human_face_feat_mfn_s8_v1");
        }
        if (m_model) {
            ESP_LOGI(TAG, "MFN model loaded successfully!");
        } else {
            ESP_LOGE(TAG, "Failed to create MFN model!");
        }
#else
        ESP_LOGE(TAG, "MFN model not enabled - define CONFIG_HUMAN_FACE_FEAT_MFN_S8_V1");
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

    // Initialize database with path and feature length
    int feat_len = m_feat_model ? m_feat_model->get_feat_len() : MFN_FEAT_LEN;

    if (db_path != nullptr) {
        ESP_LOGI(TAG, "Creating database at: %s (feat_len=%d)", db_path, feat_len);
        m_db = new dl::recognition::DataBase(db_path, feat_len);
        ESP_LOGI(TAG, "Database initialized: %d faces enrolled", m_db->get_num_feats());
    } else {
        // In-memory database (empty path)
        ESP_LOGI(TAG, "Creating in-memory database (feat_len=%d)", feat_len);
        m_db = new dl::recognition::DataBase("", feat_len);
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

    if (m_db->get_num_feats() == 0) {
        // No faces enrolled
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

    // Query database - get top 1 match with threshold 0.0 (we'll check threshold in caller)
    std::vector<dl::recognition::result_t> results = m_db->query_feat(feat, 0.0f, 1);

    // Return best match if found
    static dl::recognition::result_t best_result;
    if (!results.empty()) {
        best_result = results[0];
        return &best_result;
    }

    return nullptr;
}

int HumanFaceRecognizer::enroll(const dl::image::img_t &img,
                                const dl::detect::result_t &detect_result,
                                int id)
{
    (void)id;  // ID is auto-generated by database

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

    // Get current count before enrollment
    int count_before = m_db->get_num_feats();

    // Enroll in database
    esp_err_t err = m_db->enroll_feat(feat);
    if (err == ESP_OK) {
        int new_id = m_db->get_num_feats() - 1;  // New ID is count - 1
        ESP_LOGI(TAG, "Face enrolled with ID: %d (total: %d)", new_id, m_db->get_num_feats());
        return new_id;
    } else {
        ESP_LOGE(TAG, "Failed to enroll face: %s", esp_err_to_name(err));
        return -1;
    }
}

void HumanFaceRecognizer::clear_all_feats()
{
    if (m_db) {
        esp_err_t err = m_db->clear_all_feats();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "All faces cleared from database");
        } else {
            ESP_LOGE(TAG, "Failed to clear database: %s", esp_err_to_name(err));
        }
    }
}

bool HumanFaceRecognizer::delete_feat(int id)
{
    if (m_db) {
        esp_err_t err = m_db->delete_feat((uint16_t)id);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Face ID %d deleted", id);
            return true;
        } else {
            ESP_LOGE(TAG, "Failed to delete face ID %d: %s", id, esp_err_to_name(err));
        }
    }
    return false;
}

bool HumanFaceRecognizer::delete_last_feat()
{
    if (m_db) {
        esp_err_t err = m_db->delete_last_feat();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Last face deleted");
            return true;
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
