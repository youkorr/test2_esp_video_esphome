set(srcs "sd_mmc_card.cpp")

set(requires "esp_driver_gpio")
set(priv_requires "fatfs" "esp_driver_sdmmc" "vfs")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       REQUIRES ${requires}
                       PRIV_REQUIRES ${priv_requires})
