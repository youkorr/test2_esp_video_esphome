set(srcs "src/esp_cam_sensor.c"
         "src/esp_cam_motor.c"
         "src/esp_cam_sensor_xclk.c"
         "src/driver_spi/spi_slave.c"
         "src/driver_cam/esp_cam_ctlr_spi_cam.c"
         "sensor/ov5647/ov5647.c"
         "sensor/sc202cs/sc202cs.c")

set(include_dirs "include"
                 "sensor/ov5647/include"
                 "sensor/sc202cs/include")

set(priv_include_dirs "src"
                      "src/driver_spi"
                      "src/driver_cam"
                      "sensor/ov5647/private_include"
                      "sensor/sc202cs/include/private_include")

set(requires "esp_driver_i2c" "esp_driver_gpio" "esp_driver_ledc" "esp_driver_cam")
set(priv_requires "spi_flash" "driver")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       PRIV_INCLUDE_DIRS ${priv_include_dirs}
                       REQUIRES ${requires}
                       PRIV_REQUIRES ${priv_requires})


