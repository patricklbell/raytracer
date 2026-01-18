#include "demos/demos_inc.h"
#include "demos/demos_inc.c"

demo_hook void render(const DEMO_Settings* settings) {
    const vec4_f32 tri_vertices[] = {
        {-0.5f, -sqrt_f32(3.f)/4.f, 0.f},
        {+0.5f, -sqrt_f32(3.f)/4.f, 0.f},
        {+0.0f, +sqrt_f32(3.f)/4.f, 0.f},
    };
    // @note check geo packing matches array
    Assert(geo_vertex_stride(GEO_VertexAttributes_P, GEO_VertexAttributes_P) == sizeof(tri_vertices[0]));

    {DeferResource(RT_World* world = rt_make_world((RT_WorldSettings){}), rt_world_cleanup(world)) {
        RT_TracerSettings tsettings = get_rt_tracer_settings(settings, (DEMO_ExtraTracerSettings){});
        {DeferResource(RT_Handle tracer = rt_make_tracer(tsettings), rt_tracer_cleanup(tracer)) {
            RT_Handle tri;
            {
                tri = rt_world_add_mesh(world);
                RT_Mesh* tri_ptr = rt_world_resolve_mesh(world, tri);
                tri_ptr->vertices = tri_vertices;
                tri_ptr->vertices_count = ArrayLength(tri_vertices);
                tri_ptr->primitive = GEO_Primitive_TRI_LIST;
                tri_ptr->attrs = GEO_VertexAttributes_P;
                
                rt_tracer_build_blas(tracer, world);
            }


            {
                RT_Handle white_billboard = rt_world_add_material(world);
                RT_Material* white_billboard_ptr = rt_world_resolve_material(world, white_billboard);
                white_billboard_ptr->type = RT_MaterialType_Lambertian;
                white_billboard_ptr->albedo = make_scale_3f32(0.5f);
                white_billboard_ptr->billboard = true;

                RT_Handle instance = rt_world_add_instance(world);
                RT_Instance* instance_ptr = rt_world_resolve_instance(world, instance);
                instance_ptr->type = RT_InstanceType_Mesh;
                instance_ptr->material = white_billboard;
                instance_ptr->mesh.handle = tri;
                instance_ptr->mesh.translation = zero_struct;
                instance_ptr->mesh.rotation = make_identity_quat();
                instance_ptr->mesh.scale = make_scale_3f32(1.f);

                rt_tracer_build_tlas(tracer, world);
            }

            {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
                int width = settings->width, height = settings->height;
                vec3_f32* buffer = push_array(scratch.arena, vec3_f32, width*height);
                
                RT_CastSettings csettings = get_rt_cast_settings(settings,
                    make_3f32(0,0,1), make_3f32(0,0,0),
                    (DEMO_ExtraCastSettings){.orthographic=true}
                );
                rt_tracer_cast(tracer, csettings, buffer, width, height);

                stbi_write_hdr(settings->out.cstr, width, height, 3, &buffer[0].v[0]);
            }}}
        }}
    }
}