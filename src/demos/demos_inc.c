#include "common/common_inc.c"
#include "os/os_inc.c"
#include "geo/geo.c"
#include "mesh/mesh.c"
#include "lbvh/lbvh.c"
#include "raytracer/raytracer_inc.c"

int main(int argc, char** argv) {
    ThreadCtx main_ctx;
    thread_equip(&main_ctx);

    b8 help = false, bad = false;
    const int DEFAULT_WIDTH=100, DEFAULT_HEIGHT=100;
    const u8 DEFAULT_BOUNCES=8;

    DEMO_Settings settings = {
        .width=DEFAULT_WIDTH,
        .height=DEFAULT_HEIGHT,
        .samples=1,
        .bounces=DEFAULT_BOUNCES,
    };

    // argument parsing
    int positional_i = 0;
    int seed = 0;
    for (int i = 1; i < argc; i++) {
        NTString8 arg = make_ntstr8(argv[i], strlen(argv[i]));
        
        if (!ntstr8_begins_with(arg, "-")) {
            if (positional_i == 0) {
                settings.out = make_ntstr8(argv[i], strlen(argv[i]));
                positional_i++;
                continue;
            }
        }

        if (ntstr8_eq(arg, ntstr8_lit("--help")) || ntstr8_eq(arg, ntstr8_lit("-h"))) {
            help = true;
        } else if (ntstr8_begins_with(arg, "--width")) {
            if (sscanf(arg.cstr, "--width=%d", &settings.width) != 1 || settings.width <= 0) {
                fprintf(stderr, "invalid WIDTH argument, must be > 0");
                bad = true;
            }
        } else if (ntstr8_begins_with(arg, "--height")) {
            if (sscanf(arg.cstr, "--height=%d", &settings.height) != 1 || settings.height <= 0) {
                fprintf(stderr, "invalid HEIGHT argument, must be > 0");
                bad = true;
            }
        } else if (ntstr8_begins_with(arg, "--samples")) {
            int samples;
            if (sscanf(arg.cstr, "--samples=%d", &samples) != 1 || samples <= 0 || samples > 255) {
                fprintf(stderr, "invalid SAMPLES argument, must be > 0 and < 255");
                bad = true;
            } else {
                settings.samples = (u8)samples;
            }
        } else if (ntstr8_begins_with(arg, "--bounces")) {
            int bounces;
            if (sscanf(arg.cstr, "--bounces=%d", &bounces) != 1 || bounces <= 0 || bounces > RT_MAX_MAX_BOUNCES) {
                fprintf(stderr, "invalid BOUNCES argument, must be > 0 and < 255");
                bad = true;
            } else {
                settings.bounces = (u8)bounces;
            }
        } else if (ntstr8_begins_with(arg, "--seed")) {
            if (sscanf(arg.cstr, "--seed=%d", &seed) != 1) {
                fprintf(stderr, "invalid SEED argument");
                bad = true;
            }
        } else {
            fprintf(stderr, "invalid argument \"%s\", skipping\n", arg.cstr);
        }
    }
    if (settings.out.length == 0) {
        fprintf(stderr, "missing required positional argument OUT\n");
        bad = true;
    }
    if (help || bad) {
        if (bad)
            printf("\n");
        printf(
            "Usage: [OUT]\n"
            "Renders the demo to OUT as a high dynamic range (.hdr) image file.\n"
            "\n"
            "Options:\n"
            "   -h, --help          show this help message\n"
            "   --width=WIDTH       set the width of image to WIDTH pixels. defaults to %d\n"
            "   --height=HEIGHT     set the height of image to HEIGHT pixels. defaults to %d\n"
            "   --samples=SAMPLES   set the number of samples per pixel to SAMPLES x SAMPLES. defaults to 1\n"
            "   --bounces=BOUNCES   set the maximum number of ray bounces to BOUNCES. defaults to %d\n"
            "   --seed=SEED         seed random number generators with SEED\n",
            DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_BOUNCES
        );
        return !help;
    }

    rand_seed((u64)seed);

    // call demo hook
    render(&settings);
    return 0;
}

RT_CastSettings get_rt_cast_settings(const DEMO_Settings* settings, vec3_f32 eye, vec3_f32 subject, DEMO_ExtraCastSettings extra) {
    vec3_f32 d = sub_3f32(subject, eye);
    vec3_f32 up = make_3f32(0,1,0);
    vec3_f32 forward = normalize_3f32(d);
    vec3_f32 right = cross_3f32(forward, up);

    f32 aspect_ratio = (f32)settings->width/settings->height;
    f32 vfov = (extra.vfov > 0.f) ? extra.vfov : DegreesToRad(45);
    f32 focus_distance = length_3f32(d);

    f32 h = tan_f32(vfov/2.f);
    f32 focus_plane_height = 2*h*focus_distance;
    f32 defocus_radius = focus_distance*tan_f32(extra.defocus_angle/2.f);

    return (RT_CastSettings){
        .eye=eye,
        .up=up,
        .forward=forward,
        .right=right,
        .viewport=make_3f32(focus_plane_height*aspect_ratio, focus_plane_height, focus_distance),
        .samples=settings->samples,
        .ior=1.f,
        .defocus=extra.defocus_angle > 0.f,
        .defocus_disk=make_2f32(defocus_radius, defocus_radius),
        .orthographic=extra.orthographic,
    };
}

RT_TracerSettings get_rt_tracer_settings(const DEMO_Settings* settings, DEMO_ExtraTracerSettings extra) {
    return (RT_TracerSettings){
        .max_bounces=settings->bounces,
        .sky=!extra.no_sky,
    };
}