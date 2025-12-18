// Wrapper to force compilation of ESP-DL face detection postprocessors
// This ensures MSR and MNP postprocessors for face detection are compiled and linked

#include "../../esp-dl/vision/detect/dl_detect_postprocessor.cpp"
#include "../../esp-dl/vision/detect/dl_detect_msr_postprocessor.cpp"
#include "../../esp-dl/vision/detect/dl_detect_mnp_postprocessor.cpp"
