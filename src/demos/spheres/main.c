#include "demos/demos_inc.h"
#include "demos/demos_inc.c"

static void add_lambertian_sphere(RT_World* world, vec3_f32 center, f32 radius, vec3_f32 albedo) {
    RT_Handle material = rt_world_add_material(world);
    RT_Material* material_ptr = rt_world_resolve_material(world, material);
    material_ptr->type = RT_MaterialType_Lambertian;
    material_ptr->albedo = albedo;

    RT_Handle entity = rt_world_add_entity(world);
    RT_Entity* entity_ptr = rt_world_resolve_entity(world, entity);
    entity_ptr->type = RT_EntityType_Sphere;
    entity_ptr->material = material;
    entity_ptr->sphere.center = center;
    entity_ptr->sphere.radius = radius;
}

demo_hook void render(const DEMO_Settings* settings) {
    {DeferResource(RT_Handle tracer = rt_make_tracer((RT_TracerSettings){}), rt_tracer_cleanup(tracer)) {
        {DeferResource(RT_World* world = rt_make_world((RT_WorldSettings){}), rt_world_cleanup(world)) {
            add_lambertian_sphere(world, make_3f32(0,0,0), 1.0, make_3f32(0.5,0.5,0.5));
            add_lambertian_sphere(world, make_3f32(0,-101,0), 100.0, make_3f32(0.5,0.5,0.5));
            
            rt_tracer_update_world(tracer, world);
            {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
                int width = settings->width, height = settings->height;
                f32 aspect_ratio = (f32)width/height;

                vec3_f32* buffer = push_array(scratch.arena, vec3_f32, width*height);

                rt_tracer_cast(
                    tracer, 
                    (RT_CastSettings){
                        .eye=make_3f32(0,0,2),
                        .up=make_3f32(0,1,0),
                        .forward=make_3f32(0,0,-1),
                        .z_extents=make_2f32(0.1*aspect_ratio, 0.1),
                        .z_near=0.1,
                        .samples=settings->samples,
                        .max_bounces=8,
                    },
                    buffer, width, height
                );

                stbi_write_hdr(settings->out.cstr, width, height, 3, &buffer[0].v[0]);
            }}}
        }}
    }
}