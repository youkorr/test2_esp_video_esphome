SENSOR_INFO = {
    'name': 'sc202cs',
    'manufacturer': 'SmartSens',
    'pid': 0xeb52,
    'i2c_address': 0x36,
    'lane_count': 1,
    'bayer_pattern': 0,
    'lane_bitrate_mbps': 576,
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
    (0x36ea, 0x06, 0),
    (0x36eb, 0x0a, 0),
    (0x36ec, 0x01, 0),
    (0x36ed, 0x18, 0),
    (0x36e9, 0x24, 0),
    (0x301f, 0x18, 0),
    (0x3031, 0x08, 0),
    (0x3037, 0x00, 0),
    (0x3200, 0x00, 0),
    (0x3201, 0xa0, 0),
    (0x3202, 0x00, 0),
    (0x3203, 0xf0, 0),
    (0x3204, 0x05, 0),
    (0x3205, 0xa7, 0),
    (0x3206, 0x03, 0),
    (0x3207, 0xc7, 0),
    (0x3208, 0x05, 0),
    (0x3209, 0x00, 0),
    (0x320a, 0x02, 0),
    (0x320b, 0xd0, 0),
    (0x3210, 0x00, 0),
    (0x3211, 0x04, 0),
    (0x3212, 0x00, 0),
    (0x3213, 0x04, 0),
    (0x3301, 0xff, 0),
    (0x3304, 0x68, 0),
    (0x3306, 0x40, 0),
    (0x3308, 0x08, 0),
    (0x3309, 0xa8, 0),
    (0x330b, 0xd0, 0),
    (0x330c, 0x18, 0),
    (0x330d, 0xff, 0),
    (0x330e, 0x20, 0),
    (0x331e, 0x59, 0),
    (0x331f, 0x99, 0),
    (0x3333, 0x10, 0),
    (0x335e, 0x06, 0),
    (0x335f, 0x08, 0),
    (0x3364, 0x1f, 0),
    (0x337c, 0x02, 0),
    (0x337d, 0x0a, 0),
    (0x338f, 0xa0, 0),
    (0x3390, 0x01, 0),
    (0x3391, 0x03, 0),
    (0x3392, 0x1f, 0),
    (0x3393, 0xff, 0),
    (0x3394, 0xff, 0),
    (0x3395, 0xff, 0),
    (0x33a2, 0x04, 0),
    (0x33ad, 0x0c, 0),
    (0x33b1, 0x20, 0),
    (0x33b3, 0x38, 0),
    (0x33f9, 0x40, 0),
    (0x33fb, 0x48, 0),
    (0x33fc, 0x0f, 0),
    (0x33fd, 0x1f, 0),
    (0x349f, 0x03, 0),
    (0x34a6, 0x03, 0),
    (0x34a7, 0x1f, 0),
    (0x34a8, 0x38, 0),
    (0x34a9, 0x30, 0),
    (0x34ab, 0xd0, 0),
    (0x34ad, 0xd8, 0),
    (0x34f8, 0x1f, 0),
    (0x34f9, 0x20, 0),
    (0x3630, 0xa0, 0),
    (0x3631, 0x92, 0),
    (0x3632, 0x64, 0),
    (0x3633, 0x43, 0),
    (0x3637, 0x49, 0),
    (0x363a, 0x85, 0),
    (0x363c, 0x0f, 0),
    (0x3650, 0x31, 0),
    (0x3670, 0x0d, 0),
    (0x3674, 0xc0, 0),
    (0x3675, 0xa0, 0),
    (0x3676, 0xa0, 0),
    (0x3677, 0x92, 0),
    (0x3678, 0x96, 0),
    (0x3679, 0x9a, 0),
    (0x367c, 0x03, 0),
    (0x367d, 0x0f, 0),
    (0x367e, 0x01, 0),
    (0x367f, 0x0f, 0),
    (0x3698, 0x83, 0),
    (0x3699, 0x86, 0),
    (0x369a, 0x8c, 0),
    (0x369b, 0x94, 0),
    (0x36a2, 0x01, 0),
    (0x36a3, 0x03, 0),
    (0x36a4, 0x07, 0),
    (0x36ae, 0x0f, 0),
    (0x36af, 0x1f, 0),
    (0x36bd, 0x22, 0),
    (0x36be, 0x22, 0),
    (0x36bf, 0x22, 0),
    (0x36d0, 0x01, 0),
    (0x370f, 0x02, 0),
    (0x3721, 0x6c, 0),
    (0x3722, 0x8d, 0),
    (0x3725, 0xc5, 0),
    (0x3727, 0x14, 0),
    (0x3728, 0x04, 0),
    (0x37b7, 0x04, 0),
    (0x37b8, 0x04, 0),
    (0x37b9, 0x06, 0),
    (0x37bd, 0x07, 0),
    (0x37be, 0x0f, 0),
    (0x3901, 0x02, 0),
    (0x3903, 0x40, 0),
    (0x3905, 0x8d, 0),
    (0x3907, 0x00, 0),
    (0x3908, 0x41, 0),
    (0x391f, 0x41, 0),
    (0x3933, 0x80, 0),
    (0x3934, 0x02, 0),
    (0x3937, 0x6f, 0),
    (0x393a, 0x01, 0),
    (0x393d, 0x01, 0),
    (0x393e, 0xc0, 0),
    (0x39dd, 0x41, 0),
    (0x3e00, 0x00, 0),
    (0x3e01, 0x4d, 0),
    (0x3e02, 0xc0, 0),
    (0x3e09, 0x00, 0),
    (0x4509, 0x28, 0),
    (0x450d, 0x61, 0),
]

GAIN_VALUES = [
    1000, 1031, 1063, 1094, 1125, 1156, 1188, 1219,
    1250, 1281, 1313, 1344, 1375, 1406, 1438, 1469,
    1500, 1531, 1563, 1594, 1625, 1656, 1688, 1719,
    1750, 1781, 1813, 1844, 1875, 1906, 1938, 1969,
    2000, 2062, 2126, 2188, 2250, 2312, 2376, 2438,
    2500, 2562, 2626, 2688, 2750, 2812, 2876, 2938,
    3000, 3062, 3126, 3188, 3250, 3312, 3376, 3438,
    3500, 3562, 3626, 3688, 3750, 3812, 3876, 3938,
    4000, 4124, 4252, 4376, 4500, 4624, 4752, 4876,
    5000, 5124, 5252, 5376, 5500, 5624, 5752, 5876,
    6000, 6124, 6252, 6376, 6500, 6624, 6752, 6876,
    7000, 7124, 7252, 7376, 7500, 7624, 7752, 7876,
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
    (0x80, 0x00, 0x01), (0x84, 0x00, 0x01), (0x88, 0x00, 0x01), (0x8c, 0x00, 0x01),
    (0x90, 0x00, 0x01), (0x94, 0x00, 0x01), (0x98, 0x00, 0x01), (0x9c, 0x00, 0x01),
    (0xa0, 0x00, 0x01), (0xa4, 0x00, 0x01), (0xa8, 0x00, 0x01), (0xac, 0x00, 0x01),
    (0xb0, 0x00, 0x01), (0xb4, 0x00, 0x01), (0xb8, 0x00, 0x01), (0xbc, 0x00, 0x01),
    (0xc0, 0x00, 0x01), (0xc4, 0x00, 0x01), (0xc8, 0x00, 0x01), (0xcc, 0x00, 0x01),
    (0xd0, 0x00, 0x01), (0xd4, 0x00, 0x01), (0xd8, 0x00, 0x01), (0xdc, 0x00, 0x01),
    (0xe0, 0x00, 0x01), (0xe4, 0x00, 0x01), (0xe8, 0x00, 0x01), (0xec, 0x00, 0x01),
    (0xf0, 0x00, 0x01), (0xf4, 0x00, 0x01), (0xf8, 0x00, 0x01), (0xfc, 0x00, 0x01),
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
    uint8_t fine;
    uint8_t coarse;
    uint8_t analog;
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
        
        esp_err_t ret = write_register({SENSOR_INFO['name']}_regs::GAIN_FINE, gain.fine);
        if (ret != ESP_OK) return ret;
        
        ret = write_register({SENSOR_INFO['name']}_regs::GAIN_COARSE, gain.coarse);
        if (ret != ESP_OK) return ret;
        
        ret = write_register({SENSOR_INFO['name']}_regs::GAIN_ANALOG, gain.analog);
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
        
        esphome::i2c::ErrorCode err = i2c_->write(data, 3);
        return (err == esphome::i2c::ERROR_OK) ? ESP_OK : ESP_FAIL;
    }}
    
    esp_err_t read_register(uint16_t reg, uint8_t* value) {{
        uint8_t addr[2] = {{
            static_cast<uint8_t>((reg >> 8) & 0xFF),
            static_cast<uint8_t>(reg & 0xFF)
        }};
        
        esphome::i2c::ErrorCode err = i2c_->write_read(addr, 2, value, 1);
        return (err == esphome::i2c::ERROR_OK) ? ESP_OK : ESP_FAIL;
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
