#pragma once
// Local compatibility declarations for esp_vfs_fat.h (ESP-IDF 5.x).
// ESPHome compiles external components with __init__.py as part of the main
// esphome IDF component, which does not list 'fatfs' in its REQUIRES.
// Rather than trying to add that dependency, we forward-declare only what we
// need here.  The actual symbols are present in libfatfs.a (fatfs is compiled
// because CONFIG_FATFS_LFN_STACK is set in sdkconfig_options).

// If the real header is somehow accessible, use it instead.
#ifdef __has_include
#  if __has_include("esp_vfs_fat.h")
#    include "esp_vfs_fat.h"
#    define SD_MMC_CARD_VFS_FAT_H_INCLUDED
#  endif
#endif

#ifndef SD_MMC_CARD_VFS_FAT_H_INCLUDED

#include "esp_err.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Matches esp_vfs_fat_mount_config_t in ESP-IDF 5.x
// Fields are in struct-layout order; we include all fields known in 5.5.x.
// Unknown future fields would be read as zero because the struct is value-
// initialised with designated initialisers at the call site.
typedef struct {
    bool   format_if_mount_failed;   ///< Format if mount fails
    int    max_files;                ///< Max number of open files
    size_t allocation_unit_size;     ///< Cluster size (bytes)
    bool   disk_status_check_enable; ///< Check disk status on each access (IDF 5+)
    bool   use_one_fat;              ///< Use only one FAT table (IDF 5.2+)
} esp_vfs_fat_sdmmc_mount_config_t;

esp_err_t esp_vfs_fat_sdmmc_mount(const char                              *base_path,
                                   const sdmmc_host_t                      *host_config,
                                   const void                              *slot_config,
                                   const esp_vfs_fat_sdmmc_mount_config_t  *mount_config,
                                   sdmmc_card_t                           **out_card);

// Returns total and free bytes on the FAT volume mounted at base_path.
// Available since ESP-IDF 4.4. Replaces statvfs() which may not be in the
// include path for ESPHome components.
esp_err_t esp_vfs_fat_info(const char *base_path,
                            uint64_t   *out_total_bytes,
                            uint64_t   *out_free_bytes);

#ifdef __cplusplus
}
#endif

#endif // SD_MMC_CARD_VFS_FAT_H_INCLUDED
