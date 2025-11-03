#include "mipi_dsi_cam.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include "mipi_dsi_cam_drivers_generated.h"

#ifdef USE_ESP32_VARIANT_ESP32P4

namespace esphome {
namespace mipi_dsi_cam {

static const char *const TAG = "mipi_dsi_cam";

void MipiDsiCam::setup() {
  ESP_LOGI(TAG, "Init MIPI Camera with IPA");
  ESP_LOGI(TAG, "  Sensor type: %s", this->sensor_type_.c_str());
  
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(false);
    delay(10);
    this->reset_pin_->digital_write(true);
    delay(20);
  }
  
  if (!this->create_sensor_driver_()) {
    ESP_LOGE(TAG, "Driver creation failed");
    this->mark_failed();
    return;
  }
  
  if (!this->init_sensor_()) {
    ESP_LOGE(TAG, "Sensor init failed");
    this->mark_failed();
    return;
  }
  
  if (!this->init_ldo_()) {
    ESP_LOGE(TAG, "LDO init failed");
    this->mark_failed();
    return;
  }
  
  if (!this->init_csi_()) {
    ESP_LOGE(TAG, "CSI init failed");
    this->mark_failed();
    return;
  }
  
  if (!this->init_isp_()) {
    ESP_LOGE(TAG, "ISP init failed");
    this->mark_failed();
    return;
  }
  
  if (!this->init_isp_modules_()) {
    ESP_LOGE(TAG, "ISP modules init failed");
    this->mark_failed();
    return;
  }
  
  if (!this->init_ipa_()) {
    ESP_LOGE(TAG, "IPA init failed");
    this->mark_failed();
    return;
  }
  
  if (!this->allocate_buffer_()) {
    ESP_LOGE(TAG, "Buffer alloc failed");
    this->mark_failed();
    return;
  }
  
  this->initialized_ = true;
  ESP_LOGI(TAG, "Camera ready (%ux%u) with IPA enabled", this->width_, this->height_);
}

bool MipiDsiCam::create_sensor_driver_() {
  ESP_LOGI(TAG, "Creating driver for: %s", this->sensor_type_.c_str());
  
  this->sensor_driver_ = create_sensor_driver(this->sensor_type_, this);
  
  if (this->sensor_driver_ == nullptr) {
    ESP_LOGE(TAG, "Unknown or unavailable sensor: %s", this->sensor_type_.c_str());
    return false;
  }
  
  ESP_LOGI(TAG, "Driver created for: %s", this->sensor_driver_->get_name());
  return true;
}

bool MipiDsiCam::init_sensor_() {
  if (!this->sensor_driver_) {
    ESP_LOGE(TAG, "No sensor driver");
    return false;
  }
  
  ESP_LOGI(TAG, "Init sensor: %s", this->sensor_driver_->get_name());
  
  this->width_ = this->sensor_driver_->get_width();
  this->height_ = this->sensor_driver_->get_height();
  this->lane_count_ = this->sensor_driver_->get_lane_count();
  this->bayer_pattern_ = this->sensor_driver_->get_bayer_pattern();
  this->lane_bitrate_mbps_ = this->sensor_driver_->get_lane_bitrate_mbps();
  
  ESP_LOGI(TAG, "  Resolution: %ux%u", this->width_, this->height_);
  ESP_LOGI(TAG, "  Lanes: %u", this->lane_count_);
  ESP_LOGI(TAG, "  Bayer: %u", this->bayer_pattern_);
  ESP_LOGI(TAG, "  Bitrate: %u Mbps", this->lane_bitrate_mbps_);
  
  uint16_t pid = 0;
  esp_err_t ret = this->sensor_driver_->read_id(&pid);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read sensor ID");
    return false;
  }
  
  if (pid != this->sensor_driver_->get_pid()) {
    ESP_LOGE(TAG, "Wrong PID: 0x%04X (expected 0x%04X)", 
             pid, this->sensor_driver_->get_pid());
    return false;
  }
  
  ESP_LOGI(TAG, "Sensor ID: 0x%04X", pid);
  
  ret = this->sensor_driver_->init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Sensor init failed: %d", ret);
    return false;
  }
  
  ESP_LOGI(TAG, "Sensor initialized");
  
  delay(200);
  ESP_LOGI(TAG, "Sensor stabilized");
  
  return true;
}

bool MipiDsiCam::init_ldo_() {
  ESP_LOGI(TAG, "Init LDO MIPI");
  
  esp_ldo_channel_config_t ldo_config = {
    .chan_id = 3,
    .voltage_mv = 2500,
  };
  
  esp_err_t ret = esp_ldo_acquire_channel(&ldo_config, &this->ldo_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "LDO failed: %d", ret);
    return false;
  }
  
  ESP_LOGI(TAG, "LDO OK (2.5V)");
  return true;
}

bool MipiDsiCam::init_csi_() {
  ESP_LOGI(TAG, "Init MIPI-CSI");
  
  esp_cam_ctlr_csi_config_t csi_config = {};
  csi_config.ctlr_id = 0;
  csi_config.clk_src = MIPI_CSI_PHY_CLK_SRC_DEFAULT;
  csi_config.h_res = this->width_;
  csi_config.v_res = this->height_;
  csi_config.lane_bit_rate_mbps = this->lane_bitrate_mbps_;
  csi_config.input_data_color_type = CAM_CTLR_COLOR_RAW8;
  csi_config.output_data_color_type = CAM_CTLR_COLOR_RGB565;
  csi_config.data_lane_num = this->lane_count_;
  csi_config.byte_swap_en = false;
  csi_config.queue_items = 10;
  
  esp_err_t ret = esp_cam_new_csi_ctlr(&csi_config, &this->csi_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "CSI failed: %d", ret);
    return false;
  }
  
  esp_cam_ctlr_evt_cbs_t callbacks = {
    .on_get_new_trans = MipiDsiCam::on_csi_new_frame_,
    .on_trans_finished = MipiDsiCam::on_csi_frame_done_,
  };
  
  ret = esp_cam_ctlr_register_event_callbacks(this->csi_handle_, &callbacks, this);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Callbacks failed: %d", ret);
    return false;
  }
  
  ret = esp_cam_ctlr_enable(this->csi_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Enable CSI failed: %d", ret);
    return false;
  }
  
  ESP_LOGI(TAG, "CSI OK");
  return true;
}

bool MipiDsiCam::init_isp_() {
  ESP_LOGI(TAG, "Init ISP");
  
  uint32_t isp_clock_hz = 120000000;
  
  esp_isp_processor_cfg_t isp_config = {};
  isp_config.clk_src = ISP_CLK_SRC_DEFAULT;
  isp_config.input_data_source = ISP_INPUT_DATA_SOURCE_CSI;
  isp_config.input_data_color_type = ISP_COLOR_RAW8;
  isp_config.output_data_color_type = ISP_COLOR_RGB565;
  isp_config.h_res = this->width_;
  isp_config.v_res = this->height_;
  isp_config.has_line_start_packet = false;
  isp_config.has_line_end_packet = false;
  isp_config.clk_hz = isp_clock_hz;
  isp_config.bayer_order = (color_raw_element_order_t)this->bayer_pattern_;
  
  esp_err_t ret = esp_isp_new_processor(&isp_config, &this->isp_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ISP creation failed: 0x%x", ret);
    return false;
  }
  
  ret = esp_isp_enable(this->isp_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ISP enable failed: 0x%x", ret);
    esp_isp_del_processor(this->isp_handle_);
    this->isp_handle_ = nullptr;
    return false;
  }
  
  ESP_LOGI(TAG, "ISP OK");
  return true;
}

bool MipiDsiCam::init_isp_modules_() {
  ESP_LOGI(TAG, "Init ISP modules for IPA");
  esp_err_t ret;
  
  // AWB Controller
  ret = esp_isp_new_awb_controller(this->isp_handle_, &this->awb_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "AWB controller creation failed: 0x%x", ret);
    return false;
  }
  
  isp_awb_config_t awb_config = {
    .sample_point = ISP_AWB_SAMPLE_POINT_AFTER_CCM,
  };
  ret = esp_isp_awb_configure(this->awb_ctlr_, &awb_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "AWB configure failed: 0x%x", ret);
    return false;
  }
  
  ret = esp_isp_awb_enable(this->awb_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "AWB enable failed: 0x%x", ret);
    return false;
  }
  ESP_LOGI(TAG, "  AWB OK");
  
  // AE Controller
  ret = esp_isp_new_ae_controller(this->isp_handle_, &this->ae_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "AE controller creation failed: 0x%x", ret);
    return false;
  }
  
  isp_ae_config_t ae_config = {
    .sample_point = ISP_AE_SAMPLE_POINT_AFTER_DEMOSAIC,
  };
  ret = esp_isp_ae_configure(this->ae_ctlr_, &ae_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "AE configure failed: 0x%x", ret);
    return false;
  }
  
  ret = esp_isp_ae_enable(this->ae_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "AE enable failed: 0x%x", ret);
    return false;
  }
  ESP_LOGI(TAG, "  AE OK");
  
  // Histogram Controller
  ret = esp_isp_new_hist_controller(this->isp_handle_, &this->hist_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Histogram controller creation failed: 0x%x", ret);
    return false;
  }
  
  isp_hist_config_t hist_config = {
    .sample_point = ISP_HIST_SAMPLE_POINT_AFTER_CCM,
    .mode = ISP_HIST_SAMPLING_RGB,
    .rgb_coefficient.coeff_r = 0,
    .rgb_coefficient.coeff_g = 0,
    .rgb_coefficient.coeff_b = 0,
  };
  ret = esp_isp_hist_configure(this->hist_ctlr_, &hist_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Histogram configure failed: 0x%x", ret);
    return false;
  }
  
  ret = esp_isp_hist_enable(this->hist_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Histogram enable failed: 0x%x", ret);
    return false;
  }
  ESP_LOGI(TAG, "  Histogram OK");
  
  // Sharpen Controller
  ret = esp_isp_new_sharpen_controller(this->isp_handle_, &this->sharpen_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Sharpen controller creation failed: 0x%x", ret);
    return false;
  }
  
  ret = esp_isp_sharpen_enable(this->sharpen_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Sharpen enable failed: 0x%x", ret);
    return false;
  }
  ESP_LOGI(TAG, "  Sharpen OK");
  
  // BF (Bayer Filter / Denoise) Controller
  ret = esp_isp_new_bf_controller(this->isp_handle_, &this->bf_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "BF controller creation failed: 0x%x", ret);
    return false;
  }
  
  ret = esp_isp_bf_enable(this->bf_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "BF enable failed: 0x%x", ret);
    return false;
  }
  ESP_LOGI(TAG, "  BF (Denoise) OK");
  
  // CCM (Color Correction Matrix) Controller
  ret = esp_isp_new_ccm_controller(this->isp_handle_, &this->ccm_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "CCM controller creation failed: 0x%x", ret);
    return false;
  }
  
  ret = esp_isp_ccm_enable(this->ccm_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "CCM enable failed: 0x%x", ret);
    return false;
  }
  ESP_LOGI(TAG, "  CCM OK");
  
  // Gamma Controller
  ret = esp_isp_new_gamma_controller(this->isp_handle_, &this->gamma_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Gamma controller creation failed: 0x%x", ret);
    return false;
  }
  
  ret = esp_isp_gamma_enable(this->gamma_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Gamma enable failed: 0x%x", ret);
    return false;
  }
  ESP_LOGI(TAG, "  Gamma OK");
  
  // Demosaic Controller
  ret = esp_isp_new_demosaic_controller(this->isp_handle_, &this->demosaic_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Demosaic controller creation failed: 0x%x", ret);
    return false;
  }
  
  ret = esp_isp_demosaic_enable(this->demosaic_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Demosaic enable failed: 0x%x", ret);
    return false;
  }
  ESP_LOGI(TAG, "  Demosaic OK");
  
  // Color Controller (brightness, contrast, saturation, hue)
  ret = esp_isp_new_color_controller(this->isp_handle_, &this->color_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Color controller creation failed: 0x%x", ret);
    return false;
  }
  
  ret = esp_isp_color_enable(this->color_ctlr_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Color enable failed: 0x%x", ret);
    return false;
  }
  ESP_LOGI(TAG, "  Color OK");
  
  ESP_LOGI(TAG, "All ISP modules initialized");
  return true;
}

bool MipiDsiCam::init_ipa_() {
  ESP_LOGI(TAG, "Init IPA (Image Processing Algorithm)");
  
  // Préparer les informations du capteur pour l'IPA
  this->ipa_sensor_info_.width = this->width_;
  this->ipa_sensor_info_.height = this->height_;
  
  // Valeurs d'exposition typiques (à adapter selon le capteur)
  this->ipa_sensor_info_.max_exposure = 100000;  // 100ms
  this->ipa_sensor_info_.min_exposure = 100;     // 0.1ms
  this->ipa_sensor_info_.cur_exposure = 10000;   // 10ms
  this->ipa_sensor_info_.step_exposure = 100;    // 0.1ms step
  
  // Valeurs de gain typiques
  this->ipa_sensor_info_.max_gain = 16.0f;
  this->ipa_sensor_info_.min_gain = 1.0f;
  this->ipa_sensor_info_.cur_gain = 2.0f;
  this->ipa_sensor_info_.step_gain = 0.1f;
  
  // Initialiser l'IPA avec configuration par défaut
  esp_err_t ret = this->ipa_.init(&this->ipa_sensor_info_, &this->ipa_metadata_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "IPA init failed: 0x%x", ret);
    return false;
  }
  
  // Appliquer les métadonnées initiales
  if (!this->apply_ipa_metadata_(&this->ipa_metadata_)) {
    ESP_LOGE(TAG, "Failed to apply initial IPA metadata");
    return false;
  }
  
  this->ipa_initialized_ = true;
  
  // Afficher la configuration IPA
  IPAConfig &config = this->ipa_.get_config();
  ESP_LOGI(TAG, "IPA Configuration:");
  ESP_LOGI(TAG, "  AWB: %s", config.awb_enabled ? "ON" : "OFF");
  ESP_LOGI(TAG, "  AE: %s", config.ae_enabled ? "ON" : "OFF");
  ESP_LOGI(TAG, "  Sharpen: %s (strength: %u)", config.sharpen_enabled ? "ON" : "OFF", config.sharpen_strength);
  ESP_LOGI(TAG, "  Denoise: %s (level: %u)", config.denoise_enabled ? "ON" : "OFF", config.denoise_level);
  ESP_LOGI(TAG, "  CCM: %s", config.ccm_enabled ? "ON" : "OFF");
  ESP_LOGI(TAG, "  Gamma: %s (%.2f)", config.gamma_enabled ? "ON" : "OFF", config.gamma_value);
  
  ESP_LOGI(TAG, "IPA initialized successfully");
  return true;
}

bool MipiDsiCam::allocate_buffer_() {
  this->frame_buffer_size_ = this->width_ * this->height_ * 2;
  
  this->frame_buffers_[0] = (uint8_t*)heap_caps_aligned_alloc(
    64, this->frame_buffer_size_, MALLOC_CAP_SPIRAM
  );
  
  this->frame_buffers_[1] = (uint8_t*)heap_caps_aligned_alloc(
    64, this->frame_buffer_size_, MALLOC_CAP_SPIRAM
  );
  
  if (!this->frame_buffers_[0] || !this->frame_buffers_[1]) {
    ESP_LOGE(TAG, "Buffer alloc failed");
    return false;
  }
  
  this->current_frame_buffer_ = this->frame_buffers_[0];
  
  ESP_LOGI(TAG, "Buffers: 2x%u bytes", this->frame_buffer_size_);
  return true;
}

bool IRAM_ATTR MipiDsiCam::on_csi_new_frame_(
  esp_cam_ctlr_handle_t handle,
  esp_cam_ctlr_trans_t *trans,
  void *user_data
) {
  MipiDsiCam *cam = (MipiDsiCam*)user_data;
  trans->buffer = cam->frame_buffers_[cam->buffer_index_];
  trans->buflen = cam->frame_buffer_size_;
  return false;
}

bool IRAM_ATTR MipiDsiCam::on_csi_frame_done_(
  esp_cam_ctlr_handle_t handle,
  esp_cam_ctlr_trans_t *trans,
  void *user_data
) {
  MipiDsiCam *cam = (MipiDsiCam*)user_data;
  
  if (trans->received_size > 0) {
    cam->frame_ready_ = true;
    cam->buffer_index_ = (cam->buffer_index_ + 1) % 2;
    cam->total_frames_received_++;
  }
  
  return false;
}

bool MipiDsiCam::get_isp_statistics_(esp_ipa_stats_t *stats) {
  if (!stats || !this->ipa_initialized_) {
    return false;
  }
  
  memset(stats, 0, sizeof(esp_ipa_stats_t));
  stats->seq = this->total_frames_received_;
  stats->flags = 0;
  
  esp_err_t ret;
  
  // AWB Statistics
  if (this->awb_ctlr_) {
    isp_awb_stat_result_t awb_result;
    ret = esp_isp_awb_get_statistics(this->awb_ctlr_, 0, &awb_result);
    if (ret == ESP_OK) {
      stats->awb_stats[0].counted = awb_result.white_patch_num;
      stats->awb_stats[0].sum_r = awb_result.sum_r;
      stats->awb_stats[0].sum_g = awb_result.sum_g;
      stats->awb_stats[0].sum_b = awb_result.sum_b;
      stats->flags |= IPA_STATS_FLAGS_AWB;
    }
  }
  
  // AE Statistics
  if (this->ae_ctlr_) {
    isp_ae_result_t ae_result;
    ret = esp_isp_ae_get_statistics(this->ae_ctlr_, 0, &ae_result);
    if (ret == ESP_OK) {
      for (int i = 0; i < ISP_AE_REGIONS && i < ISP_AE_BLOCK_X_NUM * ISP_AE_BLOCK_Y_NUM; i++) {
        stats->ae_stats[i].luminance = ae_result.luminance[i];
      }
      stats->flags |= IPA_STATS_FLAGS_AE;
    }
  }
  
  // Histogram Statistics
  if (this->hist_ctlr_) {
    isp_hist_result_t hist_result;
    ret = esp_isp_hist_get_statistics(this->hist_ctlr_, 0, &hist_result);
    if (ret == ESP_OK) {
      for (int i = 0; i < ISP_HIST_SEGMENT_NUMS; i++) {
        stats->hist_stats[i].value = hist_result.hist_value[i];
      }
      stats->flags |= IPA_STATS_FLAGS_HIST;
    }
  }
  
  // Sharpen Statistics
  if (this->sharpen_ctlr_) {
    isp_sharpen_hist_result_t sharpen_result;
    ret = esp_isp_sharpen_get_histogram(this->sharpen_ctlr_, 0, &sharpen_result);
    if (ret == ESP_OK) {
      // Trouver la valeur max dans l'histogramme sharpen
      uint8_t max_val = 0;
      for (int i = 0; i < ISP_SHARPEN_HIST_INTERVAL_NUMS; i++) {
        if (sharpen_result.hist_value[i] > max_val) {
          max_val = (uint8_t)sharpen_result.hist_value[i];
        }
      }
      stats->sharpen_stats.value = max_val;
      stats->flags |= IPA_STATS_FLAGS_SHARPEN;
    }
  }
  
  return (stats->flags != 0);
}

bool MipiDsiCam::apply_ipa_metadata_(const esp_ipa_metadata_t *metadata) {
  if (!metadata || !this->ipa_initialized_) {
    return false;
  }
  
  esp_err_t ret;
  
  // Appliquer AWB (Auto White Balance)
  if ((metadata->flags & IPA_METADATA_FLAGS_RG) && (metadata->flags & IPA_METADATA_FLAGS_BG)) {
    if (this->awb_ctlr_) {
      isp_awb_gain_t gains = {
        .gain_r = (uint32_t)(metadata->red_gain * 256),
        .gain_gr = 256,
        .gain_gb = 256,
        .gain_b = (uint32_t)(metadata->blue_gain * 256),
      };
      ret = esp_isp_awb_set_gain(this->awb_ctlr_, &gains);
      if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set AWB gains: 0x%x", ret);
      }
    }
  }
  
  // Appliquer AE (Auto Exposure) - sur le capteur
  if (this->sensor_driver_) {
    if (metadata->flags & IPA_METADATA_FLAGS_ET) {
      ret = this->sensor_driver_->set_exposure(metadata->exposure);
      if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set exposure: 0x%x", ret);
      }
    }
    
    if (metadata->flags & IPA_METADATA_FLAGS_GN) {
      // Convertir le gain en index pour le capteur
      uint32_t gain_index = (uint32_t)(metadata->gain * 10.0f);
      ret = this->sensor_driver_->set_gain(gain_index);
      if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set gain: 0x%x", ret);
      }
    }
  }
  
  // Appliquer BF (Bayer Filter / Denoise)
  if ((metadata->flags & IPA_METADATA_FLAGS_BF) && this->bf_ctlr_) {
    isp_bf_config_t bf_config;
    bf_config.denoising_level = metadata->bf.level;
    memcpy(bf_config.matrix, metadata->bf.matrix, sizeof(bf_config.matrix));
    ret = esp_isp_bf_configure(this->bf_ctlr_, &bf_config);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to configure BF: 0x%x", ret);
    }
  }
  
  // Appliquer Demosaic
  if ((metadata->flags & IPA_METADATA_FLAGS_DM) && this->demosaic_ctlr_) {
    isp_demosaic_config_t demosaic_config;
    demosaic_config.grad_ratio = metadata->demosaic.gradient_ratio;
    ret = esp_isp_demosaic_configure(this->demosaic_ctlr_, &demosaic_config);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to configure demosaic: 0x%x", ret);
    }
  }
  
  // Appliquer Sharpen
  if ((metadata->flags & IPA_METADATA_FLAGS_SH) && this->sharpen_ctlr_) {
    isp_sharpen_config_t sharpen_config;
    sharpen_config.h_thresh = metadata->sharpen.h_thresh;
    sharpen_config.l_thresh = metadata->sharpen.l_thresh;
    sharpen_config.h_coeff = metadata->sharpen.h_coeff;
    sharpen_config.m_coeff = metadata->sharpen.m_coeff;
    memcpy(sharpen_config.matrix, metadata->sharpen.matrix, sizeof(sharpen_config.matrix));
    ret = esp_isp_sharpen_configure(this->sharpen_ctlr_, &sharpen_config);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to configure sharpen: 0x%x", ret);
    }
  }
  
  // Appliquer Gamma
  if ((metadata->flags & IPA_METADATA_FLAGS_GAMMA) && this->gamma_ctlr_) {
    isp_gamma_curve_points_t gamma_pts;
    for (int i = 0; i < ISP_GAMMA_CURVE_POINTS_NUM; i++) {
      gamma_pts.pts[i].x = metadata->gamma.x[i];
      gamma_pts.pts[i].y = metadata->gamma.y[i];
    }
    ret = esp_isp_gamma_set_curve(this->gamma_ctlr_, &gamma_pts);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to set gamma curve: 0x%x", ret);
    }
  }
  
  // Appliquer CCM (Color Correction Matrix)
  if ((metadata->flags & IPA_METADATA_FLAGS_CCM) && this->ccm_ctlr_) {
    isp_ccm_config_t ccm_config;
    memcpy(ccm_config.matrix, metadata->ccm.matrix, sizeof(ccm_config.matrix));
    ccm_config.saturation = 1;
    ret = esp_isp_ccm_configure(this->ccm_ctlr_, &ccm_config);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to configure CCM: 0x%x", ret);
    }
  }
  
  // Appliquer Color adjustments (brightness, contrast, saturation, hue)
  if (this->color_ctlr_) {
    isp_color_config_t color_config = {};
    
    if (metadata->flags & IPA_METADATA_FLAGS_BR) {
      color_config.brightness = (int)(metadata->brightness - 50) * 2;  // -100 à +100
    }
    if (metadata->flags & IPA_METADATA_FLAGS_CN) {
      color_config.contrast = (int)(metadata->contrast - 50) * 2;  // -100 à +100
    }
    if (metadata->flags & IPA_METADATA_FLAGS_ST) {
      color_config.saturation = metadata->saturation * 2;  // 0 à 200
    }
    if (metadata->flags & IPA_METADATA_FLAGS_HUE) {
      color_config.hue = metadata->hue;  // -180 à +180
    }
    
    ret = esp_isp_color_configure(this->color_ctlr_, &color_config);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to configure color: 0x%x", ret);
    }
  }
  
  return true;
}

void MipiDsiCam::process_ipa_() {
  if (!this->ipa_initialized_ || !this->streaming_) {
    return;
  }
  
  uint32_t now = millis();
  if (now - this->last_ipa_process_time_ < this->ipa_process_interval_ms_) {
    return;  // Pas encore le moment de traiter l'IPA
  }
  this->last_ipa_process_time_ = now;
  
  // Récupérer les statistiques ISP
  esp_ipa_stats_t stats;
  if (!this->get_isp_statistics_(&stats)) {
    ESP_LOGW(TAG, "Failed to get ISP statistics");
    return;
  }
  
  // Traiter avec l'IPA
  this->ipa_.process(&stats, &this->ipa_sensor_info_, &this->ipa_metadata_);
  
  // Appliquer les nouvelles métadonnées
  if (!this->apply_ipa_metadata_(&this->ipa_metadata_)) {
    ESP_LOGW(TAG, "Failed to apply IPA metadata");
  }
}

bool MipiDsiCam::start_streaming() {
  if (!this->initialized_ || this->streaming_) {
    return false;
  }
  
  ESP_LOGI(TAG, "Start streaming with IPA");
  
  this->total_frames_received_ = 0;
  this->last_frame_log_time_ = millis();
  this->last_ipa_process_time_ = millis();
  
  if (this->sensor_driver_) {
    esp_err_t ret = this->sensor_driver_->start_stream();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Sensor start failed: %d", ret);
      return false;
    }
    delay(100);
  }
  
  esp_err_t ret = esp_cam_ctlr_start(this->csi_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "CSI start failed: %d", ret);
    return false;
  }
  
  this->streaming_ = true;
  ESP_LOGI(TAG, "Streaming active with IPA");
  return true;
}

bool MipiDsiCam::stop_streaming() {
  if (!this->streaming_) {
    return true;
  }
  
  esp_cam_ctlr_stop(this->csi_handle_);
  
  if (this->sensor_driver_) {
    this->sensor_driver_->stop_stream();
  }
  
  this->streaming_ = false;
  ESP_LOGI(TAG, "Streaming stopped");
  return true;
}

bool MipiDsiCam::capture_frame() {
  if (!this->streaming_) {
    return false;
  }
  
  bool was_ready = this->frame_ready_;
  if (was_ready) {
    this->frame_ready_ = false;
    uint8_t last_buffer = (this->buffer_index_ + 1) % 2;
    this->current_frame_buffer_ = this->frame_buffers_[last_buffer];
  }
  
  return was_ready;
}

void MipiDsiCam::loop() {
  if (this->streaming_) {
    // Traiter l'IPA périodiquement
    this->process_ipa_();
    
    // Statistiques de streaming
    static uint32_t ready_count = 0;
    static uint32_t not_ready_count = 0;
    
    if (this->frame_ready_) {
      ready_count++;
    } else {
      not_ready_count++;
    }
    
    uint32_t now = millis();
    if (now - this->last_frame_log_time_ >= 5000) {
      float sensor_fps = this->total_frames_received_ / 5.0f;
      float ready_rate = (float)ready_count / (float)(ready_count + not_ready_count) * 100.0f;
      
      // Afficher les stats IPA
      const IPAHistory &hist = this->ipa_.get_history();
      
      ESP_LOGI(TAG, "Streaming: %.1f fps | ready: %.1f%%", sensor_fps, ready_rate);
      ESP_LOGI(TAG, "  IPA AWB: R=%.2f B=%.2f CT=%uK", 
               hist.prev_red_gain, hist.prev_blue_gain, hist.prev_color_temp);
      ESP_LOGI(TAG, "  IPA AE: exp=%u gain=%.2f lum=%u", 
               hist.prev_exposure, hist.prev_gain, hist.prev_avg_luminance);
      
      this->total_frames_received_ = 0;
      this->last_frame_log_time_ = now;
      ready_count = 0;
      not_ready_count = 0;
    }
  }
}

void MipiDsiCam::dump_config() {
  ESP_LOGCONFIG(TAG, "MIPI Camera with IPA:");
  if (this->sensor_driver_) {
    ESP_LOGCONFIG(TAG, "  Sensor: %s", this->sensor_driver_->get_name());
    ESP_LOGCONFIG(TAG, "  PID: 0x%04X", this->sensor_driver_->get_pid());
  } else {
    ESP_LOGCONFIG(TAG, "  Sensor: %s (driver not loaded)", this->sensor_type_.c_str());
  }
  ESP_LOGCONFIG(TAG, "  Resolution: %ux%u", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Format: RGB565");
  ESP_LOGCONFIG(TAG, "  Lanes: %u", this->lane_count_);
  ESP_LOGCONFIG(TAG, "  Bayer: %u", this->bayer_pattern_);
  ESP_LOGCONFIG(TAG, "  Streaming: %s", this->streaming_ ? "YES" : "NO");
  
  if (this->ipa_initialized_) {
    IPAConfig &config = this->ipa_.get_config();
    ESP_LOGCONFIG(TAG, "  IPA Status: ACTIVE");
    ESP_LOGCONFIG(TAG, "    AWB: %s", config.awb_enabled ? "ON" : "OFF");
    ESP_LOGCONFIG(TAG, "    AE: %s", config.ae_enabled ? "ON" : "OFF");
    ESP_LOGCONFIG(TAG, "    Sharpen: %s (%u)", config.sharpen_enabled ? "ON" : "OFF", config.sharpen_strength);
    ESP_LOGCONFIG(TAG, "    Denoise: %s (%u)", config.denoise_enabled ? "ON" : "OFF", config.denoise_level);
  } else {
    ESP_LOGCONFIG(TAG, "  IPA Status: DISABLED");
  }
}

}  // namespace mipi_dsi_cam
}  // namespace esphome

#endif  // USE_ESP32_VARIANT_ESP32P4







