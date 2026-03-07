// ============================================================================
// helpers
// ============================================================================

internal RT_VK_Tracer* rt_vk_handle_to_tracer(RT_Handle handle) {
    return (RT_VK_Tracer*)handle.v64[0];
}
internal RT_Handle rt_vk_tracer_to_handle(RT_VK_Tracer* tracer) {
    RT_Handle h = zero_struct;
    h.v64[0] = (u64)tracer;
    return h;
}

#define VK_CHECK(r)  Assert((r) == VK_SUCCESS)

// Load a device-level KHR function pointer into the tracer struct.
// Usage: RT_VK_LOAD(t, GetBufferDeviceAddressKHR)
//   → t->pvkGetBufferDeviceAddressKHR = vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddressKHR")
#define RT_VK_LOAD(t, sym) \
    do { \
        (t)->pvk##sym = (PFN_vk##sym)vkGetDeviceProcAddr((t)->device, "vk" #sym); \
        Assert((t)->pvk##sym != NULL); \
    } while (0)

// Find a memory type index satisfying type_filter bits and required property flags.
static u32 rt_vk_find_memory(RT_VK_Tracer* t, u32 type_filter, VkMemoryPropertyFlags props) {
    for (u32 i = 0; i < t->mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) &&
            (t->mem_props.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    Assert(!"No suitable Vulkan memory type found");
    return 0;
}

// Create a VkBuffer, allocate and bind memory.
// If usage includes SHADER_DEVICE_ADDRESS_BIT, device address allocation is requested.
static void rt_vk_create_buf(RT_VK_Tracer* t,
    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_props,
    VkBuffer* out_buf, VkDeviceMemory* out_mem)
{
    Assert(size > 0);
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = size, .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1, .pQueueFamilyIndices = &t->queue_family,
    };
    VK_CHECK(vkCreateBuffer(t->device, &bci, NULL, out_buf));

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(t->device, *out_buf, &req);

    VkMemoryAllocateFlagsInfo afi = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
                     ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
                     : (VkMemoryAllocateFlags)0,
    };
    VkMemoryAllocateInfo mai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = &afi,
        .allocationSize  = req.size,
        .memoryTypeIndex = rt_vk_find_memory(t, req.memoryTypeBits, mem_props),
    };
    VK_CHECK(vkAllocateMemory(t->device, &mai, NULL, out_mem));
    VK_CHECK(vkBindBufferMemory(t->device, *out_buf, *out_mem, 0));
}

static void rt_vk_destroy_buf(VkDevice dev, VkBuffer buf, VkDeviceMemory mem) {
    if (buf != VK_NULL_HANDLE) vkDestroyBuffer(dev, buf, NULL);
    if (mem != VK_NULL_HANDLE) vkFreeMemory(dev, mem, NULL);
}

// Get the device address of a buffer.
static VkDeviceAddress rt_vk_buf_addr(RT_VK_Tracer* t, VkBuffer buf) {
    VkBufferDeviceAddressInfo bai = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buf,
    };
    return t->pvkGetBufferDeviceAddressKHR(t->device, &bai);
}

// Begin a one-time command buffer.
static void rt_vk_cmd_begin(RT_VK_Tracer* t) {
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(t->cmd_buf, &bi));
}

// End, submit, and wait for the command buffer.
static void rt_vk_cmd_submit_wait(RT_VK_Tracer* t) {
    VK_CHECK(vkEndCommandBuffer(t->cmd_buf));
    VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    VK_CHECK(vkCreateFence(t->device, &fi, NULL, &fence));
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &t->cmd_buf,
    };
    VK_CHECK(vkQueueSubmit(t->queue, 1, &si, fence));
    VK_CHECK(vkWaitForFences(t->device, 1, &fence, VK_TRUE, UINT64_MAX));
    vkDestroyFence(t->device, fence, NULL);
    // Reset for next recording
    VK_CHECK(vkResetCommandBuffer(t->cmd_buf, 0));
}

// Upload CPU data to a device-local buffer via a transient host-visible staging buffer.
// extra_usage is OR'd with VK_BUFFER_USAGE_TRANSFER_DST_BIT.
static void rt_vk_upload(RT_VK_Tracer* t,
    const void* data, VkDeviceSize size, VkBufferUsageFlags extra_usage,
    VkBuffer* out_buf, VkDeviceMemory* out_mem)
{
    VkBuffer stg = VK_NULL_HANDLE; VkDeviceMemory stg_mem = VK_NULL_HANDLE;
    rt_vk_create_buf(t, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stg, &stg_mem);
    void* mapped;
    VK_CHECK(vkMapMemory(t->device, stg_mem, 0, size, 0, &mapped));
    memcpy(mapped, data, (size_t)size);
    vkUnmapMemory(t->device, stg_mem);

    rt_vk_create_buf(t, size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | extra_usage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        out_buf, out_mem);

    rt_vk_cmd_begin(t);
    VkBufferCopy copy = {.size = size};
    vkCmdCopyBuffer(t->cmd_buf, stg, *out_buf, 1, &copy);
    rt_vk_cmd_submit_wait(t);
    rt_vk_destroy_buf(t->device, stg, stg_mem);
}

// Load a SPIR-V file and create a VkShaderModule.
static VkShaderModule rt_vk_load_spv(VkDevice dev, const char* path) {
    FILE* f = fopen(path, "rb");
    Assert(f != NULL);  // shader SPVs must be compiled before running (use --vulkan in build.sh)
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    Assert(sz > 0 && (sz % 4) == 0);
    u32* code = (u32*)malloc((size_t)sz);
    Assert(code != NULL);
    size_t rd = fread(code, 1, (size_t)sz, f);
    fclose(f);
    Assert((long)rd == sz);
    VkShaderModuleCreateInfo smci = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = (size_t)sz, .pCode = code,
    };
    VkShaderModule mod = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(dev, &smci, NULL, &mod);
    free(code);
    VK_CHECK(result);
    return mod;
}

// Build a row-major 3×4 TRS matrix for VkAccelerationStructureInstanceKHR.transform.
// The resulting matrix performs: world_pos = M * [object_pos, 1]
static void rt_vk_make_trs(vec3_f32 tr, vec4_f32 q, vec3_f32 sc, VkTransformMatrixKHR* out) {
    f32 qx = q.x, qy = q.y, qz = q.z, qw = q.w;
    f32 x2 = 2*qx*qx, y2 = 2*qy*qy, z2 = 2*qz*qz;
    f32 xy = 2*qx*qy, xz = 2*qx*qz, yz = 2*qy*qz;
    f32 wx = 2*qw*qx, wy = 2*qw*qy, wz = 2*qw*qz;
    // Row 0
    out->matrix[0][0] = (1 - y2 - z2)  * sc.x;
    out->matrix[0][1] = (xy - wz)      * sc.y;
    out->matrix[0][2] = (xz + wy)      * sc.z;
    out->matrix[0][3] = tr.x;
    // Row 1
    out->matrix[1][0] = (xy + wz)      * sc.x;
    out->matrix[1][1] = (1 - x2 - z2)  * sc.y;
    out->matrix[1][2] = (yz - wx)      * sc.z;
    out->matrix[1][3] = tr.y;
    // Row 2
    out->matrix[2][0] = (xz - wy)      * sc.x;
    out->matrix[2][1] = (yz + wx)      * sc.y;
    out->matrix[2][2] = (1 - x2 - y2)  * sc.z;
    out->matrix[2][3] = tr.z;
}

static void rt_vk_destroy_blas(RT_VK_Tracer* t) {
    for (u32 i = 0; i < t->blas_count; i++) {
        t->pvkDestroyAccelerationStructureKHR(t->device, t->blas_entries[i].handle, NULL);
        rt_vk_destroy_buf(t->device, t->blas_entries[i].buf, t->blas_entries[i].mem);
    }
    rt_vk_destroy_buf(t->device, t->vtx_buf, t->vtx_mem);
    rt_vk_destroy_buf(t->device, t->idx_buf, t->idx_mem);
    t->blas_entries = NULL;
    t->blas_count   = 0;
    t->vtx_buf = VK_NULL_HANDLE; t->vtx_mem = VK_NULL_HANDLE;
    t->idx_buf = VK_NULL_HANDLE; t->idx_mem = VK_NULL_HANDLE;
    arena_clear(t->blas_arena);
}

static void rt_vk_destroy_tlas(RT_VK_Tracer* t) {
    if (t->tlas_handle != VK_NULL_HANDLE) {
        t->pvkDestroyAccelerationStructureKHR(t->device, t->tlas_handle, NULL);
        t->tlas_handle = VK_NULL_HANDLE;
    }
    rt_vk_destroy_buf(t->device, t->tlas_buf,          t->tlas_mem);
    rt_vk_destroy_buf(t->device, t->tlas_instance_buf,  t->tlas_instance_mem);
    rt_vk_destroy_buf(t->device, t->mesh_info_buf,      t->mesh_info_mem);
    rt_vk_destroy_buf(t->device, t->inst_meta_buf,      t->inst_meta_mem);
    rt_vk_destroy_buf(t->device, t->mat_buf,            t->mat_mem);
    t->tlas_buf = VK_NULL_HANDLE; t->tlas_mem = VK_NULL_HANDLE;
    t->tlas_instance_buf = VK_NULL_HANDLE; t->tlas_instance_mem = VK_NULL_HANDLE;
    t->mesh_info_buf = VK_NULL_HANDLE; t->mesh_info_mem = VK_NULL_HANDLE;
    t->inst_meta_buf = VK_NULL_HANDLE; t->inst_meta_mem = VK_NULL_HANDLE;
    t->mat_buf = VK_NULL_HANDLE; t->mat_mem = VK_NULL_HANDLE;
    t->tlas_instance_count = 0;
    arena_clear(t->tlas_arena);
}

static void rt_vk_destroy_output_image(RT_VK_Tracer* t) {
    if (t->out_image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(t->device, t->out_image_view, NULL);
        t->out_image_view = VK_NULL_HANDLE;
    }
    if (t->out_image != VK_NULL_HANDLE) {
        vkDestroyImage(t->device, t->out_image, NULL);
        t->out_image = VK_NULL_HANDLE;
    }
    if (t->out_image_mem != VK_NULL_HANDLE) {
        vkFreeMemory(t->device, t->out_image_mem, NULL);
        t->out_image_mem = VK_NULL_HANDLE;
    }
    rt_vk_destroy_buf(t->device, t->stage_buf, t->stage_mem);
    t->stage_buf = VK_NULL_HANDLE; t->stage_mem = VK_NULL_HANDLE;
    t->stage_size = 0;
    t->out_width = t->out_height = 0;
}

// ============================================================================
// descriptor set update helpers
// ============================================================================

// Write binding 2 — output image descriptor.
static void rt_vk_update_image_descriptor(RT_VK_Tracer* t) {
    VkDescriptorImageInfo img_info = {
        .sampler     = VK_NULL_HANDLE,
        .imageView   = t->out_image_view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    VkWriteDescriptorSet w = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = t->desc_set,
        .dstBinding      = 2,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo      = &img_info,
    };
    vkUpdateDescriptorSets(t->device, 1, &w, 0, NULL);
}

// Write bindings 0,1,3-7 — TLAS, camera UBO, and scene buffers.
static void rt_vk_update_scene_descriptors(RT_VK_Tracer* t) {
    // Binding 0 — TLAS
    VkWriteDescriptorSetAccelerationStructureKHR as_info = {
        .sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1,
        .pAccelerationStructures    = &t->tlas_handle,
    };
    // Binding 1 — camera UBO
    VkDescriptorBufferInfo cam_info = {.buffer = t->cam_buf, .offset = 0, .range = VK_WHOLE_SIZE};
    // Binding 3 — vtx
    VkDescriptorBufferInfo vtx_info = {.buffer = t->vtx_buf, .offset = 0, .range = VK_WHOLE_SIZE};
    // Binding 4 — idx
    VkDescriptorBufferInfo idx_info = {.buffer = t->idx_buf, .offset = 0, .range = VK_WHOLE_SIZE};
    // Binding 5 — mesh info
    VkDescriptorBufferInfo mi_info  = {.buffer = t->mesh_info_buf, .offset = 0, .range = VK_WHOLE_SIZE};
    // Binding 6 — instance meta
    VkDescriptorBufferInfo im_info  = {.buffer = t->inst_meta_buf, .offset = 0, .range = VK_WHOLE_SIZE};
    // Binding 7 — materials
    VkDescriptorBufferInfo mat_info = {.buffer = t->mat_buf, .offset = 0, .range = VK_WHOLE_SIZE};

    VkWriteDescriptorSet writes[7];
    writes[0] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = &as_info,
        .dstSet = t->desc_set, .dstBinding = 0, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
    };
    writes[1] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = t->desc_set, .dstBinding = 1, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &cam_info,
    };
    writes[2] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = t->desc_set, .dstBinding = 3, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &vtx_info,
    };
    writes[3] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = t->desc_set, .dstBinding = 4, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &idx_info,
    };
    writes[4] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = t->desc_set, .dstBinding = 5, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &mi_info,
    };
    writes[5] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = t->desc_set, .dstBinding = 6, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &im_info,
    };
    writes[6] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = t->desc_set, .dstBinding = 7, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &mat_info,
    };
    vkUpdateDescriptorSets(t->device, ArrayLength(writes), writes, 0, NULL);
}

// ============================================================================
// tracer
// ============================================================================
rt_hook RT_Handle rt_make_tracer(RT_TracerSettings settings) {
    Assert(settings.max_bounces < RT_MAX_MAX_BOUNCES);

    Arena* arena = arena_alloc();
    
    RT_VK_Tracer* t = push_array(arena, RT_VK_Tracer, 1);
    t->arena        = arena;
    t->blas_arena   = arena_alloc();
    t->tlas_arena   = arena_alloc();
    t->max_bounces  = settings.max_bounces;
    t->sky          = settings.sky;

    // ---- Vulkan instance (headless, no WSI extensions) ----
    {
        VkApplicationInfo ai = {
            .sType          = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "raytracer",
            .apiVersion     = VK_API_VERSION_1_3,
        };
        VkInstanceCreateInfo ici = {
            .sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &ai,
        };
        VK_CHECK(vkCreateInstance(&ici, NULL, &t->instance));
    }

    // ---- Physical device ----
    {
        u32 dev_count = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(t->instance, &dev_count, NULL));
        Assert(dev_count > 0);
        DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
            VkPhysicalDevice* devs = push_array_no_zero(scratch.arena, VkPhysicalDevice, dev_count);
            VK_CHECK(vkEnumeratePhysicalDevices(t->instance, &dev_count, devs));
            // Prefer a discrete GPU with RT support; fall back to the first device.
            t->phys_device = devs[0];
            for (u32 i = 0; i < dev_count; i++) {
                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties(devs[i], &props);
                if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    t->phys_device = devs[i];
                    break;
                }
            }
        }

        t->rt_props = (VkPhysicalDeviceRayTracingPipelinePropertiesKHR){
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
        };
        VkPhysicalDeviceProperties2 pd2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &t->rt_props,
        };
        vkGetPhysicalDeviceProperties2(t->phys_device, &pd2);
        vkGetPhysicalDeviceMemoryProperties(t->phys_device, &t->mem_props);
    }

    // ---- Queue family (first compute/graphics family, no present required) ----
    {
        u32 qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(t->phys_device, &qf_count, NULL);
        DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
            VkQueueFamilyProperties* qfps =
                push_array_no_zero(scratch.arena, VkQueueFamilyProperties, qf_count);
            vkGetPhysicalDeviceQueueFamilyProperties(t->phys_device, &qf_count, qfps);
            t->queue_family = (u32)-1;
            for (u32 i = 0; i < qf_count; i++) {
                if (qfps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    t->queue_family = i;
                    break;
                }
            }
            Assert(t->queue_family != (u32)-1);
        }
    }

    // ---- Logical device with RT extension chain ----
    {
        const char* exts[] = {
            "VK_KHR_ray_tracing_pipeline",
            "VK_KHR_acceleration_structure",
            "VK_KHR_buffer_device_address",
            "VK_KHR_deferred_host_operations",
            "VK_EXT_descriptor_indexing",
            "VK_KHR_maintenance3",
            "VK_KHR_spirv_1_4",
            "VK_KHR_shader_float_controls",
        };

        VkPhysicalDeviceBufferDeviceAddressFeatures buf_addr_feat = {
            .sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
            .bufferDeviceAddress = VK_TRUE,
        };
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accel_feat = {
            .sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
            .pNext                 = &buf_addr_feat,
            .accelerationStructure = VK_TRUE,
        };
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_feat = {
            .sType              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
            .pNext              = &accel_feat,
            .rayTracingPipeline = VK_TRUE,
        };

        f32 prio = 1.f;
        VkDeviceQueueCreateInfo qci = {
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = t->queue_family,
            .queueCount       = 1, .pQueuePriorities = &prio,
        };
        VkDeviceCreateInfo dci = {
            .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext                   = &rt_feat,
            .queueCreateInfoCount    = 1, .pQueueCreateInfos = &qci,
            .enabledExtensionCount   = ArrayLength(exts),
            .ppEnabledExtensionNames = exts,
        };
        VK_CHECK(vkCreateDevice(t->phys_device, &dci, NULL, &t->device));
        vkGetDeviceQueue(t->device, t->queue_family, 0, &t->queue);
    }

    // ---- Load KHR extension function pointers ----
    RT_VK_LOAD(t, GetBufferDeviceAddressKHR);
    RT_VK_LOAD(t, CreateAccelerationStructureKHR);
    RT_VK_LOAD(t, DestroyAccelerationStructureKHR);
    RT_VK_LOAD(t, GetAccelerationStructureBuildSizesKHR);
    RT_VK_LOAD(t, GetAccelerationStructureDeviceAddressKHR);
    RT_VK_LOAD(t, CmdBuildAccelerationStructuresKHR);
    RT_VK_LOAD(t, GetRayTracingShaderGroupHandlesKHR);
    RT_VK_LOAD(t, CreateRayTracingPipelinesKHR);
    RT_VK_LOAD(t, CmdTraceRaysKHR);

    // ---- Command pool + single reusable command buffer ----
    {
        VkCommandPoolCreateInfo cpci = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = t->queue_family,
        };
        VK_CHECK(vkCreateCommandPool(t->device, &cpci, NULL, &t->cmd_pool));
        VkCommandBufferAllocateInfo cbai = {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = t->cmd_pool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VK_CHECK(vkAllocateCommandBuffers(t->device, &cbai, &t->cmd_buf));
    }

    // ---- Descriptor set layout (bindings 0-7, all in set 0) ----
    {
        VkDescriptorSetLayoutBinding bindings[8] = {
            // 0: TLAS
            {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1,
             VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, NULL},
            // 1: Camera UBO
            {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
             VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR
             | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, NULL},
            // 2: Output image
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
             VK_SHADER_STAGE_RAYGEN_BIT_KHR, NULL},
            // 3: Vertex buffer
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
             VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, NULL},
            // 4: Index buffer
            {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
             VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, NULL},
            // 5: Mesh info
            {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
             VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, NULL},
            // 6: Instance meta
            {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
             VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, NULL},
            // 7: Materials
            {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
             VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, NULL},
        };
        VkDescriptorSetLayoutCreateInfo lci = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = ArrayLength(bindings), .pBindings = bindings,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(t->device, &lci, NULL, &t->desc_layout));
    }

    // ---- Descriptor pool + allocate one set ----
    {
        VkDescriptorPoolSize pool_sizes[] = {
            {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,             1},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              1},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             5},
        };
        VkDescriptorPoolCreateInfo dpci = {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets       = 1,
            .poolSizeCount = ArrayLength(pool_sizes), .pPoolSizes = pool_sizes,
        };
        VK_CHECK(vkCreateDescriptorPool(t->device, &dpci, NULL, &t->desc_pool));

        VkDescriptorSetAllocateInfo dsai = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = t->desc_pool,
            .descriptorSetCount = 1, .pSetLayouts = &t->desc_layout,
        };
        VK_CHECK(vkAllocateDescriptorSets(t->device, &dsai, &t->desc_set));
    }

    // ---- Pipeline layout ----
    {
        VkPipelineLayoutCreateInfo plci = {
            .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1, .pSetLayouts = &t->desc_layout,
        };
        VK_CHECK(vkCreatePipelineLayout(t->device, &plci, NULL, &t->pipeline_layout));
    }

    // ---- Ray tracing pipeline ----
    // Shader group layout:
    //   groups[0] = rgen  (GENERAL, stage 0)
    //   groups[1] = rmiss (GENERAL, stage 1)
    //   groups[2] = rchit (TRIANGLES_HIT_GROUP, closestHitShader = stage 2)
    {
        VkShaderModule rgen_mod  = rt_vk_load_spv(t->device, RT_VK_SHADER_RGEN);
        VkShaderModule rmiss_mod = rt_vk_load_spv(t->device, RT_VK_SHADER_RMISS);
        VkShaderModule rchit_mod = rt_vk_load_spv(t->device, RT_VK_SHADER_RCHIT);

        VkPipelineShaderStageCreateInfo stages[3] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
             VK_SHADER_STAGE_RAYGEN_BIT_KHR,      rgen_mod,  "main", NULL},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
             VK_SHADER_STAGE_MISS_BIT_KHR,        rmiss_mod, "main", NULL},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
             VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, rchit_mod, "main", NULL},
        };
        VkRayTracingShaderGroupCreateInfoKHR groups[3] = {
            // rgen
            {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, NULL,
             VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
             0, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, NULL},
            // rmiss
            {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, NULL,
             VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
             1, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, NULL},
            // rchit
            {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, NULL,
             VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
             VK_SHADER_UNUSED_KHR, 2, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, NULL},
        };
        VkRayTracingPipelineCreateInfoKHR rtpci = {
            .sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
            .stageCount                   = 3, .pStages  = stages,
            .groupCount                   = 3, .pGroups  = groups,
            .maxPipelineRayRecursionDepth = 1,
            .layout                       = t->pipeline_layout,
        };
        VK_CHECK(t->pvkCreateRayTracingPipelinesKHR(
            t->device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rtpci, NULL, &t->rt_pipeline));

        vkDestroyShaderModule(t->device, rgen_mod,  NULL);
        vkDestroyShaderModule(t->device, rmiss_mod, NULL);
        vkDestroyShaderModule(t->device, rchit_mod, NULL);
    }

    // ---- Shader binding table (SBT) ----
    // Layout: [rgen | rmiss | rchit], each entry padded to shaderGroupBaseAlignment.
    {
        u32 handle_size    = t->rt_props.shaderGroupHandleSize;
        u32 handle_stride  = t->rt_props.shaderGroupBaseAlignment;
        u32 sbt_size       = 3u * handle_stride;

        char* handles = (char*)malloc(3u * handle_size);
        Assert(handles != NULL);
        VK_CHECK(t->pvkGetRayTracingShaderGroupHandlesKHR(
            t->device, t->rt_pipeline, 0, 3, 3u * handle_size, handles));

        rt_vk_create_buf(t, sbt_size,
            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
            | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &t->sbt_buf, &t->sbt_mem);

        char* sbt_mapped;
        VK_CHECK(vkMapMemory(t->device, t->sbt_mem, 0, sbt_size, 0, (void**)&sbt_mapped));
        for (u32 i = 0; i < 3; i++) {
            memcpy(sbt_mapped + i * handle_stride, handles + i * handle_size, handle_size);
        }
        vkUnmapMemory(t->device, t->sbt_mem);
        free(handles);

        t->sbt_addr = rt_vk_buf_addr(t, t->sbt_buf);
    }

    // ---- Camera UBO (host-visible, persistently mapped) ----
    {
        rt_vk_create_buf(t, sizeof(RT_VK_CameraUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &t->cam_buf, &t->cam_mem);
        VK_CHECK(vkMapMemory(t->device, t->cam_mem, 0, sizeof(RT_VK_CameraUBO), 0, &t->cam_mapped));
    }

    return rt_vk_tracer_to_handle(t);
}

// ============================================================================
// BLAS
// ============================================================================

rt_hook void rt_tracer_build_blas(RT_Handle handle, RT_World* world) {
    RT_VK_Tracer* t = rt_vk_handle_to_tracer(handle);

    // Free any previously built BLAS resources.
    if (t->blas_count > 0) {
        rt_vk_destroy_blas(t);
    }

    u32 mesh_count = world->meshes.length;
    if (mesh_count == 0) {
        return;
    }

    // ---- First pass: compute combined buffer sizes ----
    u64 total_vtx_bytes = 0;
    u64 total_idx_bytes = 0;
    {
        for EachList(node, RT_MeshNode, world->meshes.first) {
            const RT_Mesh* m = &node->v;
            total_vtx_bytes += (u64)m->vertices_count * geo_vertex_size(m->attrs);
            if (m->indices_count > 0)
                total_idx_bytes += (u64)m->indices_count * sizeof(u32);
        }
    }

    // ---- Upload combined vertex buffer ----
    {
        DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
            u8* combined = push_array_no_zero(scratch.arena, u8, total_vtx_bytes);
            u64 offset = 0;
            for EachList(node, RT_MeshNode, world->meshes.first) {
                const RT_Mesh* m = &node->v;
                u64 sz = (u64)m->vertices_count * geo_vertex_size(m->attrs);
                memcpy(combined + offset, m->vertices, (size_t)sz);
                offset += sz;
            }
            rt_vk_upload(t, combined, (VkDeviceSize)total_vtx_bytes,
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                &t->vtx_buf, &t->vtx_mem);
        }
    }

    // ---- Upload combined index buffer (if any mesh has indices) ----
    if (total_idx_bytes > 0) {
        DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
            u32* combined = push_array_no_zero(scratch.arena, u32, total_idx_bytes / sizeof(u32));
            u64 offset = 0;
            for EachList(node, RT_MeshNode, world->meshes.first) {
                const RT_Mesh* m = &node->v;
                if (m->indices_count > 0) {
                    u64 sz = (u64)m->indices_count * sizeof(u32);
                    memcpy((u8*)combined + offset, m->indices, (size_t)sz);
                    offset += sz;
                }
            }
            rt_vk_upload(t, combined, (VkDeviceSize)total_idx_bytes,
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                &t->idx_buf, &t->idx_mem);
        }
    } else {
        // Allocate a dummy 4-byte idx buffer so the descriptor write is always valid.
        u32 dummy = 0;
        rt_vk_upload(t, &dummy, sizeof(u32),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            &t->idx_buf, &t->idx_mem);
    }

    VkDeviceAddress vtx_base = rt_vk_buf_addr(t, t->vtx_buf);
    VkDeviceAddress idx_base = (total_idx_bytes > 0) ? rt_vk_buf_addr(t, t->idx_buf) : 0;

    // ---- Allocate BLAS entry array ----
    t->blas_count   = mesh_count;
    t->blas_entries = push_array_no_zero(t->blas_arena, RT_VK_BLASEntry, mesh_count);

    // ---- Per-mesh scratch buffers for the batch build ----
    DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
        VkBuffer*       scratch_bufs = push_array_no_zero(scratch.arena, VkBuffer, mesh_count);
        VkDeviceMemory* scratch_mems = push_array_no_zero(scratch.arena, VkDeviceMemory, mesh_count);

        VkAccelerationStructureBuildGeometryInfoKHR* build_infos =
            push_array_no_zero(scratch.arena, VkAccelerationStructureBuildGeometryInfoKHR, mesh_count);
        VkAccelerationStructureGeometryKHR* geoms =
            push_array_no_zero(scratch.arena, VkAccelerationStructureGeometryKHR, mesh_count);
        const VkAccelerationStructureBuildRangeInfoKHR** range_ptrs =
            push_array_no_zero(scratch.arena, const VkAccelerationStructureBuildRangeInfoKHR*, mesh_count);
        VkAccelerationStructureBuildRangeInfoKHR* ranges =
            push_array_no_zero(scratch.arena, VkAccelerationStructureBuildRangeInfoKHR, mesh_count);

        u32 vtx_float_cursor = 0;
        u32 idx_elem_cursor  = 0;
        u32 mesh_idx = 0;

        for EachList(node, RT_MeshNode, world->meshes.first) {
            RT_Mesh* m    = &node->v;
            RT_VK_BLASEntry* entry = &t->blas_entries[mesh_idx];

            u32 vtx_size_bytes   = (u32)(geo_vertex_size(m->attrs));
            u32 p_byte_offset    = (u32)(geo_vertex_offset(m->attrs, GEO_VertexAttributes_P));
            bool auto_idx        = (m->indices_count == 0);
            u32  tri_count       = auto_idx ? m->vertices_count / 3 : m->indices_count / 3;

            entry->vtx_float_offset = vtx_float_cursor;
            entry->idx_elem_offset  = idx_elem_cursor;

            // Geometry descriptor for this mesh
            VkAccelerationStructureGeometryTrianglesDataKHR tri_data = {
                .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                .vertexFormat = VK_FORMAT_R32G32B32A32_SFLOAT,  // positions stored as vec4_f32
                .vertexData   = {.deviceAddress = vtx_base
                                    + (u64)vtx_float_cursor * sizeof(f32)
                                    + p_byte_offset},
                .vertexStride = vtx_size_bytes,
                .maxVertex    = m->vertices_count - 1u,
                .indexType    = auto_idx ? VK_INDEX_TYPE_NONE_KHR : VK_INDEX_TYPE_UINT32,
                .indexData    = {.deviceAddress = auto_idx ? 0
                                    : (idx_base + (u64)idx_elem_cursor * sizeof(u32))},
                .transformData = {.deviceAddress = 0},
            };
            geoms[mesh_idx] = (VkAccelerationStructureGeometryKHR){
                .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
                .geometry     = {.triangles = tri_data},
                .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR,
            };

            // Query build sizes
            build_infos[mesh_idx] = (VkAccelerationStructureBuildGeometryInfoKHR){
                .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                .type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                .flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
                .mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
                .geometryCount = 1, .pGeometries = &geoms[mesh_idx],
            };
            VkAccelerationStructureBuildSizesInfoKHR sizes = {
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
            };
            t->pvkGetAccelerationStructureBuildSizesKHR(t->device,
                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                &build_infos[mesh_idx], &tri_count, &sizes);

            // Allocate BLAS storage buffer
            rt_vk_create_buf(t, sizes.accelerationStructureSize,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &entry->buf, &entry->mem);

            // Create the BLAS object
            VkAccelerationStructureCreateInfoKHR asci = {
                .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
                .buffer = entry->buf, .size = sizes.accelerationStructureSize,
                .type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            };
            VK_CHECK(t->pvkCreateAccelerationStructureKHR(t->device, &asci, NULL, &entry->handle));

            // Get its device address
            VkAccelerationStructureDeviceAddressInfoKHR addr_info = {
                .sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
                .accelerationStructure = entry->handle,
            };
            entry->device_address =
                t->pvkGetAccelerationStructureDeviceAddressKHR(t->device, &addr_info);

            // Allocate scratch buffer for this BLAS build
            rt_vk_create_buf(t, sizes.buildScratchSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &scratch_bufs[mesh_idx], &scratch_mems[mesh_idx]);

            build_infos[mesh_idx].dstAccelerationStructure = entry->handle;
            build_infos[mesh_idx].scratchData.deviceAddress =
                rt_vk_buf_addr(t, scratch_bufs[mesh_idx]);

            ranges[mesh_idx] = (VkAccelerationStructureBuildRangeInfoKHR){
                .primitiveCount = tri_count, .primitiveOffset = 0,
                .firstVertex = 0, .transformOffset = 0,
            };
            range_ptrs[mesh_idx] = &ranges[mesh_idx];

            // Assign blas_id so rt_tracer_build_tlas can find the right entry.
            m->blas_id = mesh_idx;

            vtx_float_cursor += (u32)(m->vertices_count * geo_vertex_size(m->attrs) / sizeof(f32));
            if (!auto_idx) idx_elem_cursor += m->indices_count;
            mesh_idx++;
        }

        // ---- Record all BLAS builds in one command buffer, then submit ----
        rt_vk_cmd_begin(t);
        t->pvkCmdBuildAccelerationStructuresKHR(t->cmd_buf, mesh_count, build_infos, range_ptrs);
        rt_vk_cmd_submit_wait(t);

        // Free per-BLAS scratch buffers
        for (u32 i = 0; i < mesh_count; i++) {
            rt_vk_destroy_buf(t->device, scratch_bufs[i], scratch_mems[i]);
        }
    }
}

// ============================================================================
// rt_tracer_build_tlas
// ============================================================================

rt_hook void rt_tracer_build_tlas(RT_Handle handle, RT_World* world) {
    RT_VK_Tracer* t = rt_vk_handle_to_tracer(handle);

    // Free any previously built TLAS resources.
    rt_vk_destroy_tlas(t);

    u32 mesh_count = world->meshes.length;
    u32 mat_count  = world->materials.length;

    // Count only mesh instances (spheres are not supported in the Vulkan backend).
    u32 inst_count = 0;
    for EachList(node, RT_InstanceNode, world->instances.first) {
        if (node->v.type == RT_InstanceType_Mesh) inst_count++;
    }

    // ---- Assign GPU material indices (enumerating the material list in order) ----
    DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
        // Map from RT_Handle.v64[0] (pointer to RT_MaterialNode) -> GPU index.
        // We use a simple parallel arrays approach: materials_gpu_idx[i] = i
        // in LinkedList traversal order.
        RT_VK_GPUMaterial* gpu_mats = push_array_no_zero(scratch.arena, RT_VK_GPUMaterial, Max(mat_count, 1u));
        RT_MaterialNode**  mat_nodes = push_array_no_zero(scratch.arena, RT_MaterialNode*, Max(mat_count, 1u));
        {
            u32 mi = 0;
            for EachList(mnode, RT_MaterialNode, world->materials.first) {
                mat_nodes[mi] = mnode;
                RT_Material* m = &mnode->v;
                RT_VK_GPUMaterial* g = &gpu_mats[mi];
                g->albedo[0]  = m->albedo.x;  g->albedo[1]  = m->albedo.y;
                g->albedo[2]  = m->albedo.z;  g->albedo[3]  = 0;
                g->emissive[0]= m->emissive.x; g->emissive[1]= m->emissive.y;
                g->emissive[2]= m->emissive.z; g->emissive[3]= 0;
                g->type       = (u32)m->type;
                g->roughness  = m->roughness;
                g->ior        = m->ior;
                g->billboard  = m->billboard ? 1u : 0u;
                mi++;
            }
        }

        // Helper: find GPU index of a material handle.
        // (Linear search; acceptable since mat_count is small per scene.)
        #define FIND_MAT_IDX(handle_val) [&]() -> u32 { \
            for (u32 _mi = 0; _mi < mat_count; _mi++) { \
                if ((u64)mat_nodes[_mi] == (handle_val)) return _mi; \
            } \
            return 0; \
        }()

        // ---- Build TLAS instance array ----
        VkAccelerationStructureInstanceKHR* vk_instances =
            push_array_no_zero(scratch.arena, VkAccelerationStructureInstanceKHR, Max(inst_count, 1u));
        RT_VK_MeshInfo*    mesh_infos =
            push_array_no_zero(scratch.arena, RT_VK_MeshInfo, Max(mesh_count, 1u));
        RT_VK_InstanceMeta* inst_metas =
            push_array_no_zero(scratch.arena, RT_VK_InstanceMeta, Max(inst_count, 1u));

        // Fill mesh info array (same order as blas_entries / traversal order)
        {
            u32 vtx_float_cursor = 0;
            u32 idx_elem_cursor  = 0;
            u32 mi = 0;
            for EachList(mnode, RT_MeshNode, world->meshes.first) {
                RT_Mesh* m = &mnode->v;
                bool auto_idx = (m->indices_count == 0);
                bool has_n    = (m->attrs & GEO_VertexAttributes_N) != 0;
                u32  vstride  = (u32)(geo_vertex_size(m->attrs) / sizeof(f32));
                u32  n_off    = has_n ? (u32)(geo_vertex_offset(m->attrs, GEO_VertexAttributes_N) / sizeof(f32)) : 0u;

                mesh_infos[mi] = (RT_VK_MeshInfo){
                    .vtx_float_offset    = vtx_float_cursor,
                    .idx_elem_offset     = idx_elem_cursor,
                    .vertex_stride_floats= vstride,
                    .normal_float_offset = n_off,
                    .has_normal          = has_n ? 1u : 0u,
                    .auto_index          = auto_idx ? 1u : 0u,
                    .tri_count           = auto_idx ? m->vertices_count / 3u : m->indices_count / 3u,
                    .vertex_count        = m->vertices_count,
                };
                vtx_float_cursor += m->vertices_count * vstride;
                if (!auto_idx) idx_elem_cursor += m->indices_count;
                mi++;
            }
        }

        // Fill VkAccelerationStructureInstanceKHR and InstanceMeta arrays (mesh instances only)
        {
            u32 ii = 0;
            for EachList(inode, RT_InstanceNode, world->instances.first) {
                const RT_Instance* inst = &inode->v;
                if (inst->type != RT_InstanceType_Mesh) continue;

                RT_Mesh* mesh = rt_world_resolve_mesh(world, inst->mesh.handle);
                u32 mesh_idx  = (u32)mesh->blas_id;
                u32 mat_idx   = FIND_MAT_IDX(inst->material.v64[0]);

                VkTransformMatrixKHR xform;
                rt_vk_make_trs(inst->mesh.translation, inst->mesh.rotation, inst->mesh.scale, &xform);

                vk_instances[ii] = (VkAccelerationStructureInstanceKHR){
                    .transform                              = xform,
                    .instanceCustomIndex                    = ii,
                    .mask                                   = 0xFF,
                    .instanceShaderBindingTableRecordOffset = 0,
                    .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
                    .accelerationStructureReference         = t->blas_entries[mesh_idx].device_address,
                };
                inst_metas[ii] = (RT_VK_InstanceMeta){
                    .mesh_index     = mesh_idx,
                    .material_index = mat_idx,
                };
                ii++;
            }
        }

        #undef FIND_MAT_IDX

        t->tlas_instance_count = inst_count;

        // ---- Upload instance buffer (host-visible for TLAS build input) ----
        VkDeviceSize inst_buf_size = Max((VkDeviceSize)inst_count, 1u) * sizeof(VkAccelerationStructureInstanceKHR);
        rt_vk_create_buf(t, inst_buf_size,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
            | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &t->tlas_instance_buf, &t->tlas_instance_mem);
        if (inst_count > 0) {
            void* mapped;
            VK_CHECK(vkMapMemory(t->device, t->tlas_instance_mem, 0, inst_buf_size, 0, &mapped));
            memcpy(mapped, vk_instances, (size_t)(inst_count * sizeof(VkAccelerationStructureInstanceKHR)));
            vkUnmapMemory(t->device, t->tlas_instance_mem);
        }
        VkDeviceAddress inst_buf_addr = rt_vk_buf_addr(t, t->tlas_instance_buf);

        // ---- Build TLAS ----
        VkAccelerationStructureGeometryInstancesDataKHR inst_data = {
            .sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .arrayOfPointers = VK_FALSE,
            .data            = {.deviceAddress = inst_buf_addr},
        };
        VkAccelerationStructureGeometryKHR tlas_geom = {
            .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
            .geometry     = {.instances = inst_data},
            .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR,
        };
        u32 tlas_prim_count = Max(inst_count, 1u);
        VkAccelerationStructureBuildGeometryInfoKHR tlas_build_info = {
            .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            .flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .geometryCount = 1, .pGeometries = &tlas_geom,
        };
        VkAccelerationStructureBuildSizesInfoKHR tlas_sizes = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
        };
        t->pvkGetAccelerationStructureBuildSizesKHR(t->device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &tlas_build_info, &tlas_prim_count, &tlas_sizes);

        rt_vk_create_buf(t, tlas_sizes.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
            | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &t->tlas_buf, &t->tlas_mem);

        VkAccelerationStructureCreateInfoKHR tlas_ci = {
            .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = t->tlas_buf, .size = tlas_sizes.accelerationStructureSize,
            .type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        };
        VK_CHECK(t->pvkCreateAccelerationStructureKHR(t->device, &tlas_ci, NULL, &t->tlas_handle));

        VkBuffer       tlas_scratch_buf = VK_NULL_HANDLE;
        VkDeviceMemory tlas_scratch_mem = VK_NULL_HANDLE;
        rt_vk_create_buf(t, tlas_sizes.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &tlas_scratch_buf, &tlas_scratch_mem);

        tlas_build_info.dstAccelerationStructure   = t->tlas_handle;
        tlas_build_info.scratchData.deviceAddress  = rt_vk_buf_addr(t, tlas_scratch_buf);

        VkAccelerationStructureBuildRangeInfoKHR tlas_range = {
            .primitiveCount = inst_count, .primitiveOffset = 0,
        };
        const VkAccelerationStructureBuildRangeInfoKHR* tlas_range_ptr = &tlas_range;

        rt_vk_cmd_begin(t);
        t->pvkCmdBuildAccelerationStructuresKHR(t->cmd_buf, 1, &tlas_build_info, &tlas_range_ptr);
        rt_vk_cmd_submit_wait(t);

        rt_vk_destroy_buf(t->device, tlas_scratch_buf, tlas_scratch_mem);

        // ---- Upload scene descriptor buffers ----
        VkDeviceSize mi_size  = Max((VkDeviceSize)mesh_count, 1u) * sizeof(RT_VK_MeshInfo);
        VkDeviceSize im_size  = Max((VkDeviceSize)inst_count, 1u) * sizeof(RT_VK_InstanceMeta);
        VkDeviceSize mat_size = Max((VkDeviceSize)mat_count,  1u) * sizeof(RT_VK_GPUMaterial);

        rt_vk_upload(t, mesh_infos, mi_size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &t->mesh_info_buf, &t->mesh_info_mem);
        rt_vk_upload(t, inst_metas, im_size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &t->inst_meta_buf, &t->inst_meta_mem);
        rt_vk_upload(t, gpu_mats,  mat_size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &t->mat_buf, &t->mat_mem);
    }

    // ---- Update descriptor set (bindings 0,1,3-7) ----
    rt_vk_update_scene_descriptors(t);
}

// ============================================================================
// rt_tracer_cleanup
// ============================================================================

rt_hook void rt_tracer_cleanup(RT_Handle handle) {
    RT_VK_Tracer* t = rt_vk_handle_to_tracer(handle);

    vkDeviceWaitIdle(t->device);

    rt_vk_destroy_output_image(t);
    rt_vk_destroy_tlas(t);
    rt_vk_destroy_blas(t);

    // Camera UBO
    vkUnmapMemory(t->device, t->cam_mem);
    rt_vk_destroy_buf(t->device, t->cam_buf, t->cam_mem);

    // SBT
    rt_vk_destroy_buf(t->device, t->sbt_buf, t->sbt_mem);

    // Pipeline
    vkDestroyPipeline(t->device, t->rt_pipeline, NULL);
    vkDestroyPipelineLayout(t->device, t->pipeline_layout, NULL);

    // Descriptors
    vkDestroyDescriptorPool(t->device, t->desc_pool, NULL);
    vkDestroyDescriptorSetLayout(t->device, t->desc_layout, NULL);

    // Command
    vkFreeCommandBuffers(t->device, t->cmd_pool, 1, &t->cmd_buf);
    vkDestroyCommandPool(t->device, t->cmd_pool, NULL);

    // Device / instance
    vkDestroyDevice(t->device, NULL);
    vkDestroyInstance(t->instance, NULL);

    arena_release(t->tlas_arena);
    arena_release(t->blas_arena);
    arena_release(t->arena);
}

// ============================================================================
// rt_tracer_cast
// ============================================================================

// Create (or recreate) the rgba32f output image and its CPU readback staging buffer.
static void rt_vk_ensure_output_image(RT_VK_Tracer* t, u32 width, u32 height) {
    if (t->out_width == width && t->out_height == height) return;

    rt_vk_destroy_output_image(t);
    t->out_width  = width;
    t->out_height = height;

    // Storage image
    VkImageCreateInfo ici = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_R32G32B32A32_SFLOAT,
        .extent        = {width, height, 1},
        .mipLevels     = 1, .arrayLayers = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1, .pQueueFamilyIndices = &t->queue_family,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(t->device, &ici, NULL, &t->out_image));

    VkMemoryRequirements img_req;
    vkGetImageMemoryRequirements(t->device, t->out_image, &img_req);
    VkMemoryAllocateInfo img_mai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = img_req.size,
        .memoryTypeIndex = rt_vk_find_memory(t, img_req.memoryTypeBits,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VK_CHECK(vkAllocateMemory(t->device, &img_mai, NULL, &t->out_image_mem));
    VK_CHECK(vkBindImageMemory(t->device, t->out_image, t->out_image_mem, 0));

    VkImageViewCreateInfo ivci = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = t->out_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = VK_FORMAT_R32G32B32A32_SFLOAT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VK_CHECK(vkCreateImageView(t->device, &ivci, NULL, &t->out_image_view));

    // Transition image to VK_IMAGE_LAYOUT_GENERAL
    rt_vk_cmd_begin(t);
    VkImageMemoryBarrier barrier = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = 0,
        .dstAccessMask    = 0,
        .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout        = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image            = t->out_image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(t->cmd_buf,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);
    rt_vk_cmd_submit_wait(t);

    // CPU readback staging buffer (RGBA32F = 16 bytes/pixel)
    t->stage_size = (u64)width * height * 4u * sizeof(f32);
    rt_vk_create_buf(t, (VkDeviceSize)t->stage_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &t->stage_buf, &t->stage_mem);

    // Update binding 2
    rt_vk_update_image_descriptor(t);
}

rt_hook void rt_tracer_cast(RT_Handle handle, RT_CastSettings settings, vec3_f32* out_radiance, int width, int height) {
    RT_VK_Tracer* t = rt_vk_handle_to_tracer(handle);
    Assert(t->tlas_handle != VK_NULL_HANDLE && "call rt_tracer_build_tlas before rt_tracer_cast");

    // Ensure the output image exists at the requested resolution.
    rt_vk_ensure_output_image(t, (u32)width, (u32)height);

    // ---- Update camera UBO ----
    {
        RT_VK_CameraUBO ubo;
        ubo.eye[0]      = settings.eye.x;     ubo.eye[1]     = settings.eye.y;
        ubo.eye[2]      = settings.eye.z;     ubo.eye[3]     = 0;
        ubo.right[0]    = settings.right.x;   ubo.right[1]   = settings.right.y;
        ubo.right[2]    = settings.right.z;   ubo.right[3]   = 0;
        ubo.up[0]       = settings.up.x;      ubo.up[1]      = settings.up.y;
        ubo.up[2]       = settings.up.z;      ubo.up[3]      = 0;
        ubo.forward[0]  = settings.forward.x; ubo.forward[1] = settings.forward.y;
        ubo.forward[2]  = settings.forward.z; ubo.forward[3] = 0;
        ubo.viewport[0] = settings.viewport.x; ubo.viewport[1] = settings.viewport.y;
        ubo.viewport[2] = settings.viewport.z; ubo.viewport[3] = 0;
        ubo.samples       = (u32)settings.samples;
        ubo.seed_offset   = (u32)time(NULL);
        ubo.max_bounces   = (u32)t->max_bounces;
        ubo.ortho         = settings.orthographic ? 1u : 0u;
        ubo.defocus_x     = (settings.defocus && settings.defocus_disk.x > 0) ? settings.defocus_disk.x : 0.f;
        ubo.defocus_y     = (settings.defocus && settings.defocus_disk.y > 0) ? settings.defocus_disk.y : 0.f;
        ubo.sky           = t->sky ? 1u : 0u;
        ubo._pad          = 0;
        memcpy(t->cam_mapped, &ubo, sizeof(ubo));
    }

    // ---- SBT regions ----
    u32 stride = t->rt_props.shaderGroupBaseAlignment;
    VkStridedDeviceAddressRegionKHR rgen_region  = {t->sbt_addr + 0u * stride, stride, stride};
    VkStridedDeviceAddressRegionKHR rmiss_region = {t->sbt_addr + 1u * stride, stride, stride};
    VkStridedDeviceAddressRegionKHR rchit_region = {t->sbt_addr + 2u * stride, stride, stride};
    VkStridedDeviceAddressRegionKHR call_region  = zero_struct;

    // ---- Record render + readback commands ----
    rt_vk_cmd_begin(t);

    // Bind pipeline + descriptor set
    vkCmdBindPipeline(t->cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, t->rt_pipeline);
    vkCmdBindDescriptorSets(t->cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        t->pipeline_layout, 0, 1, &t->desc_set, 0, NULL);

    // Trace rays
    t->pvkCmdTraceRaysKHR(t->cmd_buf,
        &rgen_region, &rmiss_region, &rchit_region, &call_region,
        (u32)width, (u32)height, 1);

    // Barrier: storage write → transfer read
    VkImageMemoryBarrier to_src = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image            = t->out_image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(t->cmd_buf,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &to_src);

    // Copy image → staging buffer
    VkBufferImageCopy bic = {
        .bufferOffset      = 0,
        .bufferRowLength   = 0, .bufferImageHeight = 0,
        .imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageOffset       = {0, 0, 0},
        .imageExtent       = {(u32)width, (u32)height, 1},
    };
    vkCmdCopyImageToBuffer(t->cmd_buf,
        t->out_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        t->stage_buf, 1, &bic);

    // Barrier: transfer read → general (restore for next frame)
    VkImageMemoryBarrier to_general = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask    = 0,
        .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout        = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image            = t->out_image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(t->cmd_buf,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, NULL, 0, NULL, 1, &to_general);

    rt_vk_cmd_submit_wait(t);

    // ---- Copy staging buffer → out_radiance ----
    {
        f32* pixels;
        VK_CHECK(vkMapMemory(t->device, t->stage_mem, 0, (VkDeviceSize)t->stage_size, 0, (void**)&pixels));
        for (int i = 0; i < width * height; i++) {
            out_radiance[i].x = pixels[i * 4 + 0];
            out_radiance[i].y = pixels[i * 4 + 1];
            out_radiance[i].z = pixels[i * 4 + 2];
        }
        vkUnmapMemory(t->device, t->stage_mem);
    }
}
