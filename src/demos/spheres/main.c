#include "demos/demos_inc.h"
#include "demos/demos_inc.c"

demo_hook void render(const DEMO_Settings* settings) {
    {DeferResource(RT_Handle tracer = rt_make_tracer((RT_TracerSettings){}), rt_tracer_cleanup(tracer)) {
        {DeferResource(RT_World* world = rt_make_world((RT_WorldSettings){}), rt_world_cleanup(world)) {
            rt_world_add_sphere(world, 
                (RT_Sphere){
                    .center=make_3f32(0,0,0),
                    .color=make_3f32(1,0,0),
                    .radius=1.0,
                }
            );
            
            rt_tracer_update_world(tracer, world);
            {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
                int width = settings->width, height = settings->height;
                f32 aspect_ratio = (f32)width/height;

                vec3_f32* buffer = push_array(scratch.arena, vec3_f32, width*height);

                rt_tracer_cast(
                    tracer, 
                    (RT_CastSettings){
                        .eye=make_3f32(0,0,-1),
                        .up=make_3f32(0,1,0),
                        .forward=make_3f32(0,0,1),
                        .z_extents=make_2f32(1.0, 1.0*aspect_ratio),
                        .z_near=0.1f,
                    },
                    buffer, width, height
                );

                stbi_write_hdr(settings->out.cstr, width, height, 3, &buffer[0].v[0]);
            }}}
        }}
    }
}