#include "common/common_inc.c"
#include "os/os_inc.c"
#include "raytracer/raytracer_inc.c"

int main(int argc, char** argv) {
    ThreadCtx main_ctx;
    thread_equip(&main_ctx);

    b8 help = false, bad = false;
    const int DEFAULT_WIDTH=100, DEFAULT_HEIGHT=100;

    DEMO_Settings settings = {
        .width=DEFAULT_WIDTH,
        .height=DEFAULT_HEIGHT,
    };

    // argument parsing
    int positional_i = 0;
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
                fprintf(stderr, "invalid WIDTH argument");
                bad = true;
            }
        } else if (ntstr8_begins_with(arg, "--height")) {
            if (sscanf(arg.cstr, "--height=%d", &settings.height) != 1 || settings.height <= 0) {
                fprintf(stderr, "invalid HEIGHT argument");
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
            "   --width=WIDTH       set width of image to WIDTH pixels. defaults to %d\n"
            "   --height=HEIGHT     set height of image to HEIGHT pixels. defaults to %d\n",
            argv[0], DEFAULT_WIDTH, DEFAULT_HEIGHT
        );
        return !help;
    }

    // call demo hook
    render(&settings);
    return 0;
}