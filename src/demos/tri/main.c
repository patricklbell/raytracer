#include "demos/demos_inc.h"
#include "demos/demos_inc.c"

demo_hook void render(const DEMO_Settings* settings) {
    // @note winding order should not matter
    const vec4_f32 tri_vertices[] = {
        {-0.5f, -sqrt_f32(3.f)/4.f, 0.f},
        {+0.5f, -sqrt_f32(3.f)/4.f, 0.f},
        {+0.0f, +sqrt_f32(3.f)/4.f, 0.f}
    };
    // @note check geo packing matches array
    Assert(geo_vertex_stride(GEO_VertexAttributes_P, GEO_VertexAttributes_P) == sizeof(tri_vertices[0]));

    {DeferResource(RT_World* world = rt_make_world((RT_WorldSettings){}), rt_world_cleanup(world)) {
        RT_Handle material = rt_world_add_material(world);
        RT_Material* material_ptr = rt_world_resolve_material(world, material);
        material_ptr->type = RT_MaterialType_Lambertian;
        material_ptr->albedo = make_scale_3f32(0.5f);

        RT_Handle entity = rt_world_add_entity(world);
        RT_Entity* entity_ptr = rt_world_resolve_entity(world, entity);
        entity_ptr->type = RT_EntityType_Mesh;
        entity_ptr->material = material;
        entity_ptr->mesh.vertices = tri_vertices;
        entity_ptr->mesh.vertices_count = ArrayLength(tri_vertices);
        entity_ptr->mesh.primitive = GEO_Primitive_TRI_LIST;
        entity_ptr->mesh.attrs = GEO_VertexAttributes_P;

        {DeferResource(RT_Handle tracer = rt_make_tracer(get_rt_tracer_settings(settings)), rt_tracer_cleanup(tracer)) {
            rt_tracer_load_world(tracer, world);

            {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
                int width = settings->width, height = settings->height;
                vec3_f32* buffer = push_array(scratch.arena, vec3_f32, width*height);
                
                RT_CastSettings csettings = get_rt_cast_settings(settings,
                    make_3f32(0,0,-1), make_3f32(0,0,0),
                    (DEMO_ExtraCastSettings){.orthographic=true}
                );
                rt_tracer_cast(tracer, csettings, buffer, width, height);

                stbi_write_hdr(settings->out.cstr, width, height, 3, &buffer[0].v[0]);
            }}}
        }}
    }
}