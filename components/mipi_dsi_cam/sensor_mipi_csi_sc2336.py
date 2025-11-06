SENSOR_INFO = {
    'name': 'sc2336',
    'manufacturer': 'SmartSens',
    'pid': 0xcb3a,
    'i2c_address': 0x36,
    'lane_count': 2,
    'bayer_pattern': 3,
    'lane_bitrate_mbps': 336,
    'width': 1280,
    'height': 720,
    'fps': 30,
}

REGISTERS = {
    'sensor_id_h': 0x3107,
    'sensor_id_l': 0x3108,
    'stream_mode': 0x0100,
    'gain_fine': 0x3e07,
    'gain_coarse': 0x3e06,
    'gain_analog': 0x3e09,
    'exposure_h': 0x3e00,
    'exposure_m': 0x3e01,
    'exposure_l': 0x3e02,
    'flip_mirror': 0x3221,
}

INIT_SEQUENCE = [
    (0x0103, 0x01, 10),
    (0x0100, 0x00, 10),
    (0x36e9, 0x80, 0),
    (0x37f9, 0x80, 0),
    (0x301f, 0x8e, 0),
    (0x3031, 0x08, 0),
    (0x3037, 0x00, 0),
    (0x3106, 0x05, 0),
    (0x3200, 0x01, 0),
    (0x3201, 0x34, 0),
    (0x3202, 0x00, 0),
    (0x3203, 0xb4, 0),
    (0x3204, 0x06, 0),
    (0x3205, 0x53, 0),
    (0x3206, 0x03, 0),
    (0x3207, 0x8b, 0),
    (0x3208, 0x05, 0),
    (0x3209, 0x00, 0),
    (0x320a, 0x02, 0),
    (0x320b, 0xd0, 0),
    (0x320c, 0x08, 0),
    (0x320d, 0xc0, 0),
    (0x320e, 0x04, 0),
    (0x320f, 0xe2, 0),
    (0x3210, 0x00, 0),
    (0x3211, 0x10, 0),
    (0x3212, 0x00, 0),
    (0x3213, 0x04, 0),
    (0x3248, 0x04, 0),
    (0x3249, 0x0b, 0),
    (0x3253, 0x08, 0),
    (0x3301, 0x09, 0),
    (0x3302, 0xff, 0),
    (0x3303, 0x10, 0),
    (0x3306, 0x68, 0),
    (0x3307, 0x02, 0),
    (0x330a, 0x01, 0),
    (0x330b, 0x18, 0),
    (0x330c, 0x16, 0),
    (0x330d, 0xd3, 0),
    (0x3318, 0x02, 0),
    (0x3321, 0x0a, 0),
    (0x3327, 0x0e, 0),
    (0x332b, 0x12, 0),
    (0x3333, 0x10, 0),
    (0x3334, 0x40, 0),
    (0x335e, 0x06, 0),
    (0x335f, 0x0a, 0),
    (0x3364, 0x1f, 0),
    (0x337c, 0x02, 0),
    (0x337d, 0x0e, 0),
    (0x3390, 0x09, 0),
    (0x3391, 0x0f, 0),
    (0x3392, 0x1f, 0),
    (0x3393, 0x20, 0),
    (0x3394, 0x20, 0),
    (0x3395, 0xff, 0),
    (0x33a2, 0x04, 0),
    (0x33b1, 0x80, 0),
    (0x33b2, 0x68, 0),
    (0x33b3, 0x42, 0),
    (0x33f9, 0x78, 0),
    (0x33fb, 0xe0, 0),
    (0x33fc, 0x0f, 0),
    (0x33fd, 0x1f, 0),
    (0x349f, 0x03, 0),
    (0x34a6, 0x0f, 0),
    (0x34a7, 0x1f, 0),
    (0x34a8, 0x42, 0),
    (0x34a9, 0x06, 0),
    (0x34aa, 0x01, 0),
    (0x34ab, 0x28, 0),
    (0x34ac, 0x01, 0),
    (0x34ad, 0x90, 0),
    (0x3630, 0xf4, 0),
    (0x3633, 0x22, 0),
    (0x3639, 0xf4, 0),
    (0x363c, 0x47, 0),
    (0x3670, 0x09, 0),
    (0x3674, 0xf4, 0),
    (0x3675, 0xfb, 0),
    (0x3676, 0xed, 0),
    (0x367c, 0x09, 0),
    (0x367d, 0x0f, 0),
    (0x3690, 0x22, 0),
    (0x3691, 0x22, 0),
    (0x3692, 0x22, 0),
    (0x3698, 0x89, 0),
    (0x3699, 0x96, 0),
    (0x369a, 0xd0, 0),
    (0x369b, 0xd0, 0),
    (0x369c, 0x09, 0),
    (0x369d, 0x0f, 0),
    (0x36a2, 0x09, 0),
    (0x36a3, 0x0f, 0),
    (0x36a4, 0x1f, 0),
    (0x36d0, 0x01, 0),
    (0x36ea, 0x0e, 0),
    (0x36eb, 0x0a, 0),
    (0x36ec, 0x1a, 0),
    (0x36ed, 0x18, 0),
    (0x3722, 0xe1, 0),
    (0x3724, 0x41, 0),
    (0x3725, 0xc1, 0),
    (0x3728, 0x20, 0),
    (0x37fa, 0x15, 0),
    (0x37fb, 0x32, 0),
    (0x37fc, 0x11, 0),
    (0x37fd, 0x17, 0),
    (0x3900, 0x0d, 0),
    (0x3905, 0x98, 0),
    (0x391b, 0x81, 0),
    (0x391c, 0x10, 0),
    (0x3933, 0x81, 0),
    (0x3934, 0xc5, 0),
    (0x3940, 0x68, 0),
    (0x3941, 0x00, 0),
    (0x3942, 0x01, 0),
    (0x3943, 0xc6, 0),
    (0x3952, 0x02, 0),
    (0x3953, 0x0f, 0),
    (0x3e01, 0x4d, 0),
    (0x3e02, 0xc0, 0),
    (0x3e08, 0x1f, 0),
    (0x3e1b, 0x14, 0),
    (0x4509, 0x38, 0),
    (0x4819, 0x06, 0),
    (0x481b, 0x04, 0),
    (0x481d, 0x0c, 0),
    (0x481f, 0x03, 0),
    (0x4821, 0x0a, 0),
    (0x4823, 0x03, 0),
    (0x4825, 0x03, 0),
    (0x4827, 0x03, 0),
    (0x4829, 0x05, 0),
    (0x5799, 0x06, 0),
    (0x5ae0, 0xfe, 0),
    (0x5ae1, 0x40, 0),
    (0x5ae2, 0x30, 0),
    (0x5ae3, 0x28, 0),
    (0x5ae4, 0x20, 0),
    (0x5ae5, 0x30, 0),
    (0x5ae6, 0x28, 0),
    (0x5ae7, 0x20, 0),
    (0x5ae8, 0x3c, 0),
    (0x5ae9, 0x30, 0),
    (0x5aea, 0x28, 0),
    (0x5aeb, 0x3c, 0),
    (0x5aec, 0x30, 0),
    (0x5aed, 0x28, 0),
    (0x5aee, 0xfe, 0),
    (0x5aef, 0x40, 0),
    (0x5af4, 0x30, 0),
    (0x5af5, 0x28, 0),
    (0x5af6, 0x20, 0),
    (0x5af7, 0x30, 0),
    (0x5af8, 0x28, 0),
    (0x5af9, 0x20, 0),
    (0x5afa, 0x3c, 0),
    (0x5afb, 0x30, 0),
    (0x5afc, 0x28, 0),
    (0x5afd, 0x3c, 0),
    (0x5afe, 0x30, 0),
    (0x5aff, 0x28, 0),
    (0x36e9, 0x54, 0),
    (0x37f9, 0x54, 0),
]

GAIN_VALUES = [
    1000, 1031, 1063, 1094, 1125, 1156, 1188, 1219,
    1250, 1281, 1313, 1344, 1375, 1406, 1438, 1469,
    1500, 1531, 1563, 1594, 1625, 1656, 1688, 1719,
    1750, 1781, 1813, 1844, 1875, 1906, 1938, 1969,
    2000, 2063, 2125, 2188, 2250, 2313, 2375, 2438,
    2500, 2563, 2625, 2688, 2750, 2813, 2875, 2938,
    3000, 3063, 3125, 3188, 3250, 3313, 3375, 3438,
    3500, 3563, 3625, 3688, 3750, 3813, 3875, 3938,
    4000, 4126, 4250, 4376, 4500, 4626, 4750, 4876,
    5000, 5126, 5250, 5376, 5500, 5626, 5750, 5876,
    6000, 6126, 6250, 6376, 6500, 6626, 6750, 6876,
    7000, 7126, 7250, 7376, 7500, 7626, 7750, 7876,
]

GAIN_REGISTERS = [
    (0x80, 0x00, 0x00), (0x84, 0x00, 0x00), (0x88, 0x00, 0x00), (0x8c, 0x00, 0x00),
    (0x90, 0x00, 0x00), (0x94, 0x00, 0x00), (0x98, 0x00, 0x00), (0x9c, 0x00, 0x00),
    (0xa0, 0x00, 0x00), (0xa4, 0x00, 0x00), (0xa8, 0x00, 0x00), (0xac, 0x00, 0x00),
    (0xb0, 0x00, 0x00), (0xb4, 0x00, 0x00), (0xb8, 0x00, 0x00), (0xbc, 0x00, 0x00),
    (0xc0, 0x00, 0x00), (0xc4, 0x00, 0x00), (0xc8, 0x00, 0x00), (0xcc, 0x00, 0x00),
    (0xd0, 0x00, 0x00), (0xd4, 0x00, 0x00), (0xd8, 0x00, 0x00), (0xdc, 0x00, 0x00),
    (0xe0, 0x00, 0x00), (0xe4, 0x00, 0x00), (0xe8, 0x00, 0x00), (0xec, 0x00, 0x00),
    (0xf0, 0x00, 0x00), (0xf4, 0x00, 0x00), (0xf8, 0x00, 0x00), (0xfc, 0x00, 0x00),
    (0x80, 0x01, 0x00), (0x84, 0x01, 0x00), (0x88, 0x01, 0x00), (0x8c, 0x01, 0x00),
    (0x90, 0x01, 0x00), (0x94, 0x01, 0x00), (0x98, 0x01, 0x00), (0x9c, 0x01, 0x00),
    (0xa0, 0x01, 0x00), (0xa4, 0x01, 0x00), (0xa8, 0x01, 0x00), (0xac, 0x01, 0x00),
    (0xb0, 0x01, 0x00), (0xb4, 0x01, 0x00), (0xb8, 0x01, 0x00), (0xbc, 0x01, 0x00),
    (0xc0, 0x01, 0x00), (0xc4, 0x01, 0x00), (0xc8, 0x01, 0x00), (0xcc, 0x01, 0x00),
    (0xd0, 0x01, 0x00), (0xd4, 0x01, 0x00), (0xd8, 0x01, 0x00), (0xdc, 0x01, 0x00),
    (0xe0, 0x01, 0x00), (0xe4, 0x01, 0x00), (0xe8, 0x01, 0x00), (0xec, 0x01, 0x00),
    (0xf0, 0x01, 0x00), (0xf4, 0x01, 0x00), (0xf8, 0x01, 0x00), (0xfc, 0x01, 0x00),
    (0x80, 0x01, 0x08), (0x84, 0x01, 0x08), (0x88, 0x01, 0x08), (0x8c, 0x01, 0x08),
    (0x90, 0x01, 0x08), (0x94, 0x01, 0x08), (0x98, 0x01, 0x08), (0x9c, 0x01, 0x08),
    (0xa0, 0x01, 0x08), (0xa4, 0x01, 0x08), (0xa8, 0x01, 0x08), (0xac, 0x01, 0x08),
    (0xb0, 0x01, 0x08), (0xb4, 0x01, 0x08), (0xb8, 0x01, 0x08), (0xbc, 0x01, 0x08),
    (0xc0, 0x01, 0x08), (0xc4, 0x01, 0x08), (0xc8, 0x01, 0x08), (0xcc, 0x01, 0x08),
    (0xd0, 0x01, 0x08), (0xd4, 0x01, 0x08), (0xd8, 0x01, 0x08), (0xdc, 0x01, 0x08),
    (0xe0, 0x01, 0x08), (0xe4, 0x01, 0x08), (0xe8, 0x01, 0x08), (0xec, 0x01, 0x08),
    (0xf0, 0x01, 0x08), (0xf4, 0x01, 0x08), (0xf8, 0x01, 0x08), (0xfc, 0x01, 0x08),
]

def generate_driver_cpp():
    cpp_code = f'''
namespace esphome {{
namespace mipi_dsi_cam {{

namespace {SENSOR_INFO['name']}_regs {{
'''
    
    for name, addr in REGISTERS.items():
        cpp_code += f'    constexpr uint16_t {name.upper()} = 0x{addr:04X};\n'
    
    cpp_code += f'''
}}

struct {SENSOR_INFO['name'].upper()}InitRegister {{
    uint16_t addr;
    uint8_t value;
    uint16_t delay_ms;
}};

static const {SENSOR_INFO['name'].upper()}InitRegister {SENSOR_INFO['name']}_init_sequence[] = {{
'''
    
    for addr, value, delay in INIT_SEQUENCE:
        cpp_code += f'    {{0x{addr:04X}, 0x{value:02X}, {delay}}},\n'
    
    cpp_code += f'''
}};

struct {SENSOR_INFO['name'].upper()}GainRegisters {{
    uint8_t dgain_fine;
    uint8_t dgain_coarse;
    uint8_t analog_gain;
}};

static const {SENSOR_INFO['name'].upper()}GainRegisters {SENSOR_INFO['name']}_gain_map[] = {{
'''
    
    for fine, coarse, analog in GAIN_REGISTERS:
        cpp_code += f'    {{0x{fine:02X}, 0x{coarse:02X}, 0x{analog:02X}}},\n'
    
    cpp_code += f'''
}};

class {SENSOR_INFO['name'].upper()}Driver {{
public:
    {SENSOR_INFO['name'].upper()}Driver(esphome::i2c::I2CDevice* i2c) : i2c_(i2c) {{}}
    
    esp_err_t init() {{
        ESP_LOGI(TAG, "Init {SENSOR_INFO['name'].upper()}");
        
        for (size_t i = 0; i < sizeof({SENSOR_INFO['name']}_init_sequence) / sizeof({SENSOR_INFO['name'].upper()}InitRegister); i++) {{
            const auto& reg = {SENSOR_INFO['name']}_init_sequence[i];
            
            if (reg.delay_ms > 0) {{
                vTaskDelay(pdMS_TO_TICKS(reg.delay_ms));
            }}
            
            esp_err_t ret = write_register(reg.addr, reg.value);
            if (ret != ESP_OK) {{
                ESP_LOGE(TAG, "Init failed at reg 0x%04X", reg.addr);
                return ret;
            }}
        }}
        
        ESP_LOGI(TAG, "{SENSOR_INFO['name'].upper()} initialized");
        return ESP_OK;
    }}
    
    esp_err_t read_id(uint16_t* pid) {{
        uint8_t pid_h, pid_l;
        
        esp_err_t ret = read_register({SENSOR_INFO['name']}_regs::SENSOR_ID_H, &pid_h);
        if (ret != ESP_OK) return ret;
        
        ret = read_register({SENSOR_INFO['name']}_regs::SENSOR_ID_L, &pid_l);
        if (ret != ESP_OK) return ret;
        
        *pid = (pid_h << 8) | pid_l;
        return ESP_OK;
    }}
    
    esp_err_t start_stream() {{
        return write_register({SENSOR_INFO['name']}_regs::STREAM_MODE, 0x01);
    }}
    
    esp_err_t stop_stream() {{
        return write_register({SENSOR_INFO['name']}_regs::STREAM_MODE, 0x00);
    }}
    
    esp_err_t set_gain(uint32_t gain_index) {{
        if (gain_index >= sizeof({SENSOR_INFO['name']}_gain_map) / sizeof({SENSOR_INFO['name'].upper()}GainRegisters)) {{
            gain_index = (sizeof({SENSOR_INFO['name']}_gain_map) / sizeof({SENSOR_INFO['name'].upper()}GainRegisters)) - 1;
        }}
        
        const auto& gain = {SENSOR_INFO['name']}_gain_map[gain_index];
        
        esp_err_t ret = write_register({SENSOR_INFO['name']}_regs::GAIN_FINE, gain.dgain_fine);
        if (ret != ESP_OK) return ret;
        
        ret = write_register({SENSOR_INFO['name']}_regs::GAIN_COARSE, gain.dgain_coarse);
        if (ret != ESP_OK) return ret;
        
        ret = write_register({SENSOR_INFO['name']}_regs::GAIN_ANALOG, gain.analog_gain);
        return ret;
    }}
    
    esp_err_t set_exposure(uint32_t exposure) {{
        uint8_t exp_h = (exposure >> 12) & 0x0F;
        uint8_t exp_m = (exposure >> 4) & 0xFF;
        uint8_t exp_l = (exposure & 0x0F) << 4;
        
        esp_err_t ret = write_register({SENSOR_INFO['name']}_regs::EXPOSURE_H, exp_h);
        if (ret != ESP_OK) return ret;
        
        ret = write_register({SENSOR_INFO['name']}_regs::EXPOSURE_M, exp_m);
        if (ret != ESP_OK) return ret;
        
        ret = write_register({SENSOR_INFO['name']}_regs::EXPOSURE_L, exp_l);
        return ret;
    }}
    
    esp_err_t write_register(uint16_t reg, uint8_t value) {{
        uint8_t data[3] = {{
            static_cast<uint8_t>((reg >> 8) & 0xFF),
            static_cast<uint8_t>(reg & 0xFF),
            value
        }};
        
        auto err = i2c_->write_read(data, 3, nullptr, 0); // new write
        if (err != esphome::i2c::ERROR_OK) {{
            ESP_LOGE(TAG, "I2C write failed for reg 0x%04X", reg);
            return ESP_FAIL;
        }}
        return ESP_OK;
    }}
    
    esp_err_t read_register(uint16_t reg, uint8_t* value) {{
        uint8_t addr[2] = {{
            static_cast<uint8_t>((reg >> 8) & 0xFF),
            static_cast<uint8_t>(reg & 0xFF)
        }};
        
        auto err = i2c_->write_read(addr, 2, value, 1);
                                                       
                                                     
        if (err != esphome::i2c::ERROR_OK) {{
            ESP_LOGE(TAG, "I2C cmd failed for reg 0x%04X", reg);
            return ESP_FAIL;
        }}
        
        return ESP_OK;
    }}
    
private:
    esphome::i2c::I2CDevice* i2c_;
    static constexpr const char* TAG = "{SENSOR_INFO['name'].upper()}";
}};

class {SENSOR_INFO['name'].upper()}Adapter : public ISensorDriver {{
public:
    {SENSOR_INFO['name'].upper()}Adapter(i2c::I2CDevice* i2c) : driver_(i2c) {{}}
    
    const char* get_name() const override {{ return "{SENSOR_INFO['name']}"; }}
    uint16_t get_pid() const override {{ return 0x{SENSOR_INFO['pid']:04X}; }}
    uint8_t get_i2c_address() const override {{ return 0x{SENSOR_INFO['i2c_address']:02X}; }}
    uint8_t get_lane_count() const override {{ return {SENSOR_INFO['lane_count']}; }}
    uint8_t get_bayer_pattern() const override {{ return {SENSOR_INFO['bayer_pattern']}; }}
    uint16_t get_lane_bitrate_mbps() const override {{ return {SENSOR_INFO['lane_bitrate_mbps']}; }}
    uint16_t get_width() const override {{ return {SENSOR_INFO['width']}; }}
    uint16_t get_height() const override {{ return {SENSOR_INFO['height']}; }}
    uint8_t get_fps() const override {{ return {SENSOR_INFO['fps']}; }}
    
    esp_err_t init() override {{ return driver_.init(); }}
    esp_err_t read_id(uint16_t* pid) override {{ return driver_.read_id(pid); }}
    esp_err_t start_stream() override {{ return driver_.start_stream(); }}
    esp_err_t stop_stream() override {{ return driver_.stop_stream(); }}
    esp_err_t set_gain(uint32_t gain_index) override {{ return driver_.set_gain(gain_index); }}
    esp_err_t set_exposure(uint32_t exposure) override {{ return driver_.set_exposure(exposure); }}
    esp_err_t write_register(uint16_t reg, uint8_t value) override {{ return driver_.write_register(reg, value); }}
    esp_err_t read_register(uint16_t reg, uint8_t* value) override {{ return driver_.read_register(reg, value); }}
    
private:
    {SENSOR_INFO['name'].upper()}Driver driver_;
}};

}}
}}
'''
    
    return cpp_code

def get_sensor_info():
    return SENSOR_INFO

def get_driver_code():
    return generate_driver_cpp()
