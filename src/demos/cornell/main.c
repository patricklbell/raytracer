#include "demos/demos_inc.h"
#include "demos/demos_inc.c"

static RT_Handle add_lambertian_billboard_material(RT_World* world, vec3_f32 albedo) {
    RT_Handle mat = rt_world_add_material(world);
    RT_Material* mat_ptr = rt_world_resolve_material(world, mat);
    mat_ptr->type = RT_MaterialType_Lambertian;
    mat_ptr->albedo = albedo;
    mat_ptr->billboard = true;

    return mat;
}

static RT_Handle add_light_material(RT_World* world, vec3_f32 emissive) {
    RT_Handle mat = rt_world_add_material(world);
    RT_Material* mat_ptr = rt_world_resolve_material(world, mat);
    mat_ptr->type = RT_MaterialType_Light;
    mat_ptr->emissive = emissive;

    return mat;
}

static void add_quad(RT_World* world, RT_Handle mesh, RT_Handle mat, vec3_f32 c, vec3_f32 u, vec3_f32 v) {
    const vec3_f32 quad_n = (vec3_f32){.x=0,.y=1,.z=0};

    vec3_f32 n = cross_3f32(u, v);
    vec4_f32 r = make_a_to_b_quat(quad_n, n);
    vec3_f32 s = make_3f32(length_3f32(u), 1, length_3f32(v));

    RT_Handle instance = rt_world_add_instance(world);
    RT_Instance* instance_ptr = rt_world_resolve_instance(world, instance);
    instance_ptr->type = RT_InstanceType_Mesh;
    instance_ptr->material = mat;
    instance_ptr->mesh.handle = mesh;
    instance_ptr->mesh.translation = c;
    instance_ptr->mesh.rotation = r;
    instance_ptr->mesh.scale = s;
}

demo_hook void render(const DEMO_Settings* settings) {
    // @note check geo packing matches array
    Assert(geo_vertex_stride(GEO_VertexAttributes_P, GEO_VertexAttributes_P) == sizeof(vec4_f32));
    Assert(geo_vertex_offset(GEO_VertexAttributes_P, GEO_VertexAttributes_P) == 0);

    const vec4_f32 box_vertices[] = {
        {-1,-1,-1,0},
        {+1,-1,-1,0},
        {+1,+1,-1,0},
        {+1,-1,+1,0},
        {+1,+1,+1,0},
        {-1,+1,-1,0},
        {-1,+1,+1,0},
        {-1,-1,+1,0},
    };
    const u32 box_indices[] = {
        0, 1, 2,
        0, 2, 5,
        7, 3, 4,
        7, 4, 6,
        1, 3, 4,
        1, 4, 2,
        0, 5, 6,
        0, 6, 7,
        5, 2, 4,
        5, 4, 6,
        0, 7, 3,
        0, 3, 1,
    };

    const vec4_f32 quad_vertices[] = {
        {-1, 0, -1, 0},
        {+1, 0, -1, 0},
        {+1, 0, +1, 0},
        {-1, 0, +1, 0},
    };
    const u32 quad_indices[] = {
        0, 1, 2,
        0, 2, 3,
    };

    {DeferResource(RT_World* world = rt_make_world((RT_WorldSettings){}), rt_world_cleanup(world)) {
        RT_TracerSettings tsettings = get_rt_tracer_settings(settings, (DEMO_ExtraTracerSettings){.no_sky=true});
        {DeferResource(RT_Handle tracer = rt_make_tracer(tsettings), rt_tracer_cleanup(tracer)) {
            RT_Handle box = rt_world_add_mesh(world);
            {
                RT_Mesh* box_ptr = rt_world_resolve_mesh(world, box);
                box_ptr->vertices = box_vertices;
                box_ptr->vertices_count = ArrayLength(box_vertices);
                box_ptr->indices = box_indices;
                box_ptr->indices_count = ArrayLength(box_indices);
                box_ptr->primitive = GEO_Primitive_TRI_LIST;
                box_ptr->attrs = GEO_VertexAttributes_P;
            }

            RT_Handle quad = rt_world_add_mesh(world);
            {
                RT_Mesh* quad_ptr = rt_world_resolve_mesh(world, quad);
                quad_ptr->vertices = quad_vertices;
                quad_ptr->vertices_count = ArrayLength(quad_vertices);
                quad_ptr->indices = quad_indices;
                quad_ptr->indices_count = ArrayLength(quad_indices);
                quad_ptr->primitive = GEO_Primitive_TRI_LIST;
                quad_ptr->attrs = GEO_VertexAttributes_P;
            }
            
            rt_tracer_build_blas(tracer, world);

            f32 extents = 277.5;
            {
                RT_Handle white = add_lambertian_billboard_material(world, make_scale_3f32(0.73));
                RT_Handle red = add_lambertian_billboard_material(world, make_3f32(0.65,0.05,0.05));
                RT_Handle green = add_lambertian_billboard_material(world, make_3f32(0.12,0.45,0.15));
                RT_Handle light = add_light_material(world, make_scale_3f32(15));

                // floor
                add_quad(world, quad, white,
                    make_3f32(0, -extents, 0),
                    make_3f32(extents, 0, 0),
                    make_3f32(0, 0, extents)
                );

                // ceiling
                add_quad(world, quad, white,
                    make_3f32(0, extents, 0),
                    make_3f32(extents, 0, 0),
                    make_3f32(0, 0, extents)
                );

                // back wall
                add_quad(world, quad, white,
                    make_3f32(0, 0, -extents),
                    make_3f32(extents, 0, 0),
                    make_3f32(0, extents, 0)
                );

                // left wall
                add_quad(world, quad, green,
                    make_3f32(-extents, 0, 0),
                    make_3f32(0, 0, extents),
                    make_3f32(0, extents, 0)
                );

                // right wall
                add_quad(world, quad, red,
                    make_3f32(extents, 0, 0),
                    make_3f32(0, 0, extents),
                    make_3f32(0, extents, 0)
                );

                // light
                add_quad(world, quad, light,
                   make_3f32(0, extents-1, 0),
                   make_3f32(extents/4.f, 0, 0),
                   make_3f32(0, 0, extents/4.f)
                );
            }

            rt_tracer_build_tlas(tracer, world);

            {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
                int width = settings->width, height = settings->height;
                vec3_f32* buffer = push_array(scratch.arena, vec3_f32, width*height);
                
                RT_CastSettings csettings = get_rt_cast_settings(settings,
                    make_3f32(0, 0, 2.f*extents), make_3f32(0, 0, 0),
                    (DEMO_ExtraCastSettings){.vfov=DegreesToRad(40)}
                );
                rt_tracer_cast(tracer, csettings, buffer, width, height);

                stbi_write_hdr(settings->out.cstr, width, height, 3, &buffer[0].v[0]);
            }}}
        }}
    }
}