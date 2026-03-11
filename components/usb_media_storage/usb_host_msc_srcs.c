/*
 * Wrapper to compile bundled usb_host_msc sources.
 * ESPHome automatically compiles all .c files in the component directory.
 */
#ifdef USE_ESP_IDF

#include "components/usb_host_msc/src/msc_host.c"
#include "components/usb_host_msc/src/msc_scsi_bot.c"
#include "components/usb_host_msc/src/msc_host_vfs.c"
#include "components/usb_host_msc/src/diskio_usb.c"

#endif
