#include "demos/demos_inc.h"
#include "demos/demos_inc.c"


static RT_Material* add_sphere(RT_World* world, vec3_f32 center, f32 radius) {
    RT_Handle material = rt_world_add_material(world);
    RT_Material* material_ptr = rt_world_resolve_material(world, material);

    RT_Handle entity = rt_world_add_entity(world);
    RT_Entity* entity_ptr = rt_world_resolve_entity(world, entity);
    entity_ptr->type = RT_EntityType_Sphere;
    entity_ptr->material = material;
    entity_ptr->sphere.center = center;
    entity_ptr->sphere.radius = radius;

    return material_ptr;
}

static void add_lambertian_sphere(RT_World* world, vec3_f32 center, f32 radius, vec3_f32 albedo) {
    RT_Material* material_ptr = add_sphere(world, center, radius);
    material_ptr->type = RT_MaterialType_Lambertian;
    material_ptr->albedo = albedo;
}
static void add_metal_sphere(RT_World* world, vec3_f32 center, f32 radius, f32 roughness) {
    RT_Material* material_ptr = add_sphere(world, center, radius);
    material_ptr->type = RT_MaterialType_Metal;
    material_ptr->roughness = roughness;
}
static void add_dieletric_sphere(RT_World* world, vec3_f32 center, f32 radius, f32 ior) {
    RT_Material* material_ptr = add_sphere(world, center, radius);
    material_ptr->type = RT_MaterialType_Dieletric;
    material_ptr->ior = ior;
}

demo_hook void render(const DEMO_Settings* settings) {
    {DeferResource(RT_World* world = rt_make_world((RT_WorldSettings){}), rt_world_cleanup(world)) {
        add_dieletric_sphere(world, make_3f32(0,0,0), 1.0, 1.52);
        // add_dieletric_sphere(world, make_3f32(0,0,0), 0.8, 1.0);
        add_lambertian_sphere(world, make_3f32(0,0,0), 0.8, make_3f32(0.8,0.5,0.5));
        add_metal_sphere(world, make_3f32(-1.6,0,0), 0.5, 0.1);
        add_metal_sphere(world, make_3f32(+1.6,0,0), 0.5, 0.9);
        // add_dieletric_sphere(world, make_3f32(0,+1.6,0), 0.5, 1.52);
        add_lambertian_sphere(world, make_3f32(0,-101,0), 100.0, make_3f32(0.5,0.6,0.5));

        RT_TracerSettings tracer_settings = {
            .max_bounces=settings->bounces
        };
        {DeferResource(RT_Handle tracer = rt_make_tracer(tracer_settings), rt_tracer_cleanup(tracer)) {    
            rt_tracer_load_world(tracer, world);
            {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
                int width = settings->width, height = settings->height;
                vec3_f32* buffer = push_array(scratch.arena, vec3_f32, width*height);
                
                f32 aspect_ratio = (f32)width/height;
                f32 vfov = DegreesToRad(45);
                f32 focus_distance = 2.5f;
                f32 defocus_angle = DegreesToRad(5);

                f32 h = tan_f32(vfov/2.f);
                f32 focus_plane_height = 2*h*focus_distance;
                f32 defocus_radius = focus_distance*tan_f32(defocus_angle/2.f);

                rt_tracer_cast(
                    tracer, 
                    (RT_CastSettings){
                        .eye=make_3f32(0,0,2.5),
                        .up=make_3f32(0,1,0),
                        .forward=make_3f32(0,0,-1),
                        .viewport=make_3f32(focus_plane_height*aspect_ratio, focus_plane_height, focus_distance),
                        .samples=settings->samples,
                        .ior=1.f,
                        .defocus=true,
                        .defocus_disk=make_2f32(defocus_radius, defocus_radius),
                    },
                    buffer, width, height
                );

                stbi_write_hdr(settings->out.cstr, width, height, 3, &buffer[0].v[0]);
            }}}
        }}
    }
}