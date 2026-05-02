#ifndef __ESP_GALLERY_CONFIGS_H__
#define __ESP_GALLERY_CONFIGS_H__

#define MAX_IMAGE_WIDTH 240
#define MAX_IMAGE_HEIGHT 320

#ifdef ESP32_BUILD
    #define FS_ROOT_PATH "/"
#else
    #ifndef FS_ROOT_PATH
        #define FS_ROOT_PATH "./"
    #endif
#endif

#endif
