#pragma once

#define rt_hook internal

bool rt_is_zero_handle(RT_Handle handle) {
    return handle.v64[0] == 0;
}

RT_World* rt_make_world(RT_WorldSettings settings) {
    Arena* arena = arena_alloc();
    RT_World* world = push_array(arena, RT_World, 1);
    world->arena = arena;
    return world;
}
void rt_world_cleanup(RT_World* world) {
    arena_release(world->arena);
}

RT_Handle rt_world_add_instance(RT_World* world) {
    RT_InstanceNode* node = push_array(world->arena, RT_InstanceNode, 1);
    dllist_push_front(world->instances.first, world->instances.last, node);
    world->instances.length++;
    return (RT_Handle){.v64 = {(u64)node}};
}
RT_Instance* rt_world_resolve_instance(RT_World* world, RT_Handle handle) {
    RT_InstanceNode* node = (RT_InstanceNode*)handle.v64[0];
    return &node->v;
}
void rt_world_remove_instance(RT_World* world, RT_Handle handle) {
    // @todo reclaim memory
    RT_InstanceNode* node = (RT_InstanceNode*)handle.v64[0];
    dllist_remove(world->instances.first, world->instances.last, node);
}

RT_Handle rt_world_add_material(RT_World* world) {
    RT_MaterialNode* node = push_array(world->arena, RT_MaterialNode, 1);
    dllist_push_front(world->materials.first, world->materials.last, node);
    world->materials.length++;
    return (RT_Handle){.v64 = {(u64)node}};
}
RT_Material* rt_world_resolve_material(RT_World* world, RT_Handle handle) {
    RT_MaterialNode* node = (RT_MaterialNode*)handle.v64[0];
    return &node->v;
}
void rt_world_remove_material(RT_World* world, RT_Handle handle) {
    // @todo reclaim memory
    RT_MaterialNode* node = (RT_MaterialNode*)handle.v64[0];
    dllist_remove(world->materials.first, world->materials.last, node);
}

RT_Handle rt_world_add_mesh(RT_World* world) {
    RT_MeshNode* node = push_array(world->arena, RT_MeshNode, 1);
    dllist_push_front(world->meshes.first, world->meshes.last, node);
    world->meshes.length++;
    return (RT_Handle){.v64 = {(u64)node}};
}
RT_Mesh* rt_world_resolve_mesh(RT_World* world, RT_Handle handle) {
    RT_MeshNode* node = (RT_MeshNode*)handle.v64[0];
    return &node->v;
}
void rt_world_remove_mesh(RT_World* world, RT_Handle handle) {
    // @todo reclaim memory
    RT_MeshNode* node = (RT_MeshNode*)handle.v64[0];
    dllist_remove(world->meshes.first, world->meshes.last, node);
}