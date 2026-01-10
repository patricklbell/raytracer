#pragma once

#include "common/common_inc.h"
#include "os/os_inc.h"
#include "raytracer/raytracer_inc.h"

#include "tracing/tracing.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb/stb_image_write.h"

#define demo_hook internal

typedef struct DEMO_Settings DEMO_Settings;
struct DEMO_Settings {
    int         width;
    int         height;
    u8          samples;
    u8          bounces;
    NTString8   out; 
};

demo_hook void render(const DEMO_Settings* settings);