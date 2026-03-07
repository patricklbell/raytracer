#pragma once

#include <vulkan/vulkan.h>

#define RT_VK_SHADER_RGEN  "shaders/rgen.spv"
#define RT_VK_SHADER_RCHIT "shaders/rchit.spv"
#define RT_VK_SHADER_RMISS "shaders/rmiss.spv"

// ============================================================================
// GPU-side structs (layouts must match the corresponding GLSL declarations)
// ============================================================================

// rchit.glsl binding 5 — MeshInfo
typedef struct RT_VK_MeshInfo RT_VK_MeshInfo;
struct RT_VK_MeshInfo {
    u32 vtx_float_offset;       // float index of this mesh's first vertex in vtx[]
    u32 idx_elem_offset;        // element index of this mesh's first index in idx[] (0 if auto_index)
    u32 vertex_stride_floats;   // floats between consecutive vertices (geo_vertex_size / 4)
    u32 normal_float_offset;    // float offset within a vertex to its normal (0 if no normal)
    u32 has_normal;             // 1 if per-vertex normals are present
    u32 auto_index;             // 1 if triangles are sequential (no index buffer)
    u32 tri_count;
    u32 vertex_count;
};

// rchit.glsl binding 6 — InstanceMeta
typedef struct RT_VK_InstanceMeta RT_VK_InstanceMeta;
struct RT_VK_InstanceMeta {
    u32 mesh_index;
    u32 material_index;
    u32 _pad0;
    u32 _pad1;
};

// rchit.glsl binding 7 — GPUMaterial (std430)
typedef struct RT_VK_GPUMaterial RT_VK_GPUMaterial;
struct RT_VK_GPUMaterial {
    f32 albedo[4];
    f32 emissive[4];
    u32 type;
    f32 roughness;
    f32 ior;
    u32 billboard;
};

// rgen.glsl / rmiss.glsl binding 1 — Camera UBO (std140)
typedef struct RT_VK_CameraUBO RT_VK_CameraUBO;
struct RT_VK_CameraUBO {
    f32  eye[4];
    f32  right[4];
    f32  up[4];
    f32  forward[4];
    f32  viewport[4];   // xyz = (plane_width, plane_height, focus_distance)
    u32  samples;
    u32  seed_offset;
    u32  max_bounces;
    u32  ortho;
    f32  defocus_x;
    f32  defocus_y;
    u32  sky;
    u32  _pad;
};
StaticAssert(sizeof(RT_VK_CameraUBO) % 16 == 0, rt_vk_camera_ubo_std140_alignment);

typedef struct RT_VK_BLASEntry RT_VK_BLASEntry;
struct RT_VK_BLASEntry {
    VkAccelerationStructureKHR handle;
    VkBuffer                   buf;
    VkDeviceMemory             mem;
    VkDeviceAddress            device_address;
    u32                        vtx_float_offset;  // into combined vtx_buf
    u32                        idx_elem_offset;   // into combined idx_buf
};

typedef struct RT_VK_Tracer RT_VK_Tracer;
struct RT_VK_Tracer {
    Arena*  arena;
    Arena*  blas_arena;
    Arena*  tlas_arena;

    u8      max_bounces;
    bool    sky;

    // Vulkan core
    VkInstance                                      instance;
    VkPhysicalDevice                                phys_device;
    VkDevice                                        device;
    VkQueue                                         queue;
    u32                                             queue_family;

    VkPhysicalDeviceMemoryProperties                mem_props;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;

    PFN_vkGetBufferDeviceAddressKHR                 pvkGetBufferDeviceAddressKHR;
    PFN_vkCreateAccelerationStructureKHR            pvkCreateAccelerationStructureKHR;
    PFN_vkDestroyAccelerationStructureKHR           pvkDestroyAccelerationStructureKHR;
    PFN_vkGetAccelerationStructureBuildSizesKHR     pvkGetAccelerationStructureBuildSizesKHR;
    PFN_vkGetAccelerationStructureDeviceAddressKHR  pvkGetAccelerationStructureDeviceAddressKHR;
    PFN_vkCmdBuildAccelerationStructuresKHR         pvkCmdBuildAccelerationStructuresKHR;
    PFN_vkGetRayTracingShaderGroupHandlesKHR        pvkGetRayTracingShaderGroupHandlesKHR;
    PFN_vkCreateRayTracingPipelinesKHR              pvkCreateRayTracingPipelinesKHR;
    PFN_vkCmdTraceRaysKHR                           pvkCmdTraceRaysKHR;

    // Command pool / buffer
    VkCommandPool   cmd_pool;
    VkCommandBuffer cmd_buf;

    // Descriptor set
    VkDescriptorSetLayout desc_layout;
    VkDescriptorPool      desc_pool;
    VkDescriptorSet       desc_set;
    VkPipelineLayout      pipeline_layout;

    // RT pipeline + shader binding table
    VkPipeline      rt_pipeline;
    VkBuffer        sbt_buf;
    VkDeviceMemory  sbt_mem;
    VkDeviceAddress sbt_addr;

    // Camera UBO (persistently host-mapped)
    VkBuffer       cam_buf;
    VkDeviceMemory cam_mem;
    void*          cam_mapped;

    // Output storage image (rgba32f, resized lazily in rt_tracer_cast)
    VkImage        out_image;
    VkDeviceMemory out_image_mem;
    VkImageView    out_image_view;
    u32            out_width;
    u32            out_height;

    // CPU readback staging buffer
    VkBuffer       stage_buf;
    VkDeviceMemory stage_mem;
    u64            stage_size;

    // vertex / index GPU buffers
    VkBuffer         vtx_buf;
    VkDeviceMemory   vtx_mem;
    VkBuffer         idx_buf;
    VkDeviceMemory   idx_mem;

    // BLAS
    RT_VK_BLASEntry* blas_entries;
    u32              blas_count;

    // TLAS
    VkAccelerationStructureKHR tlas_handle;
    VkBuffer                   tlas_buf;
    VkDeviceMemory             tlas_mem;
    VkBuffer                   tlas_instance_buf;   // VkAccelerationStructureInstanceKHR[]
    VkDeviceMemory             tlas_instance_mem;
    u32                        tlas_instance_count;

    // Scene descriptor buffers
    VkBuffer       mesh_info_buf;
    VkDeviceMemory mesh_info_mem;
    VkBuffer       inst_meta_buf;
    VkDeviceMemory inst_meta_mem;
    VkBuffer       mat_buf;
    VkDeviceMemory mat_mem;
};

internal RT_VK_Tracer* rt_vk_handle_to_tracer(RT_Handle handle);
internal RT_Handle     rt_vk_tracer_to_handle(RT_VK_Tracer* tracer);
