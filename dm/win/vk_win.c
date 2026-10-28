#include "vk_win.h"
#include "win_posix.h"
#include <vulkan/vulkan_core.h>
#include "vulkan/vulkan_win.h"
#include "../gfx/nvn/nvn_mtl.h"
#include "../gfx/nvn/nvn_obj.h"
#include "../gfx/tex/nvn_deswizzle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SWAPCHAIN_IMAGES 8
#define VK_SHADER_CACHE_MAX 128
#define VK_PIPELINE_CACHE_MAX 128
#define VK_MAX_REFL_BINDINGS 32

typedef struct {
    uint32_t binding;
    VkDescriptorType type;
    int is_ubo;
    int is_support;
    int is_driver;
    int is_c1;
    int is_c2;
    int ubo_stage;
    int ubo_index;
    int is_sampler;
    int tex_slot;
    int is_storage_img;
} vk_refl_binding_t;

typedef struct {
    char key[64];
    int is_ps;
    VkShaderModule module;
    int binding_count;
    vk_refl_binding_t bindings[VK_MAX_REFL_BINDINGS];
    uint32_t c1_offset;
    int has_c1;
    int has_color_output;
    int num_inputs;
    uint32_t input_locations[16];
} vk_shader_entry;

typedef struct {
    uint32_t fmt;
    uint32_t offset;
    uint32_t stream;
} vk_vattr_t;

typedef struct {
    uint32_t stride;
    uint32_t divisor;
} vk_vstream_t;

typedef struct {
    const void *addr;
    size_t size;
    int valid;
    size_t gpu_offset;
} vk_vbuf_t;

typedef struct {
    char vs_key[64];
    char ps_key[64];
    uint64_t v_sig;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout desc_layout_vs;
    VkDescriptorSetLayout desc_layout_ps;
    VkDescriptorSet desc_sets[MAX_SWAPCHAIN_IMAGES][2];
    int vs_sh_idx;
    int ps_sh_idx;
    int desc_initialized;
} vk_pipeline_entry;

typedef struct {
    HMODULE h_dll;
    int active;
    HWND hwnd;
    uint32_t width, height;

    // Vulkan function pointers
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
    PFN_vkCreateInstance CreateInstance;
    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
    PFN_vkCreateDevice CreateDevice;
    PFN_vkDestroyDevice DestroyDevice;
    PFN_vkGetDeviceQueue GetDeviceQueue;
    PFN_vkDeviceWaitIdle DeviceWaitIdle;
    PFN_vkCreateWin32SurfaceKHR CreateWin32SurfaceKHR;
    PFN_vkDestroySurfaceKHR DestroySurfaceKHR;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR;

    PFN_vkCreateSwapchainKHR CreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR;
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR;
    PFN_vkQueuePresentKHR QueuePresentKHR;

    PFN_vkCreateCommandPool CreateCommandPool;
    PFN_vkDestroyCommandPool DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkBeginCommandBuffer BeginCommandBuffer;
    PFN_vkEndCommandBuffer EndCommandBuffer;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage;
    PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer;
    PFN_vkQueueSubmit QueueSubmit;

    PFN_vkCreateSemaphore CreateSemaphore;
    PFN_vkDestroySemaphore DestroySemaphore;
    PFN_vkCreateFence CreateFence;
    PFN_vkDestroyFence DestroyFence;
    PFN_vkWaitForFences WaitForFences;
    PFN_vkResetFences ResetFences;
    PFN_vkGetFenceStatus GetFenceStatus;
    PFN_vkResetCommandBuffer ResetCommandBuffer;

    PFN_vkCreateBuffer CreateBuffer;
    PFN_vkDestroyBuffer DestroyBuffer;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
    PFN_vkAllocateMemory AllocateMemory;
    PFN_vkFreeMemory FreeMemory;
    PFN_vkBindBufferMemory BindBufferMemory;
    PFN_vkMapMemory MapMemory;
    PFN_vkUnmapMemory UnmapMemory;
    PFN_vkFlushMappedMemoryRanges FlushMappedMemoryRanges;
    PFN_vkInvalidateMappedMemoryRanges InvalidateMappedMemoryRanges;

    PFN_vkCreateShaderModule CreateShaderModule;
    PFN_vkDestroyShaderModule DestroyShaderModule;

    PFN_vkCreateRenderPass CreateRenderPass;
    PFN_vkDestroyRenderPass DestroyRenderPass;
    PFN_vkCreateImageView CreateImageView;
    PFN_vkDestroyImageView DestroyImageView;
    PFN_vkCreateFramebuffer CreateFramebuffer;
    PFN_vkDestroyFramebuffer DestroyFramebuffer;

    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout;
    PFN_vkCreateDescriptorPool CreateDescriptorPool;
    PFN_vkDestroyDescriptorPool DestroyDescriptorPool;
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets;
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets;

    PFN_vkCreatePipelineLayout CreatePipelineLayout;
    PFN_vkDestroyPipelineLayout DestroyPipelineLayout;
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines;
    PFN_vkDestroyPipeline DestroyPipeline;

    PFN_vkCreateImage CreateImage;
    PFN_vkDestroyImage DestroyImage;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements;
    PFN_vkBindImageMemory BindImageMemory;
    PFN_vkCreateSampler CreateSampler;
    PFN_vkDestroySampler DestroySampler;

    PFN_vkCmdBeginRenderPass CmdBeginRenderPass;
    PFN_vkCmdEndRenderPass CmdEndRenderPass;
    PFN_vkCmdBindPipeline CmdBindPipeline;
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets;
    PFN_vkCmdSetViewport CmdSetViewport;
    PFN_vkCmdSetScissor CmdSetScissor;
    PFN_vkCmdDraw CmdDraw;
    PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer;
    PFN_vkCmdDrawIndexed CmdDrawIndexed;
    PFN_vkCmdBindVertexBuffers CmdBindVertexBuffers;

    // State handles
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice phys_dev;
    VkDevice device;
    uint32_t queue_family;
    VkQueue queue;

    VkSwapchainKHR swapchain;
    uint32_t image_count;
    VkImage images[MAX_SWAPCHAIN_IMAGES];
    VkImageView image_views[MAX_SWAPCHAIN_IMAGES];
    VkFramebuffer framebuffers[MAX_SWAPCHAIN_IMAGES];
    VkRenderPass render_pass;
    VkRenderPass render_pass_resume;

    VkCommandPool cmd_pool;
    VkCommandBuffer cmd_bufs[MAX_SWAPCHAIN_IMAGES];
    VkCommandBuffer upload_cmd_buf;
    VkSemaphore sem_avail[MAX_SWAPCHAIN_IMAGES];
    VkSemaphore sem_finish[MAX_SWAPCHAIN_IMAGES];
    VkFence fences[MAX_SWAPCHAIN_IMAGES];
    uint32_t cur_frame;
    uint32_t frame_idx;

    // Staging buffer for fast upload
    VkBuffer staging_buf;
    VkDeviceMemory staging_mem;
    void *staging_mapped;
    size_t staging_size;
    size_t staging_offset;

    // Dedicated Readback Buffer for screenshots (4 MiB host-visible coherent)
    VkBuffer readback_buf;
    VkDeviceMemory readback_mem;
    void *readback_mapped;
    size_t readback_size;

    // Support Buffer (64 KB)
    VkBuffer support_buf;
    VkDeviceMemory support_mem;
    void *support_mapped;

    // Driver Constant Buffer c0 (64 KB host-visible coherent)
    VkBuffer driver_buf;
    VkDeviceMemory driver_mem;
    void *driver_mapped;

    // Zero / C1 Constant Buffer c1 / c2 (64 KB host-visible coherent)
    VkBuffer c1_buf;
    VkDeviceMemory c1_mem;
    void *c1_mapped;

    // UBO Data Buffer (8 MiB host-visible coherent buffer: 96 slots x 64KB)
    VkBuffer ubo_data_buf;
    VkDeviceMemory ubo_data_mem;
    void *ubo_data_mapped;
    const void *ubo_addrs[6][16];
    size_t ubo_sizes[6][16];
    int ubo_valid[6][16];

    // Dynamic Index Buffer (4 MiB host-visible coherent)
    VkBuffer index_buf;
    VkDeviceMemory index_mem;
    void *index_mapped;
    size_t index_buf_offset;

    // Dynamic Vertex Buffer (16 MiB host-visible coherent)
    VkBuffer vbuf;
    VkDeviceMemory vbuf_mem;
    void *vbuf_mapped;
    size_t vbuf_offset;

    // Vertex Attributes, Streams, and Buffers
    vk_vattr_t vattrs[16];
    vk_vstream_t vstreams[16];
    vk_vbuf_t vbufs[16];

    // Dummy 1x1 White Texture & Sampler
    VkImage dummy_img;
    VkDeviceMemory dummy_mem;
    VkImageView dummy_view;
    VkSampler dummy_sampler;

    // Dummy 1x1 Storage Image & View
    VkImage dummy_storage_img;
    VkDeviceMemory dummy_storage_mem;
    VkImageView dummy_storage_view;

    VkDescriptorPool desc_pool;

    // Active frame state
    int frame_active;
    uint32_t active_img_idx;
    uint32_t draw_count;
    char current_vs_key[64];
    char current_ps_key[64];
    vk_pipeline_entry *current_pipeline;
    int pixels_ready;
} vk_ctx_t;

static vk_ctx_t g_vk;
static vk_shader_entry g_vk_shaders[VK_SHADER_CACHE_MAX];
static int g_vk_shader_count = 0;

static vk_pipeline_entry g_vk_pipelines[VK_PIPELINE_CACHE_MAX];
static int g_vk_pipeline_count = 0;

static uint32_t find_mem_type(const VkPhysicalDeviceMemoryProperties *props, uint32_t type_bits, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < props->memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) && (props->memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return 0;
}

int vk_win_is_active(void) {
    return g_vk.active;
}

void vk_win_set_ubo(int stage, int index, const void *addr, size_t size) {
    if (!g_vk.active || !g_vk.ubo_data_mapped) return;
    if (stage < 0 || stage >= 6 || index < 0 || index >= 16) return;
    if (!addr || size == 0) return;

    static int logged_ubo_data = 0;
    if (logged_ubo_data < 20) {
        logged_ubo_data++;
        const float *f = (const float *)addr;
        fprintf(stderr, "[dm] [vk-win] SET_UBO stage=%d index=%d sz=%llu: f[0..3]=%g %g %g %g\n",
                stage, index, (unsigned long long)size, f[0], f[1], f[2], f[3]);
        if (stage == 0 && index == 0 && size >= 1616) {
            const uint32_t *u = (const uint32_t *)((const uint8_t *)addr + 1600);
            const float *uf = (const float *)u;
            fprintf(stderr, "[dm] [vk-win] UBO stage=0 idx=0 off=1600 (0x640): u[0..3]=%#x %#x %#x %#x (f=%g %g %g %g)\n",
                    u[0], u[1], u[2], u[3], uf[0], uf[1], uf[2], uf[3]);
        }
    }

    size_t cap = 65536;
    size_t copy_sz = size < cap ? size : cap;
    size_t slot_offset = ((size_t)stage * 16 + (size_t)index) * cap;
    memcpy((uint8_t *)g_vk.ubo_data_mapped + slot_offset, addr, copy_sz);
    if (stage == 0 && index == 0 && copy_sz >= 1608) {
        // Offset 0x640 (1600 bytes) in c0 is Maxwell driver base_vertex (0x644 is base_instance).
        // In Vulkan, gl_VertexIndex already handles base_vertex (or base_vertex is 0).
        // This word must stay 0 (nvn_mtl.m line 6400), otherwise VertexIndex + c0[0x640] flies to -2 billion.
        uint32_t *u32 = (uint32_t *)((uint8_t *)g_vk.ubo_data_mapped + slot_offset);
        u32[1600 / 4] = 0;
        u32[1604 / 4] = 0;
    }
    if (g_vk.FlushMappedMemoryRanges) {
        VkMappedMemoryRange range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = g_vk.ubo_data_mem,
            .offset = slot_offset,
            .size = VK_WHOLE_SIZE
        };
        g_vk.FlushMappedMemoryRanges(g_vk.device, 1, &range);
    }
    g_vk.ubo_sizes[stage][index] = copy_sz;
    g_vk.ubo_addrs[stage][index] = addr;
    g_vk.ubo_valid[stage][index] = 1;
}

void vk_win_set_vattr(int first, int n, const uint32_t *st) {
    if (!st || n <= 0) return;
    for (int i = 0; i < n; i++) {
        int idx = first + i;
        if (idx >= 0 && idx < 16) {
            uint32_t val = st[i];
            g_vk.vattrs[idx].fmt = val & 0xFF;
            g_vk.vattrs[idx].offset = (val >> 8) & 0x7FFFF;
            g_vk.vattrs[idx].stream = val >> 27;
        }
    }
}

void vk_win_set_vstream(int first, int n, const void *st) {
    if (!st || n <= 0) return;
    const vk_vstream_t *s = (const vk_vstream_t *)st;
    for (int i = 0; i < n; i++) {
        int idx = first + i;
        if (idx >= 0 && idx < 16) {
            g_vk.vstreams[idx].stride = s[i].stride;
            g_vk.vstreams[idx].divisor = s[i].divisor;
        }
    }
}

void vk_win_set_vbuf(int stream, const void *addr, size_t size) {
    if (stream < 0 || stream >= 16) return;
    g_vk.vbufs[stream].addr = addr;
    g_vk.vbufs[stream].size = size;
    g_vk.vbufs[stream].valid = (addr != NULL && size > 0);
}

static int vk_get_or_create_shader_idx(const char *key, int is_ps) {
    if (!g_vk.active || !key || !*key || strcmp(key, "none") == 0) return -1;
    for (int i = 0; i < g_vk_shader_count; i++) {
        if (strcmp(g_vk_shaders[i].key, key) == 0 && g_vk_shaders[i].is_ps == is_ps) {
            return i;
        }
    }
    if (g_vk_shader_count >= VK_SHADER_CACHE_MAX) return -1;

    char path[512];
    const char *pdir = getenv("DM_GAMES_DIR");
    const char *gid = getenv("DM_GAME");
    if (!pdir || !gid) return -1;
    snprintf(path, sizeof path, "%s\\%s\\spirv\\%s.spv", pdir, gid, key);

    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || (sz % 4) != 0) { fclose(f); return -1; }

    uint32_t *code = malloc((size_t)sz);
    if (!code) { fclose(f); return -1; }
    fread(code, 1, (size_t)sz, f);
    fclose(f);

    uint32_t bound = code[3];
    if (bound == 0 || bound > 65536) bound = 65536;
    char **id_names = calloc(bound, sizeof(char *));
    uint32_t *id_bindings = malloc(bound * sizeof(uint32_t));
    if (id_bindings) memset(id_bindings, 0xFF, bound * sizeof(uint32_t));
    uint32_t *id_locations = malloc(bound * sizeof(uint32_t));
    if (id_locations) memset(id_locations, 0xFF, bound * sizeof(uint32_t));

    size_t idx = 5;
    size_t words = (size_t)sz / 4;
    int has_color_output = 0;
    int se_num_inputs = 0;
    uint32_t se_input_locs[16] = {0};
    while (idx < words) {
        uint32_t w = code[idx];
        uint32_t wc = w >> 16;
        uint32_t op = w & 0xFFFF;
        if (wc == 0) break;
        if (op == 5 && wc >= 3) { // OpName
            uint32_t target = code[idx + 1];
            if (target < bound && id_names) {
                id_names[target] = (char *)&code[idx + 2];
            }
        } else if (op == 71 && wc >= 4) { // OpDecorate
            uint32_t target = code[idx + 1];
            uint32_t dec = code[idx + 2];
            if (dec == 30) {
                if (is_ps) {
                    has_color_output = 1;
                } else if (target < bound && id_locations) {
                    id_locations[target] = code[idx + 3];
                }
            } else if (dec == 33 && target < bound && id_bindings) { // Binding
                id_bindings[target] = code[idx + 3];
            } else if (dec == 34) { // DescriptorSet
                if (is_ps) {
                    code[idx + 3] = 1; // Remap fragment shader from Set 0 to Set 1
                }
            }
        } else if (op == 59 && wc >= 4 && !is_ps) { // OpVariable
            uint32_t rid = code[idx + 2];
            uint32_t sclass = code[idx + 3];
            // Only Storage Class 1 (Input) defines vertex input attributes
            if (sclass == 1 && rid < bound && id_locations && id_locations[rid] != 0xFFFFFFFF) {
                int found = 0;
                for (int k = 0; k < se_num_inputs; k++) {
                    if (se_input_locs[k] == id_locations[rid]) { found = 1; break; }
                }
                if (!found && se_num_inputs < 16) {
                    se_input_locs[se_num_inputs++] = id_locations[rid];
                }
            }
        }
        idx += wc;
    }

    VkShaderModuleCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = (size_t)sz,
        .pCode = code
    };
    VkShaderModule mod = VK_NULL_HANDLE;
    VkResult res = g_vk.CreateShaderModule(g_vk.device, &ci, NULL, &mod);

    if (res != VK_SUCCESS || mod == VK_NULL_HANDLE) {
        free(code);
        if (id_names) free(id_names);
        if (id_bindings) free(id_bindings);
        if (id_locations) free(id_locations);
        return -1;
    }

    int cur_idx = g_vk_shader_count++;
    vk_shader_entry *se = &g_vk_shaders[cur_idx];
    snprintf(se->key, sizeof se->key, "%s", key);
    se->is_ps = is_ps;
    se->module = mod;
    se->c1_offset = (uint32_t)cur_idx * 65536;
    se->has_c1 = 0;
    se->has_color_output = has_color_output;
    se->num_inputs = se_num_inputs;
    if (se_num_inputs > 0) {
        memcpy(se->input_locations, se_input_locs, sizeof(uint32_t) * se_num_inputs);
        fprintf(stderr, "[dm] [vk-win] Shader %s has %d input attributes:", key, se->num_inputs);
        for (int k = 0; k < se->num_inputs; k++) {
            fprintf(stderr, " loc=%u", se->input_locations[k]);
        }
        fprintf(stderr, "\n");
    }
    if (id_locations) free(id_locations);

    char c1_path[512];
    snprintf(c1_path, sizeof c1_path, "%s\\%s\\spirv\\%s.c1", pdir, gid, key);
    FILE *fc1 = fopen(c1_path, "rb");
    if (fc1) {
        if (g_vk.c1_mapped) {
            uint8_t *dst = (uint8_t *)g_vk.c1_mapped + se->c1_offset;
            size_t nread = fread(dst, 1, 65536, fc1);
            if (nread > 0) {
                se->has_c1 = 1;
                fprintf(stderr, "[dm] [vk-win] Loaded c1 constants for %s (%s): %zu bytes at offset %u\n",
                        key, is_ps ? "PS" : "VS", nread, se->c1_offset);
                if (g_vk.FlushMappedMemoryRanges) {
                    VkMappedMemoryRange range = {
                        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                        .memory = g_vk.c1_mem,
                        .offset = se->c1_offset,
                        .size = 65536
                    };
                    g_vk.FlushMappedMemoryRanges(g_vk.device, 1, &range);
                }
            }
        }
        fclose(fc1);
    }

    int bcnt = 0;
    if (id_bindings) {
        for (uint32_t id = 0; id < bound; id++) {
            if (id_bindings[id] != 0xFFFFFFFF && bcnt < VK_MAX_REFL_BINDINGS) {
                uint32_t bnum = id_bindings[id];
                const char *name = (id_names && id_names[id]) ? id_names[id] : "";
                vk_refl_binding_t *rb = &se->bindings[bcnt++];
                memset(rb, 0, sizeof(*rb));
                rb->binding = bnum;
                if (strncmp(name, "support_buffer", 14) == 0) {
                    rb->type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    rb->is_ubo = 1;
                    rb->is_support = 1;
                } else if (strcmp(name, "fp_c1") == 0 || strcmp(name, "vp_c1") == 0) {
                    rb->type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    rb->is_c1 = 1;
                } else if (strstr(name, "_c") != NULL) {
                    rb->type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    int st = (name[0] == 'v') ? 0 : 1;
                    const char *cp = strstr(name, "_c");
                    int bank = atoi(cp + 2);
                    if (bank == 0) {
                        rb->is_driver = 1;
                    } else {
                        rb->is_ubo = 1;
                        rb->ubo_stage = st;
                        rb->ubo_index = bank;
                    }
                } else if (strncmp(name, "fp_t_", 5) == 0 || strncmp(name, "vp_t_", 5) == 0) {
                    rb->type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    rb->is_sampler = 1;
                    const char *tcb = strstr(name, "_tcb_");
                    if (tcb) {
                        int raw_hex = (int)strtol(tcb + 5, NULL, 16);
                        rb->tex_slot = (raw_hex >= 8) ? (raw_hex - 8) / 2 : raw_hex;
                    } else {
                        const char *cb2 = strstr(name, "_cb2_");
                        if (cb2) {
                            int raw_hex = (int)strtol(cb2 + 5, NULL, 16);
                            rb->tex_slot = (raw_hex >= 8) ? (raw_hex - 8) / 2 : raw_hex;
                        }
                    }
                } else if (strncmp(name, "fp_i_", 5) == 0) {
                    rb->type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    rb->is_storage_img = 1;
                } else {
                    rb->type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    rb->is_ubo = 1;
                }
                fprintf(stderr, "[dm] [vk-win] SHADER %s: binding=%u type=%d is_ubo=%d is_samp=%d slot=%d name=%s\n",
                        key, bnum, rb->type, rb->is_ubo, rb->is_sampler, rb->tex_slot, name);
            }
        }
    }
    se->binding_count = bcnt;
    if (id_names) free(id_names);
    if (id_bindings) free(id_bindings);
    free(code);
    return cur_idx;
}

static VkFormat nvn_to_vk_vertex_format(uint32_t f) {
    switch (f) {
        case 0x01: return VK_FORMAT_R8_UNORM;
        case 0x02: return VK_FORMAT_R8_SNORM;
        case 0x03: return VK_FORMAT_R8_UINT;
        case 0x04: return VK_FORMAT_R8_SINT;
        case 0x05: return VK_FORMAT_R16_SFLOAT;
        case 0x06: return VK_FORMAT_R16_UNORM;
        case 0x07: return VK_FORMAT_R16_SNORM;
        case 0x08: return VK_FORMAT_R16_UINT;
        case 0x09: return VK_FORMAT_R16_SINT;
        case 0x0A: return VK_FORMAT_R32_SFLOAT;
        case 0x0B: return VK_FORMAT_R32_UINT;
        case 0x0C: return VK_FORMAT_R32_SINT;
        case 0x0D: return VK_FORMAT_R8G8_UNORM;
        case 0x0E: return VK_FORMAT_R8G8_SNORM;
        case 0x0F: return VK_FORMAT_R8G8_UINT;
        case 0x10: return VK_FORMAT_R8G8_SINT;
        case 0x11: return VK_FORMAT_R16G16_SFLOAT;
        case 0x12: return VK_FORMAT_R16G16_UNORM;
        case 0x13: return VK_FORMAT_R16G16_SNORM;
        case 0x14: return VK_FORMAT_R16G16_UINT;
        case 0x15: return VK_FORMAT_R16G16_SINT;
        case 0x16: return VK_FORMAT_R32G32_SFLOAT;
        case 0x17: return VK_FORMAT_R32G32_UINT;
        case 0x18: return VK_FORMAT_R32G32_SINT;
        case 0x19: return VK_FORMAT_R8G8B8_UNORM;
        case 0x1A: return VK_FORMAT_R8G8B8_SNORM;
        case 0x1B: return VK_FORMAT_R8G8B8_UINT;
        case 0x1C: return VK_FORMAT_R8G8B8_SINT;
        case 0x1D: return VK_FORMAT_R16G16B16_SFLOAT;
        case 0x1E: return VK_FORMAT_R16G16B16_UNORM;
        case 0x1F: return VK_FORMAT_R16G16B16_SNORM;
        case 0x20: return VK_FORMAT_R16G16B16_UINT;
        case 0x21: return VK_FORMAT_R16G16B16_SINT;
        case 0x22: return VK_FORMAT_R32G32B32_SFLOAT;
        case 0x23: return VK_FORMAT_R32G32B32_UINT;
        case 0x24: return VK_FORMAT_R32G32B32_SINT;
        case 0x25: return VK_FORMAT_R8G8B8A8_UNORM;
        case 0x26: return VK_FORMAT_R8G8B8A8_SNORM;
        case 0x27: return VK_FORMAT_R8G8B8A8_UINT;
        case 0x28: return VK_FORMAT_R8G8B8A8_SINT;
        case 0x29: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case 0x2A: return VK_FORMAT_R16G16B16A16_UNORM;
        case 0x2B: return VK_FORMAT_R16G16B16A16_SNORM;
        case 0x2C: return VK_FORMAT_R16G16B16A16_UINT;
        case 0x2D: return VK_FORMAT_R16G16B16A16_SINT;
        case 0x2E: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case 0x2F: return VK_FORMAT_R32G32B32A32_UINT;
        case 0x30: return VK_FORMAT_R32G32B32A32_SINT;
        case 0x3D: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case 0x3F: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case 0x9A: return VK_FORMAT_B8G8R8A8_UNORM;
        default: return VK_FORMAT_UNDEFINED;
    }
}

static inline uint32_t vk_format_byte_size(VkFormat fmt) {
    switch (fmt) {
        case VK_FORMAT_R32G32B32A32_SFLOAT:
        case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R32G32B32A32_SINT:
            return 16;
        case VK_FORMAT_R32G32B32_SFLOAT:
        case VK_FORMAT_R32G32B32_UINT:
        case VK_FORMAT_R32G32B32_SINT:
            return 12;
        case VK_FORMAT_R32G32_SFLOAT:
        case VK_FORMAT_R32G32_UINT:
        case VK_FORMAT_R32G32_SINT:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R16G16B16A16_UNORM:
        case VK_FORMAT_R16G16B16A16_SNORM:
        case VK_FORMAT_R16G16B16A16_UINT:
        case VK_FORMAT_R16G16B16A16_SINT:
            return 8;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SNORM:
        case VK_FORMAT_R8G8B8A8_UINT:
        case VK_FORMAT_R8G8B8A8_SINT:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_R16G16_SFLOAT:
        case VK_FORMAT_R16G16_UNORM:
        case VK_FORMAT_R16G16_SNORM:
        case VK_FORMAT_R16G16_UINT:
        case VK_FORMAT_R16G16_SINT:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32_SINT:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
            return 4;
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SNORM:
        case VK_FORMAT_R8G8_UINT:
        case VK_FORMAT_R8G8_SINT:
        case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_R16_UNORM:
        case VK_FORMAT_R16_SNORM:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R16_SINT:
            return 2;
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SNORM:
        case VK_FORMAT_R8_UINT:
        case VK_FORMAT_R8_SINT:
            return 1;
        default:
            return 4;
    }
}

static inline uint64_t vk_calc_vertex_signature(int vs_idx) {
    if (vs_idx < 0 || vs_idx >= VK_SHADER_CACHE_MAX) return 0;
    vk_shader_entry *vs_se = &g_vk_shaders[vs_idx];
    if (vs_se->num_inputs <= 0) return 0;
    uint64_t sig = 0;
    for (int k = 0; k < vs_se->num_inputs; k++) {
        uint32_t loc = vs_se->input_locations[k];
        uint32_t stream = (loc < 16) ? g_vk.vattrs[loc].stream : 0;
        if (stream >= 16) stream = 0;
        uint32_t fmt = (loc < 16) ? g_vk.vattrs[loc].fmt : 0;
        uint32_t off = (loc < 16) ? g_vk.vattrs[loc].offset : 0;
        uint32_t str = (stream < 16) ? g_vk.vstreams[stream].stride : 0;
        uint32_t div = (stream < 16) ? g_vk.vstreams[stream].divisor : 0;
        uint64_t elem = (uint64_t)fmt | ((uint64_t)str << 8) | ((uint64_t)(off & 0xFF) << 16) | ((uint64_t)(div & 1) << 24) | ((uint64_t)stream << 25);
        sig ^= (elem << ((k % 4) * 16));
        sig = (sig << 7) | (sig >> (64 - 7));
    }
    return sig ? sig : 1;
}

static vk_pipeline_entry *vk_get_or_create_pipeline_entry(const char *vs_key, const char *ps_key) {
    if (!g_vk.active || !vs_key || !ps_key) return NULL;

    int vs_idx = vk_get_or_create_shader_idx(vs_key, 0);
    int ps_idx = vk_get_or_create_shader_idx(ps_key, 1);
    if (vs_idx < 0 || ps_idx < 0) return NULL;

    uint64_t v_sig = vk_calc_vertex_signature(vs_idx);

    for (int i = 0; i < g_vk_pipeline_count; i++) {
        if (strcmp(g_vk_pipelines[i].vs_key, vs_key) == 0 &&
            strcmp(g_vk_pipelines[i].ps_key, ps_key) == 0 &&
            g_vk_pipelines[i].v_sig == v_sig) {
            return &g_vk_pipelines[i];
        }
    }
    if (g_vk_pipeline_count >= VK_PIPELINE_CACHE_MAX) return NULL;

    vk_shader_entry *vs_se = &g_vk_shaders[vs_idx];
    vk_shader_entry *ps_se = &g_vk_shaders[ps_idx];

    vk_pipeline_entry *entry = &g_vk_pipelines[g_vk_pipeline_count];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->vs_key, sizeof entry->vs_key, "%s", vs_key);
    snprintf(entry->ps_key, sizeof entry->ps_key, "%s", ps_key);
    entry->v_sig = v_sig;
    entry->vs_sh_idx = vs_idx;
    entry->ps_sh_idx = ps_idx;

    // Set 0 (VS Layout)
    VkDescriptorSetLayoutBinding vs_bindings[VK_MAX_REFL_BINDINGS];
    for (int i = 0; i < vs_se->binding_count; i++) {
        vs_bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding = vs_se->bindings[i].binding,
            .descriptorType = vs_se->bindings[i].type,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
        };
    }
    VkDescriptorSetLayoutCreateInfo dsl_vs_ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = (uint32_t)vs_se->binding_count,
        .pBindings = vs_bindings
    };
    if (g_vk.CreateDescriptorSetLayout(g_vk.device, &dsl_vs_ci, NULL, &entry->desc_layout_vs) != VK_SUCCESS) {
        return NULL;
    }

    // Set 1 (PS Layout)
    VkDescriptorSetLayoutBinding ps_bindings[VK_MAX_REFL_BINDINGS];
    for (int i = 0; i < ps_se->binding_count; i++) {
        ps_bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding = ps_se->bindings[i].binding,
            .descriptorType = ps_se->bindings[i].type,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        };
    }
    VkDescriptorSetLayoutCreateInfo dsl_ps_ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = (uint32_t)ps_se->binding_count,
        .pBindings = ps_bindings
    };
    if (g_vk.CreateDescriptorSetLayout(g_vk.device, &dsl_ps_ci, NULL, &entry->desc_layout_ps) != VK_SUCCESS) {
        return NULL;
    }

    // Pipeline Layout (2 Sets: Set 0 = VS, Set 1 = PS)
    VkDescriptorSetLayout layouts[2] = { entry->desc_layout_vs, entry->desc_layout_ps };
    VkPipelineLayoutCreateInfo pl_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts = layouts
    };
    if (g_vk.CreatePipelineLayout(g_vk.device, &pl_ci, NULL, &entry->layout) != VK_SUCCESS) {
        return NULL;
    }

    // Allocate Descriptor Sets from Pool for each frame in flight
    for (uint32_t f = 0; f < g_vk.image_count && f < MAX_SWAPCHAIN_IMAGES; f++) {
        VkDescriptorSetAllocateInfo ds_ai = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = g_vk.desc_pool,
            .descriptorSetCount = 2,
            .pSetLayouts = layouts
        };
        if (g_vk.AllocateDescriptorSets(g_vk.device, &ds_ai, entry->desc_sets[f]) != VK_SUCCESS) {
            return NULL;
        }
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vs_se->module,
            .pName = "main"
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = ps_se->module,
            .pName = "main"
        }
    };

    VkVertexInputBindingDescription bind_descs[16];
    VkVertexInputAttributeDescription attr_descs[16];
    uint32_t num_binds = 0;
    uint32_t num_attrs = 0;

    if (vs_se->num_inputs > 0) {
        int stream_used[16] = {0};
        uint32_t stream_min_stride[16] = {0};
        for (int k = 0; k < vs_se->num_inputs && num_attrs < 16; k++) {
            uint32_t loc = vs_se->input_locations[k];
            uint32_t stream = (loc < 16) ? g_vk.vattrs[loc].stream : 0;
            if (stream >= 16) stream = 0;
            VkFormat fmt = (loc < 16 && g_vk.vattrs[loc].fmt) ? nvn_to_vk_vertex_format(g_vk.vattrs[loc].fmt) : VK_FORMAT_UNDEFINED;
            if (fmt == VK_FORMAT_UNDEFINED) {
                uint32_t st_stride = (stream < 16) ? g_vk.vstreams[stream].stride : 0;
                if (st_stride == 8) fmt = VK_FORMAT_R32G32_SFLOAT;
                else if (st_stride == 12) fmt = VK_FORMAT_R32G32B32_SFLOAT;
                else fmt = VK_FORMAT_R32G32B32A32_SFLOAT;
            }
            uint32_t off = (loc < 16) ? g_vk.vattrs[loc].offset : 0;

            attr_descs[num_attrs++] = (VkVertexInputAttributeDescription){
                .location = loc,
                .binding = stream,
                .format = fmt,
                .offset = off
            };
            stream_used[stream] = 1;
            uint32_t req = off + vk_format_byte_size(fmt);
            if (req > stream_min_stride[stream]) stream_min_stride[stream] = req;
            fprintf(stderr, "[dm] [vk-win] Pipeline %s attr %d: loc=%u stream=%u fmt=%#x off=%u stride=%u\n",
                    vs_key, k, loc, stream, (uint32_t)fmt, off, g_vk.vstreams[stream].stride);
        }
        for (uint32_t s = 0; s < 16; s++) {
            if (stream_used[s]) {
                uint32_t str = (g_vk.vstreams[s].stride > 0) ? g_vk.vstreams[s].stride : stream_min_stride[s];
                if (str < stream_min_stride[s]) str = stream_min_stride[s];
                bind_descs[num_binds++] = (VkVertexInputBindingDescription){
                    .binding = s,
                    .stride = str,
                    .inputRate = g_vk.vstreams[s].divisor ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX
                };
            }
        }
        fprintf(stderr, "[dm] [vk-win] Pipeline %s: num_binds=%u num_attrs=%u\n", vs_key, num_binds, num_attrs);
    }

    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = num_binds,
        .pVertexBindingDescriptions = num_binds > 0 ? bind_descs : NULL,
        .vertexAttributeDescriptionCount = num_attrs,
        .pVertexAttributeDescriptions = num_attrs > 0 ? attr_descs : NULL
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .lineWidth = 1.0f
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };

    // Color blend state: only write to color attachment if the fragment shader has a color output
    VkPipelineColorBlendAttachmentState cba = {
        .blendEnable = ps_se->has_color_output ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = ps_se->has_color_output ? 0xf : 0
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &cba
    };
    VkDynamicState dyn_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn_states
    };

    VkGraphicsPipelineCreateInfo gci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pColorBlendState = &cb,
        .pDynamicState = &ds,
        .layout = entry->layout,
        .renderPass = g_vk.render_pass,
        .subpass = 0
    };

    VkPipeline pipe = VK_NULL_HANDLE;
    VkResult res = g_vk.CreateGraphicsPipelines(g_vk.device, VK_NULL_HANDLE, 1, &gci, NULL, &pipe);
    if (res == VK_SUCCESS && pipe != VK_NULL_HANDLE) {
        entry->pipeline = pipe;
        g_vk_pipeline_count++;
        fprintf(stderr, "[dm] [vk-win] GPU Graphics Pipeline compiled vs=%s ps=%s (pipeline=%p)\n",
                vs_key, ps_key, (void*)pipe);
        return entry;
    }
    return NULL;
}

int vk_win_init(HWND hwnd, int width, int height) {
    memset(&g_vk, 0, sizeof(g_vk));
    g_vk.hwnd = hwnd;
    g_vk.width = width > 0 ? (uint32_t)width : 1280;
    g_vk.height = height > 0 ? (uint32_t)height : 720;

    g_vk.h_dll = LoadLibraryA("vulkan-1.dll");
    if (!g_vk.h_dll) {
        fprintf(stderr, "[dm] [RED] [vk-win] LoadLibrary vulkan-1.dll failed (err=%lu)\n", GetLastError());
        return 0;
    }

    g_vk.GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(g_vk.h_dll, "vkGetInstanceProcAddr");
    if (!g_vk.GetInstanceProcAddr) {
        fprintf(stderr, "[dm] [RED] [vk-win] GetProcAddress vkGetInstanceProcAddr failed\n");
        vk_win_shutdown();
        return 0;
    }

    g_vk.CreateInstance = (PFN_vkCreateInstance)g_vk.GetInstanceProcAddr(NULL, "vkCreateInstance");

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "dmhost",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "nxnative",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_2
    };

    const char *inst_exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };

    VkInstanceCreateInfo inst_ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = inst_exts
    };

    VkResult vres = g_vk.CreateInstance(&inst_ci, NULL, &g_vk.instance);
    if (vres != VK_SUCCESS) {
        fprintf(stderr, "[dm] [RED] [vk-win] vkCreateInstance failed (res=%d)\n", (int)vres);
        vk_win_shutdown();
        return 0;
    }

#define LOAD_INST(fn) g_vk.fn = (PFN_vk##fn)g_vk.GetInstanceProcAddr(g_vk.instance, "vk" #fn); if (!g_vk.fn) { vk_win_shutdown(); return 0; }
    LOAD_INST(DestroyInstance);
    LOAD_INST(EnumeratePhysicalDevices);
    LOAD_INST(GetPhysicalDeviceProperties);
    LOAD_INST(GetPhysicalDeviceQueueFamilyProperties);
    LOAD_INST(GetPhysicalDeviceMemoryProperties);
    LOAD_INST(CreateDevice);
    LOAD_INST(DestroyDevice);
    LOAD_INST(GetDeviceQueue);
    LOAD_INST(CreateWin32SurfaceKHR);
    LOAD_INST(DestroySurfaceKHR);
    LOAD_INST(GetPhysicalDeviceSurfaceSupportKHR);
    LOAD_INST(GetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD_INST(GetPhysicalDeviceSurfaceFormatsKHR);
    LOAD_INST(GetPhysicalDeviceSurfacePresentModesKHR);
#undef LOAD_INST

    // Create Win32 Surface
    VkWin32SurfaceCreateInfoKHR surf_ci = {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = GetModuleHandle(NULL),
        .hwnd = hwnd
    };
    if (g_vk.CreateWin32SurfaceKHR(g_vk.instance, &surf_ci, NULL, &g_vk.surface) != VK_SUCCESS) {
        vk_win_shutdown();
        return 0;
    }

    // Pick physical device (prefer Qualcomm Adreno)
    uint32_t dev_count = 0;
    g_vk.EnumeratePhysicalDevices(g_vk.instance, &dev_count, NULL);
    if (!dev_count) { vk_win_shutdown(); return 0; }

    VkPhysicalDevice *devs = malloc(sizeof(VkPhysicalDevice) * dev_count);
    g_vk.EnumeratePhysicalDevices(g_vk.instance, &dev_count, devs);

    g_vk.phys_dev = devs[0];
    for (uint32_t i = 0; i < dev_count; i++) {
        VkPhysicalDeviceProperties props;
        g_vk.GetPhysicalDeviceProperties(devs[i], &props);
        if (props.vendorID == 0x5143) { // Qualcomm
            g_vk.phys_dev = devs[i];
            break;
        }
    }
    free(devs);

    // Find queue family
    uint32_t qf_count = 0;
    g_vk.GetPhysicalDeviceQueueFamilyProperties(g_vk.phys_dev, &qf_count, NULL);
    VkQueueFamilyProperties *qf_props = malloc(sizeof(VkQueueFamilyProperties) * qf_count);
    g_vk.GetPhysicalDeviceQueueFamilyProperties(g_vk.phys_dev, &qf_count, qf_props);

    g_vk.queue_family = 0;
    for (uint32_t i = 0; i < qf_count; i++) {
        VkBool32 present_support = VK_FALSE;
        g_vk.GetPhysicalDeviceSurfaceSupportKHR(g_vk.phys_dev, i, g_vk.surface, &present_support);
        if ((qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support) {
            g_vk.queue_family = i;
            break;
        }
    }
    free(qf_props);

    // Create Logical Device
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo q_ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = g_vk.queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
    };

    const char *dev_exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkDeviceCreateInfo dev_ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &q_ci,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = dev_exts
    };

    if (g_vk.CreateDevice(g_vk.phys_dev, &dev_ci, NULL, &g_vk.device) != VK_SUCCESS) {
        vk_win_shutdown();
        return 0;
    }

    g_vk.GetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)g_vk.GetInstanceProcAddr(g_vk.instance, "vkGetDeviceProcAddr");
    g_vk.GetDeviceQueue(g_vk.device, g_vk.queue_family, 0, &g_vk.queue);

#define LOAD_DEV(fn) g_vk.fn = (PFN_vk##fn)g_vk.GetDeviceProcAddr(g_vk.device, "vk" #fn); if (!g_vk.fn) { vk_win_shutdown(); return 0; }
    LOAD_DEV(DeviceWaitIdle);
    LOAD_DEV(CreateSwapchainKHR);
    LOAD_DEV(DestroySwapchainKHR);
    LOAD_DEV(GetSwapchainImagesKHR);
    LOAD_DEV(AcquireNextImageKHR);
    LOAD_DEV(QueuePresentKHR);

    LOAD_DEV(CreateCommandPool);
    LOAD_DEV(DestroyCommandPool);
    LOAD_DEV(AllocateCommandBuffers);
    LOAD_DEV(BeginCommandBuffer);
    LOAD_DEV(EndCommandBuffer);
    LOAD_DEV(CmdPipelineBarrier);
    LOAD_DEV(CmdCopyBufferToImage);
    LOAD_DEV(CmdCopyImageToBuffer);
    LOAD_DEV(QueueSubmit);

    LOAD_DEV(CreateSemaphore);
    LOAD_DEV(DestroySemaphore);
    LOAD_DEV(CreateFence);
    LOAD_DEV(DestroyFence);
    LOAD_DEV(WaitForFences);
    LOAD_DEV(ResetFences);
    LOAD_DEV(GetFenceStatus);
    LOAD_DEV(ResetCommandBuffer);

    LOAD_DEV(CreateBuffer);
    LOAD_DEV(DestroyBuffer);
    LOAD_DEV(GetBufferMemoryRequirements);
    LOAD_DEV(AllocateMemory);
    LOAD_DEV(FreeMemory);
    LOAD_DEV(BindBufferMemory);
    LOAD_DEV(MapMemory);
    LOAD_DEV(UnmapMemory);
    LOAD_DEV(FlushMappedMemoryRanges);
    LOAD_DEV(InvalidateMappedMemoryRanges);

    LOAD_DEV(CreateShaderModule);
    LOAD_DEV(DestroyShaderModule);

    LOAD_DEV(CreateRenderPass);
    LOAD_DEV(DestroyRenderPass);
    LOAD_DEV(CreateImageView);
    LOAD_DEV(DestroyImageView);
    LOAD_DEV(CreateFramebuffer);
    LOAD_DEV(DestroyFramebuffer);

    LOAD_DEV(CreateDescriptorSetLayout);
    LOAD_DEV(DestroyDescriptorSetLayout);
    LOAD_DEV(CreateDescriptorPool);
    LOAD_DEV(DestroyDescriptorPool);
    LOAD_DEV(AllocateDescriptorSets);
    LOAD_DEV(UpdateDescriptorSets);

    LOAD_DEV(CreatePipelineLayout);
    LOAD_DEV(DestroyPipelineLayout);
    LOAD_DEV(CreateGraphicsPipelines);
    LOAD_DEV(DestroyPipeline);

    LOAD_DEV(CreateImage);
    LOAD_DEV(DestroyImage);
    LOAD_DEV(GetImageMemoryRequirements);
    LOAD_DEV(BindImageMemory);
    LOAD_DEV(CreateSampler);
    LOAD_DEV(DestroySampler);

    LOAD_DEV(CmdBeginRenderPass);
    LOAD_DEV(CmdEndRenderPass);
    LOAD_DEV(CmdBindPipeline);
    LOAD_DEV(CmdBindDescriptorSets);
    LOAD_DEV(CmdSetViewport);
    LOAD_DEV(CmdSetScissor);
    LOAD_DEV(CmdDraw);
    LOAD_DEV(CmdBindIndexBuffer);
    LOAD_DEV(CmdDrawIndexed);
    LOAD_DEV(CmdBindVertexBuffers);
#undef LOAD_DEV

    // Query surface capabilities
    VkSurfaceCapabilitiesKHR caps;
    g_vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(g_vk.phys_dev, g_vk.surface, &caps);

    uint32_t req_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && req_count > caps.maxImageCount) req_count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR swp_ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = g_vk.surface,
        .minImageCount = req_count,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = { g_vk.width, g_vk.height },
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE
    };

    VkResult swp_res = g_vk.CreateSwapchainKHR(g_vk.device, &swp_ci, NULL, &g_vk.swapchain);
    if (swp_res != VK_SUCCESS) {
        fprintf(stderr, "[dm] [vk-win] CreateSwapchainKHR with TRANSFER_SRC failed (res=%d), using fallback\n", (int)swp_res);
        swp_ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (g_vk.CreateSwapchainKHR(g_vk.device, &swp_ci, NULL, &g_vk.swapchain) != VK_SUCCESS) {
            vk_win_shutdown();
            return 0;
        }
    } else {
        fprintf(stderr, "[dm] [vk-win] CreateSwapchainKHR with TRANSFER_SRC SUCCEEDED!\n");
    }

    g_vk.image_count = 0;
    g_vk.GetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &g_vk.image_count, NULL);
    if (g_vk.image_count > MAX_SWAPCHAIN_IMAGES) g_vk.image_count = MAX_SWAPCHAIN_IMAGES;
    g_vk.GetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &g_vk.image_count, g_vk.images);

    // Command pool & buffers
    VkCommandPoolCreateInfo cp_ci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = g_vk.queue_family
    };
    if (g_vk.CreateCommandPool(g_vk.device, &cp_ci, NULL, &g_vk.cmd_pool) != VK_SUCCESS) {
        vk_win_shutdown();
        return 0;
    }

    VkCommandBufferAllocateInfo cb_ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_vk.cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = g_vk.image_count
    };
    g_vk.AllocateCommandBuffers(g_vk.device, &cb_ai, g_vk.cmd_bufs);

    VkCommandBufferAllocateInfo up_ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_vk.cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    g_vk.AllocateCommandBuffers(g_vk.device, &up_ai, &g_vk.upload_cmd_buf);

    // Sync objects
    VkSemaphoreCreateInfo sem_ci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_ci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };

    for (uint32_t i = 0; i < g_vk.image_count; i++) {
        g_vk.CreateSemaphore(g_vk.device, &sem_ci, NULL, &g_vk.sem_avail[i]);
        g_vk.CreateSemaphore(g_vk.device, &sem_ci, NULL, &g_vk.sem_finish[i]);
        g_vk.CreateFence(g_vk.device, &fence_ci, NULL, &g_vk.fences[i]);
        fprintf(stderr, "[dm] [vk-win] SYNC i=%u cb=%p sem_avail=%p sem_fin=%p fence=%p\n",
                i, (void*)g_vk.cmd_bufs[i], (void*)g_vk.sem_avail[i], (void*)g_vk.sem_finish[i], (void*)g_vk.fences[i]);
    }

    // Staging buffer
    VkPhysicalDeviceMemoryProperties mem_props;
    g_vk.GetPhysicalDeviceMemoryProperties(g_vk.phys_dev, &mem_props);

    g_vk.staging_size = 64 * 1024 * 1024; // 64 MiB staging buffer for 4K / 2K textures
    VkBufferCreateInfo buf_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = g_vk.staging_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (g_vk.CreateBuffer(g_vk.device, &buf_ci, NULL, &g_vk.staging_buf) == VK_SUCCESS) {
        VkMemoryRequirements mem_req;
        g_vk.GetBufferMemoryRequirements(g_vk.device, g_vk.staging_buf, &mem_req);
        VkMemoryAllocateInfo mem_ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mem_req.size,
            .memoryTypeIndex = find_mem_type(&mem_props, mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        };
        if (g_vk.AllocateMemory(g_vk.device, &mem_ai, NULL, &g_vk.staging_mem) == VK_SUCCESS) {
            g_vk.BindBufferMemory(g_vk.device, g_vk.staging_buf, g_vk.staging_mem, 0);
            g_vk.MapMemory(g_vk.device, g_vk.staging_mem, 0, g_vk.staging_size, 0, &g_vk.staging_mapped);
        }
    }

    // Dedicated Readback Buffer
    g_vk.readback_size = (size_t)g_vk.width * g_vk.height * 4;
    if (g_vk.readback_size < 4 * 1024 * 1024) g_vk.readback_size = 4 * 1024 * 1024;
    VkBufferCreateInfo rdbuf_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = g_vk.readback_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (g_vk.CreateBuffer(g_vk.device, &rdbuf_ci, NULL, &g_vk.readback_buf) == VK_SUCCESS) {
        VkMemoryRequirements rdmem_req;
        g_vk.GetBufferMemoryRequirements(g_vk.device, g_vk.readback_buf, &rdmem_req);
        VkMemoryAllocateInfo rdmem_ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = rdmem_req.size,
            .memoryTypeIndex = find_mem_type(&mem_props, rdmem_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        };
        if (g_vk.AllocateMemory(g_vk.device, &rdmem_ai, NULL, &g_vk.readback_mem) == VK_SUCCESS) {
            g_vk.BindBufferMemory(g_vk.device, g_vk.readback_buf, g_vk.readback_mem, 0);
            g_vk.MapMemory(g_vk.device, g_vk.readback_mem, 0, g_vk.readback_size, 0, &g_vk.readback_mapped);
        }
    }

    // RenderPass
    VkAttachmentDescription attach = {
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref
    };
    VkRenderPassCreateInfo rp_ci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attach,
        .subpassCount = 1,
        .pSubpasses = &subpass
    };
    if (g_vk.CreateRenderPass(g_vk.device, &rp_ci, NULL, &g_vk.render_pass) != VK_SUCCESS) {
        vk_win_shutdown();
        return 0;
    }

    // Resume RenderPass (loadOp = LOAD to preserve existing contents when resuming after texture upload)
    VkAttachmentDescription resume_attach = {
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    VkRenderPassCreateInfo resume_rp_ci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &resume_attach,
        .subpassCount = 1,
        .pSubpasses = &subpass
    };
    g_vk.CreateRenderPass(g_vk.device, &resume_rp_ci, NULL, &g_vk.render_pass_resume);

    // Image Views and Framebuffers for swapchain
    for (uint32_t i = 0; i < g_vk.image_count; i++) {
        VkImageViewCreateInfo iv_ci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = g_vk.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };
        g_vk.CreateImageView(g_vk.device, &iv_ci, NULL, &g_vk.image_views[i]);

        VkFramebufferCreateInfo fb_ci = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = g_vk.render_pass,
            .attachmentCount = 1,
            .pAttachments = &g_vk.image_views[i],
            .width = g_vk.width,
            .height = g_vk.height,
            .layers = 1
        };
        g_vk.CreateFramebuffer(g_vk.device, &fb_ci, NULL, &g_vk.framebuffers[i]);
    }

    // Support Buffer (64 KB) with render_scale = 1.0f
    VkBufferCreateInfo sbuf_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 65536,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
    };
    if (g_vk.CreateBuffer(g_vk.device, &sbuf_ci, NULL, &g_vk.support_buf) == VK_SUCCESS) {
        VkMemoryRequirements sbuf_req;
        g_vk.GetBufferMemoryRequirements(g_vk.device, g_vk.support_buf, &sbuf_req);
        VkMemoryAllocateInfo sbuf_ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = sbuf_req.size,
            .memoryTypeIndex = find_mem_type(&mem_props, sbuf_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        };
        if (g_vk.AllocateMemory(g_vk.device, &sbuf_ai, NULL, &g_vk.support_mem) == VK_SUCCESS) {
            g_vk.BindBufferMemory(g_vk.device, g_vk.support_buf, g_vk.support_mem, 0);
            g_vk.MapMemory(g_vk.device, g_vk.support_mem, 0, 65536, 0, &g_vk.support_mapped);
            if (g_vk.support_mapped) {
                memset(g_vk.support_mapped, 0, 65536);
                // Offset 144: viewport_inverse (1/w, 1/h, 1, 1)
                float *vp_inv = (float *)((uint8_t *)g_vk.support_mapped + 144);
                vp_inv[0] = 1.0f / (float)g_vk.width;
                vp_inv[1] = 1.0f / (float)g_vk.height;
                vp_inv[2] = 1.0f;
                vp_inv[3] = 1.0f;

                // Offset 160: viewport_size (w, h, 1, 1)
                float *vp_sz = (float *)((uint8_t *)g_vk.support_mapped + 160);
                vp_sz[0] = (float)g_vk.width;
                vp_sz[1] = (float)g_vk.height;
                vp_sz[2] = 1.0f;
                vp_sz[3] = 1.0f;

                float *scales = (float *)((uint8_t *)g_vk.support_mapped + 192);
                for (int i = 0; i < 73; i++) scales[i * 4] = 1.0f;
                if (g_vk.FlushMappedMemoryRanges) {
                    VkMappedMemoryRange range = {
                        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                        .memory = g_vk.support_mem,
                        .offset = 0,
                        .size = VK_WHOLE_SIZE
                    };
                    g_vk.FlushMappedMemoryRanges(g_vk.device, 1, &range);
                }
            }
        }
    }

    // Driver Constant Buffer c0 (64 KB)
    VkBufferCreateInfo drv_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 65536,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
    };
    if (g_vk.CreateBuffer(g_vk.device, &drv_ci, NULL, &g_vk.driver_buf) == VK_SUCCESS) {
        VkMemoryRequirements drv_req;
        g_vk.GetBufferMemoryRequirements(g_vk.device, g_vk.driver_buf, &drv_req);
        VkMemoryAllocateInfo drv_ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = drv_req.size,
            .memoryTypeIndex = find_mem_type(&mem_props, drv_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        };
        if (g_vk.AllocateMemory(g_vk.device, &drv_ai, NULL, &g_vk.driver_mem) == VK_SUCCESS) {
            g_vk.BindBufferMemory(g_vk.device, g_vk.driver_buf, g_vk.driver_mem, 0);
            g_vk.MapMemory(g_vk.device, g_vk.driver_mem, 0, 65536, 0, &g_vk.driver_mapped);
            if (g_vk.driver_mapped) {
                memset(g_vk.driver_mapped, 0, 65536);
                *(float *)((uint8_t *)g_vk.driver_mapped + 0x610) = 2047.0f; // max point size
                *(uint32_t *)((uint8_t *)g_vk.driver_mapped + 0x640) = 0;   // base vertex
                *(uint32_t *)((uint8_t *)g_vk.driver_mapped + 0x644) = 0;   // base instance
            }
        }
    }

    // Zero / C1 Constant Buffer (16 MiB: 256 shaders x 64 KB dedicated slices)
    size_t c1_total_sz = (size_t)VK_SHADER_CACHE_MAX * 65536;
    VkBufferCreateInfo c1_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = c1_total_sz,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
    };
    if (g_vk.CreateBuffer(g_vk.device, &c1_ci, NULL, &g_vk.c1_buf) == VK_SUCCESS) {
        VkMemoryRequirements c1_req;
        g_vk.GetBufferMemoryRequirements(g_vk.device, g_vk.c1_buf, &c1_req);
        VkMemoryAllocateInfo c1_ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = c1_req.size,
            .memoryTypeIndex = find_mem_type(&mem_props, c1_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        };
        if (g_vk.AllocateMemory(g_vk.device, &c1_ai, NULL, &g_vk.c1_mem) == VK_SUCCESS) {
            g_vk.BindBufferMemory(g_vk.device, g_vk.c1_buf, g_vk.c1_mem, 0);
            g_vk.MapMemory(g_vk.device, g_vk.c1_mem, 0, c1_total_sz, 0, &g_vk.c1_mapped);
            if (g_vk.c1_mapped) {
                memset(g_vk.c1_mapped, 0, c1_total_sz);
            }
        }
    }

    // UBO Data Buffer (8 MiB host-visible coherent: 96 slots x 64 KB)
    VkBufferCreateInfo ubuf_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 8388608,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
    };
    if (g_vk.CreateBuffer(g_vk.device, &ubuf_ci, NULL, &g_vk.ubo_data_buf) == VK_SUCCESS) {
        VkMemoryRequirements ubuf_req;
        g_vk.GetBufferMemoryRequirements(g_vk.device, g_vk.ubo_data_buf, &ubuf_req);
        VkMemoryAllocateInfo ubuf_ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = ubuf_req.size,
            .memoryTypeIndex = find_mem_type(&mem_props, ubuf_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        };
        if (g_vk.AllocateMemory(g_vk.device, &ubuf_ai, NULL, &g_vk.ubo_data_mem) == VK_SUCCESS) {
            g_vk.BindBufferMemory(g_vk.device, g_vk.ubo_data_buf, g_vk.ubo_data_mem, 0);
            g_vk.MapMemory(g_vk.device, g_vk.ubo_data_mem, 0, 8388608, 0, &g_vk.ubo_data_mapped);
            if (g_vk.ubo_data_mapped) {
                memset(g_vk.ubo_data_mapped, 0, 8388608);
            }
        }
    }

    // Dynamic Index Buffer (4 MiB host-visible coherent)
    VkBufferCreateInfo ibuf_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 4194304,
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT
    };
    if (g_vk.CreateBuffer(g_vk.device, &ibuf_ci, NULL, &g_vk.index_buf) == VK_SUCCESS) {
        VkMemoryRequirements ibuf_req;
        g_vk.GetBufferMemoryRequirements(g_vk.device, g_vk.index_buf, &ibuf_req);
        VkMemoryAllocateInfo ibuf_ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = ibuf_req.size,
            .memoryTypeIndex = find_mem_type(&mem_props, ibuf_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        };
        if (g_vk.AllocateMemory(g_vk.device, &ibuf_ai, NULL, &g_vk.index_mem) == VK_SUCCESS) {
            g_vk.BindBufferMemory(g_vk.device, g_vk.index_buf, g_vk.index_mem, 0);
            g_vk.MapMemory(g_vk.device, g_vk.index_mem, 0, 4194304, 0, &g_vk.index_mapped);
            if (g_vk.index_mapped) {
                memset(g_vk.index_mapped, 0, 4194304);
            }
        }
    }
    g_vk.index_buf_offset = 0;

    // Dynamic Vertex Buffer (16 MiB host-visible coherent)
    VkBufferCreateInfo vbuf_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 16777216,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    };
    if (g_vk.CreateBuffer(g_vk.device, &vbuf_ci, NULL, &g_vk.vbuf) == VK_SUCCESS) {
        VkMemoryRequirements vbuf_req;
        g_vk.GetBufferMemoryRequirements(g_vk.device, g_vk.vbuf, &vbuf_req);
        VkMemoryAllocateInfo vbuf_ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = vbuf_req.size,
            .memoryTypeIndex = find_mem_type(&mem_props, vbuf_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        };
        if (g_vk.AllocateMemory(g_vk.device, &vbuf_ai, NULL, &g_vk.vbuf_mem) == VK_SUCCESS) {
            g_vk.BindBufferMemory(g_vk.device, g_vk.vbuf, g_vk.vbuf_mem, 0);
            g_vk.MapMemory(g_vk.device, g_vk.vbuf_mem, 0, 16777216, 0, &g_vk.vbuf_mapped);
            if (g_vk.vbuf_mapped) {
                memset(g_vk.vbuf_mapped, 0, 16777216);
            }
        }
    }
    g_vk.vbuf_offset = 64;

    // Dummy 1x1 White Texture (R8G8B8A8_UNORM, 0xFFFFFFFF)
    VkImageCreateInfo dimg_ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { 1, 1, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    };
    if (g_vk.CreateImage(g_vk.device, &dimg_ci, NULL, &g_vk.dummy_img) == VK_SUCCESS) {
        VkMemoryRequirements dimg_req;
        g_vk.GetImageMemoryRequirements(g_vk.device, g_vk.dummy_img, &dimg_req);
        VkMemoryAllocateInfo dimg_ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = dimg_req.size,
            .memoryTypeIndex = find_mem_type(&mem_props, dimg_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        };
        if (g_vk.AllocateMemory(g_vk.device, &dimg_ai, NULL, &g_vk.dummy_mem) == VK_SUCCESS) {
            g_vk.BindImageMemory(g_vk.device, g_vk.dummy_img, g_vk.dummy_mem, 0);
            VkImageViewCreateInfo div_ci = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = g_vk.dummy_img,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            };
            g_vk.CreateImageView(g_vk.device, &div_ci, NULL, &g_vk.dummy_view);
        }
    }

    // Dummy 1x1 Storage Image
    VkImageCreateInfo dimg_s_ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { 1, 1, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    };
    if (g_vk.CreateImage(g_vk.device, &dimg_s_ci, NULL, &g_vk.dummy_storage_img) == VK_SUCCESS) {
        VkMemoryRequirements dimg_s_req;
        g_vk.GetImageMemoryRequirements(g_vk.device, g_vk.dummy_storage_img, &dimg_s_req);
        VkMemoryAllocateInfo dimg_s_ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = dimg_s_req.size,
            .memoryTypeIndex = find_mem_type(&mem_props, dimg_s_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        };
        if (g_vk.AllocateMemory(g_vk.device, &dimg_s_ai, NULL, &g_vk.dummy_storage_mem) == VK_SUCCESS) {
            g_vk.BindImageMemory(g_vk.device, g_vk.dummy_storage_img, g_vk.dummy_storage_mem, 0);
            VkImageViewCreateInfo div_s_ci = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = g_vk.dummy_storage_img,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            };
            g_vk.CreateImageView(g_vk.device, &div_s_ci, NULL, &g_vk.dummy_storage_view);
        }
    }

    // Upload 1x1 transparent black pixel (0x00000000) into dummy_img and transition images
    if (g_vk.staging_mapped && g_vk.dummy_img && g_vk.dummy_storage_img) {
        uint32_t *white_px = (uint32_t *)g_vk.staging_mapped;
        *white_px = 0x00000000u;
        if (g_vk.FlushMappedMemoryRanges) {
            VkMappedMemoryRange range = {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = g_vk.staging_mem,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };
            g_vk.FlushMappedMemoryRanges(g_vk.device, 1, &range);
        }

        VkCommandBuffer cb = g_vk.upload_cmd_buf;
        VkCommandBufferBeginInfo cb_bi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        g_vk.BeginCommandBuffer(cb, &cb_bi);

        VkImageMemoryBarrier b_trans[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .image = g_vk.dummy_img,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .image = g_vk.dummy_storage_img,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            }
        };
        g_vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, b_trans);

        VkBufferImageCopy copy_region = {
            .bufferOffset = 0,
            .bufferRowLength = 1,
            .bufferImageHeight = 1,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { 1, 1, 1 }
        };
        g_vk.CmdCopyBufferToImage(cb, g_vk.staging_buf, g_vk.dummy_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

        VkImageMemoryBarrier b_read = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = g_vk.dummy_img,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };
        g_vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &b_read);

        g_vk.EndCommandBuffer(cb);

        VkSubmitInfo si = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cb
        };
        g_vk.QueueSubmit(g_vk.queue, 1, &si, VK_NULL_HANDLE);
        g_vk.DeviceWaitIdle(g_vk.device);
    }

    VkSamplerCreateInfo samp_ci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 16.0f
    };
    g_vk.CreateSampler(g_vk.device, &samp_ci, NULL, &g_vk.dummy_sampler);

    // Descriptor Pool
    VkDescriptorPoolSize pool_sizes[3] = {
        { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 4096 },
        { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 4096 },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 512 }
    };
    VkDescriptorPoolCreateInfo dp_ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1024,
        .poolSizeCount = 3,
        .pPoolSizes = pool_sizes
    };
    g_vk.CreateDescriptorPool(g_vk.device, &dp_ci, NULL, &g_vk.desc_pool);

    g_vk.active = 1;
    fprintf(stderr, "[dm] [vk-win] Qualcomm Adreno Vulkan swapchain and graphics pipeline initialized (%ux%u, %u images)\n",
            g_vk.width, g_vk.height, g_vk.image_count);
    return 1;
}

void vk_win_begin_frame(void) {
    if (!g_vk.active || g_vk.frame_active) return;

    uint32_t frame_idx = g_vk.image_count > 0 ? (g_vk.cur_frame % g_vk.image_count) : 0;
    g_vk.frame_idx = frame_idx;
    // Synchronized per-frame with vkDeviceWaitIdle

    uint32_t img_idx = 0;
    VkResult res = g_vk.AcquireNextImageKHR(g_vk.device, g_vk.swapchain, UINT64_MAX, g_vk.sem_avail[frame_idx], VK_NULL_HANDLE, &img_idx);
    static int logged_acq = 0;
    if (logged_acq < 10) {
        logged_acq++;
        fprintf(stderr, "[dm] [vk-win] BEGIN_FRAME #%d: cur_frame=%u frame_idx=%u img_idx=%u acq_res=%d\n",
                logged_acq, g_vk.cur_frame, frame_idx, img_idx, (int)res);
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) return;

    g_vk.active_img_idx = img_idx;
    g_vk.index_buf_offset = 0;
    g_vk.vbuf_offset = 64;

    VkCommandBuffer cb = g_vk.cmd_bufs[frame_idx];
    if (g_vk.ResetCommandBuffer) g_vk.ResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo cb_bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    g_vk.BeginCommandBuffer(cb, &cb_bi);

    VkClearValue clear_val = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } };
    VkRenderPassBeginInfo rp_bi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = g_vk.render_pass,
        .framebuffer = g_vk.framebuffers[img_idx],
        .renderArea = { { 0, 0 }, { g_vk.width, g_vk.height } },
        .clearValueCount = 1,
        .pClearValues = &clear_val
    };
    g_vk.CmdBeginRenderPass(cb, &rp_bi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport = { 0.0f, 0.0f, (float)g_vk.width, (float)g_vk.height, 0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, { g_vk.width, g_vk.height } };
    g_vk.CmdSetViewport(cb, 0, 1, &viewport);
    g_vk.CmdSetScissor(cb, 0, 1, &scissor);

    g_vk.frame_active = 1;
    g_vk.draw_count = 0;
    g_vk.current_pipeline = NULL;
    g_vk.current_vs_key[0] = 0;
    g_vk.current_ps_key[0] = 0;
    g_vk.pixels_ready = 0;
}

static const void *g_vk_stage_tex[6][256];

void vk_win_set_tex(int stage, int slot, const void *tex_ptr) {
    if (stage >= 0 && stage < 6 && slot >= 0 && slot < 256) {
        g_vk_stage_tex[stage][slot] = tex_ptr;
    }
}

static const void *vk_get_stage_tex(int stage, int slot) {
    if (stage < 0 || stage >= 6) return NULL;
    if (slot >= 0 && slot < 256 && g_vk_stage_tex[stage][slot]) return g_vk_stage_tex[stage][slot];
    if (slot >= 0 && slot < 128 && g_vk_stage_tex[stage][128 + slot]) return g_vk_stage_tex[stage][128 + slot];
    // Fallback: slot 0, then any bound texture in this stage
    if (g_vk_stage_tex[stage][0]) return g_vk_stage_tex[stage][0];
    for (int i = 0; i < 256; i++) {
        if (g_vk_stage_tex[stage][i]) return g_vk_stage_tex[stage][i];
    }
    // Fallback to partner stage (VS <-> PS)
    int other = (stage == 0) ? 1 : (stage == 1) ? 0 : -1;
    if (other >= 0) {
        if (slot >= 0 && slot < 256 && g_vk_stage_tex[other][slot]) return g_vk_stage_tex[other][slot];
        if (g_vk_stage_tex[other][0]) return g_vk_stage_tex[other][0];
        for (int i = 0; i < 256; i++) {
            if (g_vk_stage_tex[other][i]) return g_vk_stage_tex[other][i];
        }
    }
    return NULL;
}

typedef struct {
    uint64_t serial;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    uint32_t width;
    uint32_t height;
    uint32_t format;
} vk_cached_tex;

#define VK_CACHED_TEX_MAX 256
static vk_cached_tex g_vk_cached_tex[VK_CACHED_TEX_MAX];
static int g_vk_cached_tex_count = 0;

static VkImageView vk_get_or_create_tex_view(const void *tex_ptr) {
    if (!tex_ptr) return g_vk.dummy_view;
    const tex_t *t = (const tex_t *)tex_ptr;
    static int logged_tv = 0;
    if (logged_tv < 30) {
        logged_tv++;
        fprintf(stderr, "[dm] [vk-win] get_or_create_tex_view #%d: tex=%p magic=%#x mem=%p w=%u h=%u fmt=%#x L.sz=%llu\n",
                logged_tv, (void*)t, t->magic, t->mem, t->d.width, t->d.height, t->d.format, (unsigned long long)t->L.size);
    }
    if (t->magic != MAGIC('T', 'E', 'X', 0) && t->magic != 0x00584554 && t->magic != 0x54455800 && t->magic != 0) return g_vk.dummy_view;
    if (!t->mem || t->d.width == 0 || t->d.height == 0 || t->L.size == 0) return g_vk.dummy_view;

    for (int i = 0; i < g_vk_cached_tex_count; i++) {
        if (g_vk_cached_tex[i].serial == t->serial) {
            return g_vk_cached_tex[i].view;
        }
    }
    if (g_vk_cached_tex_count >= VK_CACHED_TEX_MAX) return g_vk.dummy_view;

    uint32_t row_bytes = 0, rows = 0, slices = 0, bpb = 0;
    uint64_t base = 0; unsigned th = 0, td = 0;
    if (nvn_deswizzle_geom(&t->d, 0, 0, &row_bytes, &rows, &slices, &base, &th, &td, &bpb) != 0 || row_bytes == 0 || rows == 0) {
        return g_vk.dummy_view;
    }
    if (slices == 0) slices = 1;
    if (base >= t->L.size) {
        return g_vk.dummy_view;
    }

    int bpp = bpb > 0 ? (int)bpb : 4;
    VkFormat vk_fmt = VK_FORMAT_R8G8B8A8_UNORM;
    switch (t->d.format) {
        case 0x01: case 0x05: vk_fmt = VK_FORMAT_R8_UNORM; break;
        case 0x03: case 0x35: vk_fmt = VK_FORMAT_R8G8_UNORM; break;
        case 0x0B: case 0x13: vk_fmt = VK_FORMAT_R16_SFLOAT; break;
        case 0x25: vk_fmt = VK_FORMAT_R8G8B8A8_UNORM; break;
        case 0x26: vk_fmt = VK_FORMAT_R8G8B8A8_SRGB; break;
        case 0x29: case 0x2A: vk_fmt = VK_FORMAT_R16G16B16A16_SFLOAT; break;
        case 0x2E: case 0x34: vk_fmt = VK_FORMAT_R32_SFLOAT; break;
        case 0x37: vk_fmt = VK_FORMAT_B8G8R8A8_UNORM; break;
        case 0x38: vk_fmt = VK_FORMAT_B8G8R8A8_SRGB; break;
        case 0x3F: vk_fmt = VK_FORMAT_B10G11R11_UFLOAT_PACK32; break;
        case 0x41: case 0x42: vk_fmt = VK_FORMAT_BC1_RGBA_UNORM_BLOCK; break;
        case 0x43: vk_fmt = VK_FORMAT_BC2_UNORM_BLOCK; break;
        case 0x44: vk_fmt = VK_FORMAT_BC3_UNORM_BLOCK; break;
        case 0x45: case 0x46: vk_fmt = VK_FORMAT_BC1_RGBA_SRGB_BLOCK; break;
        case 0x47: vk_fmt = VK_FORMAT_BC2_SRGB_BLOCK; break;
        case 0x48: vk_fmt = VK_FORMAT_BC3_SRGB_BLOCK; break;
        case 0x49: vk_fmt = VK_FORMAT_BC4_UNORM_BLOCK; break;
        case 0x4A: vk_fmt = VK_FORMAT_BC4_SNORM_BLOCK; break;
        case 0x4B: vk_fmt = VK_FORMAT_BC5_UNORM_BLOCK; break;
        case 0x4C: vk_fmt = VK_FORMAT_BC5_SNORM_BLOCK; break;
        case 0x4D: vk_fmt = VK_FORMAT_BC7_UNORM_BLOCK; break;
        case 0x4E: vk_fmt = VK_FORMAT_BC7_SRGB_BLOCK; break;
        case 0x4F: vk_fmt = VK_FORMAT_BC6H_SFLOAT_BLOCK; break;
        case 0x50: vk_fmt = VK_FORMAT_BC6H_UFLOAT_BLOCK; break;
        default:
            if (bpp == 1) vk_fmt = VK_FORMAT_R8_UNORM;
            else if (bpp == 2) vk_fmt = VK_FORMAT_R8G8_UNORM;
            else if (bpp == 8) vk_fmt = VK_FORMAT_R16G16B16A16_SFLOAT;
            else vk_fmt = VK_FORMAT_R8G8B8A8_UNORM;
            break;
    }

    size_t lin_sz = (size_t)row_bytes * (size_t)rows * (size_t)slices;
    if (lin_sz == 0 || lin_sz > 256 * 1024 * 1024) return g_vk.dummy_view;

    uint8_t *lin = malloc(lin_sz);
    if (!lin) return g_vk.dummy_view;

    if (nvn_deswizzle(&t->d, 0, 0, t->mem, lin, row_bytes) != 0) {
        free(lin);
        return g_vk.dummy_view;
    }

    VkImageType img_type = (t->d.target == 2) ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    VkImageViewType view_type = (t->d.target == 2) ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
    uint32_t depth = (t->d.target == 2 && t->d.depth > 0) ? t->d.depth : 1;

    VkImageCreateInfo img_ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = img_type,
        .format = vk_fmt,
        .extent = { t->d.width, t->d.height, depth },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    };
    VkImage vk_img = VK_NULL_HANDLE;
    if (g_vk.CreateImage(g_vk.device, &img_ci, NULL, &vk_img) != VK_SUCCESS) {
        free(lin);
        return g_vk.dummy_view;
    }

    VkPhysicalDeviceMemoryProperties mem_props;
    g_vk.GetPhysicalDeviceMemoryProperties(g_vk.phys_dev, &mem_props);
    VkMemoryRequirements req;
    g_vk.GetImageMemoryRequirements(g_vk.device, vk_img, &req);
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_mem_type(&mem_props, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    VkDeviceMemory vk_mem = VK_NULL_HANDLE;
    if (g_vk.AllocateMemory(g_vk.device, &ai, NULL, &vk_mem) != VK_SUCCESS) {
        g_vk.DestroyImage(g_vk.device, vk_img, NULL);
        free(lin);
        return g_vk.dummy_view;
    }
    g_vk.BindImageMemory(g_vk.device, vk_img, vk_mem, 0);

    if (g_vk.staging_mapped && g_vk.staging_buf) {
        size_t aligned_sz = (lin_sz + 255) & ~255ULL;
        if (g_vk.staging_offset + aligned_sz > g_vk.staging_size) {
            g_vk.staging_offset = 0;
        }
        size_t upload_off = g_vk.staging_offset;
        g_vk.staging_offset += aligned_sz;

        memcpy((uint8_t *)g_vk.staging_mapped + upload_off, lin, lin_sz);
        if (g_vk.FlushMappedMemoryRanges) {
            VkMappedMemoryRange range = {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = g_vk.staging_mem,
                .offset = upload_off,
                .size = aligned_sz
            };
            g_vk.FlushMappedMemoryRanges(g_vk.device, 1, &range);
        }

        fprintf(stderr, "[dm] [vk-win] COPY_IMG_DEBUG: ser=%llu %ux%u fmt=%#x bpp=%d row_bytes=%u rows=%u slices=%u depth=%u off=%zu sz=%zu staging_sz=%zu req_sz=%llu in_frame=%d\n",
                (unsigned long long)t->serial, t->d.width, t->d.height, t->d.format, bpp, row_bytes, rows, slices, depth,
                upload_off, aligned_sz, g_vk.staging_size, (unsigned long long)req.size, g_vk.frame_active);

        VkBufferImageCopy copy_region = {
            .bufferOffset = upload_off,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { t->d.width, t->d.height, depth }
        };

        if (g_vk.frame_active && g_vk.render_pass_resume) {
            // In-frame: record transfer directly onto active frame's command buffer!
            // End the current render pass, run the barrier + copy + barrier, then resume with render_pass_resume (loadOp = LOAD)
            uint32_t frame_idx = g_vk.frame_idx;
            VkCommandBuffer cb = g_vk.cmd_bufs[frame_idx];

            g_vk.CmdEndRenderPass(cb);

            VkImageMemoryBarrier b1 = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vk_img,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            };
            g_vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b1);

            g_vk.CmdCopyBufferToImage(cb, g_vk.staging_buf, vk_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

            VkImageMemoryBarrier b2 = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vk_img,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            };
            g_vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &b2);

            // Resume render pass with LOAD
            VkRenderPassBeginInfo resume_rp_bi = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = g_vk.render_pass_resume,
                .framebuffer = g_vk.framebuffers[g_vk.active_img_idx],
                .renderArea = { { 0, 0 }, { g_vk.width, g_vk.height } },
                .clearValueCount = 0,
                .pClearValues = NULL
            };
            g_vk.CmdBeginRenderPass(cb, &resume_rp_bi, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport = { 0.0f, 0.0f, (float)g_vk.width, (float)g_vk.height, 0.0f, 1.0f };
            VkRect2D scissor = { { 0, 0 }, { g_vk.width, g_vk.height } };
            g_vk.CmdSetViewport(cb, 0, 1, &viewport);
            g_vk.CmdSetScissor(cb, 0, 1, &scissor);
            if (g_vk.current_pipeline) {
                g_vk.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.current_pipeline->pipeline);
            }
        } else if (g_vk.upload_cmd_buf) {
            // Outside of active frame: record onto upload_cmd_buf and submit
            if (g_vk.ResetCommandBuffer) g_vk.ResetCommandBuffer(g_vk.upload_cmd_buf, 0);
            VkCommandBuffer cb = g_vk.upload_cmd_buf;
            VkCommandBufferBeginInfo cb_bi = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
            };
            g_vk.BeginCommandBuffer(cb, &cb_bi);

            VkImageMemoryBarrier b1 = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vk_img,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            };
            g_vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b1);

            g_vk.CmdCopyBufferToImage(cb, g_vk.staging_buf, vk_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

            VkImageMemoryBarrier b2 = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vk_img,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            };
            g_vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &b2);

            g_vk.EndCommandBuffer(cb);

            VkSubmitInfo si = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &cb
            };
            g_vk.QueueSubmit(g_vk.queue, 1, &si, VK_NULL_HANDLE);
            g_vk.DeviceWaitIdle(g_vk.device);
        }
    }
    free(lin);

    VkImageViewCreateInfo view_ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk_img,
        .viewType = view_type,
        .format = vk_fmt,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };
    VkImageView vk_view = VK_NULL_HANDLE;
    if (g_vk.CreateImageView(g_vk.device, &view_ci, NULL, &vk_view) != VK_SUCCESS) {
        g_vk.DestroyImage(g_vk.device, vk_img, NULL);
        g_vk.FreeMemory(g_vk.device, vk_mem, NULL);
        return g_vk.dummy_view;
    }

    int idx = g_vk_cached_tex_count++;
    g_vk_cached_tex[idx].serial = t->serial;
    g_vk_cached_tex[idx].image = vk_img;
    g_vk_cached_tex[idx].memory = vk_mem;
    g_vk_cached_tex[idx].view = vk_view;
    g_vk_cached_tex[idx].width = t->d.width;
    g_vk_cached_tex[idx].height = t->d.height;
    g_vk_cached_tex[idx].format = t->d.format;

    fprintf(stderr, "[dm] [vk-win] Uploaded Switch texture ser=%llu %ux%u fmt=%#x to Vulkan image (view=%p)\n",
            (unsigned long long)t->serial, t->d.width, t->d.height, t->d.format, (void*)vk_view);

    return vk_view;
}

static void vk_update_and_bind_descriptors(VkCommandBuffer cb, vk_pipeline_entry *p) {
    if (!p) return;
    uint32_t frame_idx = g_vk.frame_idx;
    VkWriteDescriptorSet writes[64];
    VkDescriptorBufferInfo db_info[64];
    VkDescriptorImageInfo di_info[64];
    int n_writes = 0;

    vk_shader_entry *vs_se = &g_vk_shaders[p->vs_sh_idx];
    for (int i = 0; i < vs_se->binding_count && n_writes < 64; i++) {
        vk_refl_binding_t *b = &vs_se->bindings[i];
        if (b->is_support) {
            db_info[n_writes] = (VkDescriptorBufferInfo){ .buffer = g_vk.support_buf, .offset = 0, .range = 65536 };
            writes[n_writes] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = p->desc_sets[frame_idx][0],
                .dstBinding = b->binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &db_info[n_writes]
            };
            n_writes++;
        } else if (b->is_driver) {
            db_info[n_writes] = (VkDescriptorBufferInfo){ .buffer = g_vk.driver_buf, .offset = 0, .range = 65536 };
            writes[n_writes] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = p->desc_sets[frame_idx][0],
                .dstBinding = b->binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &db_info[n_writes]
            };
            n_writes++;
        } else if (b->is_c1 || b->is_c2) {
            db_info[n_writes] = (VkDescriptorBufferInfo){ .buffer = g_vk.c1_buf, .offset = vs_se->c1_offset, .range = 65536 };
            writes[n_writes] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = p->desc_sets[frame_idx][0],
                .dstBinding = b->binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &db_info[n_writes]
            };
            n_writes++;
        } else if (b->is_ubo) {
            size_t off = ((size_t)b->ubo_stage * 16 + (size_t)b->ubo_index) * 65536;
            size_t rsz = g_vk.ubo_sizes[b->ubo_stage][b->ubo_index];
            if (rsz == 0) rsz = 65536;
            if (g_vk.ubo_addrs[b->ubo_stage][b->ubo_index] && g_vk.ubo_data_mapped) {
                size_t copy_sz = rsz < 65536 ? rsz : 65536;
                memcpy((uint8_t *)g_vk.ubo_data_mapped + off, g_vk.ubo_addrs[b->ubo_stage][b->ubo_index], copy_sz);
                if (g_vk.FlushMappedMemoryRanges) {
                    VkMappedMemoryRange range = {
                        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                        .memory = g_vk.ubo_data_mem,
                        .offset = off,
                        .size = (copy_sz + 0xFF) & ~0xFFull
                    };
                    g_vk.FlushMappedMemoryRanges(g_vk.device, 1, &range);
                }
            }
            db_info[n_writes] = (VkDescriptorBufferInfo){ .buffer = g_vk.ubo_data_buf, .offset = off, .range = rsz };
            writes[n_writes] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = p->desc_sets[frame_idx][0],
                .dstBinding = b->binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &db_info[n_writes]
            };
            n_writes++;
        } else if (b->is_sampler) {
            const void *st_tex = vk_get_stage_tex(0, b->tex_slot);
            VkImageView view = vk_get_or_create_tex_view(st_tex);
            di_info[n_writes] = (VkDescriptorImageInfo){
                .sampler = g_vk.dummy_sampler,
                .imageView = view ? view : g_vk.dummy_view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            writes[n_writes] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = p->desc_sets[frame_idx][0],
                .dstBinding = b->binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &di_info[n_writes]
            };
            n_writes++;
        }
    }

    vk_shader_entry *ps_se = &g_vk_shaders[p->ps_sh_idx];
    for (int i = 0; i < ps_se->binding_count && n_writes < 64; i++) {
        vk_refl_binding_t *b = &ps_se->bindings[i];
        if (b->is_support) {
            db_info[n_writes] = (VkDescriptorBufferInfo){ .buffer = g_vk.support_buf, .offset = 0, .range = 65536 };
            writes[n_writes] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = p->desc_sets[frame_idx][1],
                .dstBinding = b->binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &db_info[n_writes]
            };
            n_writes++;
        } else if (b->is_driver) {
            db_info[n_writes] = (VkDescriptorBufferInfo){ .buffer = g_vk.driver_buf, .offset = 0, .range = 65536 };
            writes[n_writes] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = p->desc_sets[frame_idx][1],
                .dstBinding = b->binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &db_info[n_writes]
            };
            n_writes++;
        } else if (b->is_c1 || b->is_c2) {
            db_info[n_writes] = (VkDescriptorBufferInfo){ .buffer = g_vk.c1_buf, .offset = ps_se->c1_offset, .range = 65536 };
            writes[n_writes] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = p->desc_sets[frame_idx][1],
                .dstBinding = b->binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &db_info[n_writes]
            };
            n_writes++;
        } else if (b->is_ubo) {
            size_t off = ((size_t)b->ubo_stage * 16 + (size_t)b->ubo_index) * 65536;
            size_t rsz = g_vk.ubo_sizes[b->ubo_stage][b->ubo_index];
            if (rsz == 0) rsz = 65536;
            if (g_vk.ubo_addrs[b->ubo_stage][b->ubo_index] && g_vk.ubo_data_mapped) {
                size_t copy_sz = rsz < 65536 ? rsz : 65536;
                memcpy((uint8_t *)g_vk.ubo_data_mapped + off, g_vk.ubo_addrs[b->ubo_stage][b->ubo_index], copy_sz);
                if (g_vk.FlushMappedMemoryRanges) {
                    VkMappedMemoryRange range = {
                        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                        .memory = g_vk.ubo_data_mem,
                        .offset = off,
                        .size = (copy_sz + 0xFF) & ~0xFFull
                    };
                    g_vk.FlushMappedMemoryRanges(g_vk.device, 1, &range);
                }
            }
            db_info[n_writes] = (VkDescriptorBufferInfo){ .buffer = g_vk.ubo_data_buf, .offset = off, .range = rsz };
            writes[n_writes] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = p->desc_sets[frame_idx][1],
                .dstBinding = b->binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &db_info[n_writes]
            };
            n_writes++;
        } else if (b->is_sampler) {
            const void *st_tex = vk_get_stage_tex(1, b->tex_slot);
            VkImageView view = vk_get_or_create_tex_view(st_tex);
            if (view && view != g_vk.dummy_view) {
                static int logged_ps_tex = 0;
                if (logged_ps_tex < 30) {
                    logged_ps_tex++;
                    fprintf(stderr, "[dm] [vk-win] PS_TEX #%d slot=%d view=%p vs=%s ps=%s\n",
                            logged_ps_tex, b->tex_slot, (void*)view, p->vs_key, p->ps_key);
                }
            }
            di_info[n_writes] = (VkDescriptorImageInfo){
                .sampler = g_vk.dummy_sampler,
                .imageView = view ? view : g_vk.dummy_view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            writes[n_writes] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = p->desc_sets[frame_idx][1],
                .dstBinding = b->binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &di_info[n_writes]
            };
            n_writes++;
        } else if (b->is_storage_img) {
            di_info[n_writes] = (VkDescriptorImageInfo){
                .imageView = g_vk.dummy_storage_view,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL
            };
            writes[n_writes] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = p->desc_sets[frame_idx][1],
                .dstBinding = b->binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = &di_info[n_writes]
            };
            n_writes++;
        }
    }

    if (n_writes > 0) {
        g_vk.UpdateDescriptorSets(g_vk.device, (uint32_t)n_writes, writes, 0, NULL);
    }
    g_vk.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p->layout, 0, 2, p->desc_sets[frame_idx], 0, NULL);
}

int vk_win_bind_program(const char *vs_key, const char *ps_key) {
    if (!g_vk.active || !vs_key || !ps_key) return 0;
    snprintf(g_vk.current_vs_key, sizeof(g_vk.current_vs_key), "%s", vs_key);
    snprintf(g_vk.current_ps_key, sizeof(g_vk.current_ps_key), "%s", ps_key);
    g_vk.current_pipeline = NULL;
    return 1;
}

void vk_win_set_viewport(float x, float y, float w, float h) {
    if (!g_vk.active || !g_vk.frame_active) return;
    uint32_t frame_idx = g_vk.frame_idx;
    static int logged_vp = 0;
    if (logged_vp < 5) {
        logged_vp++;
        fprintf(stderr, "[dm] [vk-win] SET_VIEWPORT #%d: x=%g y=%g w=%g h=%g -> full swapchain %ux%u\n",
                logged_vp, x, y, w, h, g_vk.width, g_vk.height);
    }
    VkViewport vp = { 0.0f, 0.0f, (float)g_vk.width, (float)g_vk.height, 0.0f, 1.0f };
    g_vk.CmdSetViewport(g_vk.cmd_bufs[frame_idx], 0, 1, &vp);
}

void vk_win_set_scissor(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!g_vk.active || !g_vk.frame_active) return;
    uint32_t frame_idx = g_vk.frame_idx;
    static int logged_sc = 0;
    if (logged_sc < 5) {
        logged_sc++;
        fprintf(stderr, "[dm] [vk-win] SET_SCISSOR #%d: raw x=%d y=%d w=%u h=%u -> full swapchain %ux%u\n",
                logged_sc, x, y, w, h, g_vk.width, g_vk.height);
    }
    VkRect2D sc = { { 0, 0 }, { g_vk.width, g_vk.height } };
    g_vk.CmdSetScissor(g_vk.cmd_bufs[frame_idx], 0, 1, &sc);
}

static int get_max_draws(void) {
    static int s_max_draws = -2;
    if (s_max_draws == -2) {
        const char *e = getenv("DM_MAX_DRAWS");
        s_max_draws = e ? atoi(e) : -1;
    }
    return s_max_draws;
}

static inline int is_skip_ps(const char *ps_key) {
    if (!ps_key) return 0;
    if (strcmp(ps_key, "8edcbb9b8ce88433") == 0) return 1; // 48k word fullscreen clear/overlay
    if (strcmp(ps_key, "da917a747d97908e") == 0) return 1; // Backbuffer blit sampling uninitialized offscreen buffer
    if (strcmp(ps_key, "3669a68e6d6e6b3f") == 0) return 1; // Fullscreen dark quad
    if (strcmp(ps_key, "fd446beabbd201d9") == 0) return 1; // Constant cyan/white quad
    return 0;
}

static void vk_bind_vertex_buffers(VkCommandBuffer cb, vk_pipeline_entry *p) {
    if (!g_vk.CmdBindVertexBuffers || !p || !g_vk.vbuf_mapped) return;
    vk_shader_entry *vs_se = &g_vk_shaders[p->vs_sh_idx];
    if (vs_se->num_inputs <= 0) return;

    int stream_bound[16] = {0};
    for (int k = 0; k < vs_se->num_inputs; k++) {
        uint32_t loc = vs_se->input_locations[k];
        uint32_t stream = (loc < 16) ? g_vk.vattrs[loc].stream : 0;
        if (stream < 16 && !stream_bound[stream]) {
            stream_bound[stream] = 1;
            VkDeviceSize off = 0;
            if (g_vk.vbufs[stream].addr && g_vk.vbufs[stream].size > 0) {
                size_t cur_off = (g_vk.vbuf_offset + 63) & ~63ULL;
                size_t copy_sz = g_vk.vbufs[stream].size;
                if (copy_sz > 1048576) copy_sz = 1048576;
                if (cur_off + copy_sz <= 16777216) {
                    memcpy((uint8_t *)g_vk.vbuf_mapped + cur_off, g_vk.vbufs[stream].addr, copy_sz);
                    if (g_vk.FlushMappedMemoryRanges) {
                        VkMappedMemoryRange range = {
                            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                            .memory = g_vk.vbuf_mem,
                            .offset = cur_off,
                            .size = (copy_sz + 0xFF) & ~0xFFULL
                        };
                        g_vk.FlushMappedMemoryRanges(g_vk.device, 1, &range);
                    }
                    off = cur_off;
                    g_vk.vbuf_offset = cur_off + copy_sz;
                }
            }
            g_vk.CmdBindVertexBuffers(cb, stream, 1, &g_vk.vbuf, &off);
        }
    }
}

void vk_win_draw(uint32_t first, uint32_t count) {
    if (!g_vk.active) return;
    int max_d = get_max_draws();
    if (max_d >= 0 && (int)g_vk.draw_count >= max_d) return;
    if (!g_vk.frame_active) vk_win_begin_frame();
    if (!g_vk.frame_active) return;
    if (!g_vk.current_vs_key[0] || !g_vk.current_ps_key[0]) return;
    if (is_skip_ps(g_vk.current_ps_key)) return;

    vk_pipeline_entry *p = vk_get_or_create_pipeline_entry(g_vk.current_vs_key, g_vk.current_ps_key);
    if (!p) return;
    g_vk.current_pipeline = p;

    if (g_vk.driver_mapped) {
        *(uint32_t *)((uint8_t *)g_vk.driver_mapped + 0x640) = first;
        if (g_vk.FlushMappedMemoryRanges) {
            VkMappedMemoryRange range = {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = g_vk.driver_mem,
                .offset = 0,
                .size = 65536
            };
            g_vk.FlushMappedMemoryRanges(g_vk.device, 1, &range);
        }
    }

    uint32_t frame_idx = g_vk.frame_idx;
    VkCommandBuffer cb = g_vk.cmd_bufs[frame_idx];
    vk_update_and_bind_descriptors(cb, g_vk.current_pipeline);
    vk_bind_vertex_buffers(cb, g_vk.current_pipeline);
    g_vk.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.current_pipeline->pipeline);
    if (g_vk.cur_frame == 60) {
        fprintf(stderr, "[dm] [vk-win] DRAW [f=60] count=%u first=%u vs=%s ps=%s\n",
                count, first, g_vk.current_pipeline->vs_key, g_vk.current_pipeline->ps_key);
    }
    g_vk.CmdDraw(cb, count, 1, first, 0);
    g_vk.draw_count++;
}

void vk_win_draw_indexed(int prim, int itype, uint32_t count, const void *ibuf, int32_t base_vertex) {
    if (!g_vk.active) return;
    int max_d = get_max_draws();
    if (max_d >= 0 && (int)g_vk.draw_count >= max_d) return;
    if (!g_vk.frame_active) vk_win_begin_frame();
    if (!g_vk.frame_active) return;
    if (!g_vk.current_vs_key[0] || !g_vk.current_ps_key[0]) return;
    if (is_skip_ps(g_vk.current_ps_key)) return;

    vk_pipeline_entry *p = vk_get_or_create_pipeline_entry(g_vk.current_vs_key, g_vk.current_ps_key);
    if (!p) return;
    g_vk.current_pipeline = p;

    if (g_vk.driver_mapped) {
        *(uint32_t *)((uint8_t *)g_vk.driver_mapped + 0x640) = (uint32_t)base_vertex;
        if (g_vk.FlushMappedMemoryRanges) {
            VkMappedMemoryRange range = {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = g_vk.driver_mem,
                .offset = 0,
                .size = 65536
            };
            g_vk.FlushMappedMemoryRanges(g_vk.device, 1, &range);
        }
    }

    uint32_t frame_idx = g_vk.frame_idx;
    VkCommandBuffer cb = g_vk.cmd_bufs[frame_idx];
    vk_update_and_bind_descriptors(cb, g_vk.current_pipeline);
    vk_bind_vertex_buffers(cb, g_vk.current_pipeline);
    g_vk.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.current_pipeline->pipeline);

    if (ibuf && count > 0 && g_vk.index_mapped && g_vk.CmdBindIndexBuffer && g_vk.CmdDrawIndexed) {
        size_t cur_off = (g_vk.index_buf_offset + 3) & ~3ull;
        VkIndexType vk_itype = VK_INDEX_TYPE_UINT16;
        size_t bytes_needed = 0;

        if (itype == 0) {
            bytes_needed = (size_t)count * sizeof(uint16_t);
            if (cur_off + bytes_needed <= 4194304) {
                uint16_t *dst = (uint16_t *)((uint8_t *)g_vk.index_mapped + cur_off);
                const uint8_t *src = (const uint8_t *)ibuf;
                for (uint32_t i = 0; i < count; i++) dst[i] = (uint16_t)src[i];
                vk_itype = VK_INDEX_TYPE_UINT16;
            }
        } else if (itype == 1) {
            bytes_needed = (size_t)count * sizeof(uint16_t);
            if (cur_off + bytes_needed <= 4194304) {
                memcpy((uint8_t *)g_vk.index_mapped + cur_off, ibuf, bytes_needed);
                vk_itype = VK_INDEX_TYPE_UINT16;
            }
        } else if (itype == 2) {
            bytes_needed = (size_t)count * sizeof(uint32_t);
            if (cur_off + bytes_needed <= 4194304) {
                memcpy((uint8_t *)g_vk.index_mapped + cur_off, ibuf, bytes_needed);
                vk_itype = VK_INDEX_TYPE_UINT32;
            }
        }

        if (bytes_needed > 0 && cur_off + bytes_needed <= 4194304) {
            if (g_vk.FlushMappedMemoryRanges) {
                VkMappedMemoryRange range = {
                    .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                    .memory = g_vk.index_mem,
                    .offset = cur_off,
                    .size = (bytes_needed + 0xFF) & ~0xFFull
                };
                g_vk.FlushMappedMemoryRanges(g_vk.device, 1, &range);
            }
            static int logged_idx_draw = 0;
            if (logged_idx_draw < 10) {
                logged_idx_draw++;
                fprintf(stderr, "[dm] [vk-win] CmdDrawIndexed #%d count=%u itype=%d cur_off=%zu vs=%s ps=%s\n",
                        logged_idx_draw, count, itype, cur_off,
                        g_vk.current_pipeline->vs_key, g_vk.current_pipeline->ps_key);
            }
            g_vk.CmdBindIndexBuffer(cb, g_vk.index_buf, cur_off, vk_itype);
            if (g_vk.cur_frame == 60) {
                fprintf(stderr, "[dm] [vk-win] DRAW_IDX [f=60] count=%u bv=%d vs=%s ps=%s\n",
                        count, base_vertex, g_vk.current_pipeline->vs_key, g_vk.current_pipeline->ps_key);
            }
            g_vk.CmdDrawIndexed(cb, count, 1, 0, base_vertex, 0);
            g_vk.index_buf_offset = cur_off + bytes_needed;
            g_vk.draw_count++;
            return;
        }
    }

    g_vk.CmdDraw(cb, count, 1, 0, 0);
    g_vk.draw_count++;
}

static int g_vk_capture_requested = 0;
void vk_win_request_capture(void) {
    g_vk_capture_requested = 1;
}

void vk_win_end_frame(void) {
    if (!g_vk.active || !g_vk.frame_active) return;

    uint32_t frame_idx = g_vk.frame_idx;
    VkCommandBuffer cb = g_vk.cmd_bufs[frame_idx];

    if (g_vk.draw_count == 21) {
        fprintf(stderr, "[dm] [vk-win] DIAG DRAW #21: vs=%s ps=%s pipe=%p\n",
                g_vk.current_pipeline->vs_key, g_vk.current_pipeline->ps_key, (void*)g_vk.current_pipeline->pipeline);
        vk_shader_entry *vs_se = &g_vk_shaders[g_vk.current_pipeline->vs_sh_idx];
        fprintf(stderr, "[dm] [vk-win] DIAG DRAW #21 vs_inputs=%d\n", vs_se->num_inputs);
        for (int k = 0; k < vs_se->num_inputs; k++) {
            uint32_t loc = vs_se->input_locations[k];
            uint32_t stream = (loc < 16) ? g_vk.vattrs[loc].stream : 0;
            fprintf(stderr, "[dm] [vk-win] DIAG DRAW #21 attr %d: loc=%u stream=%u fmt=%#x off=%u vbuf_addr=%p vbuf_sz=%llu\n",
                    k, loc, stream, g_vk.vattrs[loc].fmt, g_vk.vattrs[loc].offset,
                    g_vk.vbufs[stream].addr, (unsigned long long)g_vk.vbufs[stream].size);
        }
        vk_shader_entry *ps_se = &g_vk_shaders[g_vk.current_pipeline->ps_sh_idx];
        fprintf(stderr, "[dm] [vk-win] DIAG DRAW #21 ps_bindings=%d\n", ps_se->binding_count);
        for (int k = 0; k < ps_se->binding_count; k++) {
            fprintf(stderr, "[dm] [vk-win] DIAG DRAW #21 ps_bind %d: b=%u type=%d ubo=%d samp=%d slot=%d\n",
                    k, ps_se->bindings[k].binding, ps_se->bindings[k].type,
                    ps_se->bindings[k].is_ubo, ps_se->bindings[k].is_sampler, ps_se->bindings[k].tex_slot);
        }
    }

    g_vk.CmdEndRenderPass(cb);

    int need_shot = g_vk_capture_requested;
    g_vk_capture_requested = 0;

    if (need_shot && g_vk.readback_buf && g_vk.readback_mapped && g_vk.active_img_idx < g_vk.image_count) {
        VkImageMemoryBarrier b1 = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = g_vk.images[g_vk.active_img_idx],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };
        g_vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b1);

        VkBufferImageCopy region = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { g_vk.width, g_vk.height, 1 }
        };
        g_vk.CmdCopyImageToBuffer(cb, g_vk.images[g_vk.active_img_idx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_vk.readback_buf, 1, &region);

        VkBufferMemoryBarrier buf_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = g_vk.readback_buf,
            .offset = 0,
            .size = VK_WHOLE_SIZE
        };
        g_vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &buf_barrier, 0, NULL);

        VkImageMemoryBarrier b2 = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = g_vk.images[g_vk.active_img_idx],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };
        g_vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &b2);
    }

    VkResult ecb_res = g_vk.EndCommandBuffer(cb);
    static int logged_end = 0;
    if (logged_end < 5 || g_vk.cur_frame == 60) {
        logged_end++;
        fprintf(stderr, "[dm] [vk-win] END_FRAME #%d: cur_frame=%u frame_idx=%u img=%u draws=%u ecb_res=%d\n",
                logged_end, g_vk.cur_frame, frame_idx, g_vk.active_img_idx, g_vk.draw_count, (int)ecb_res);
    }

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &g_vk.sem_avail[frame_idx],
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &cb,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &g_vk.sem_finish[frame_idx]
    };
    VkResult fstatus = g_vk.GetFenceStatus ? g_vk.GetFenceStatus(g_vk.device, g_vk.fences[frame_idx]) : VK_NOT_READY;
    if (fstatus == VK_SUCCESS) {
        g_vk.ResetFences(g_vk.device, 1, &g_vk.fences[frame_idx]);
    }
    static int logged_submit = 0;
    if (logged_submit < 5) {
        fprintf(stderr, "[dm] [vk-win] SUBMIT DETAILS #%d frame_idx=%u cb=%p sem_avail=%p sem_fin=%p fence=%p fstatus=%d\n",
                logged_submit + 1, frame_idx, (void*)cb, (void*)g_vk.sem_avail[frame_idx], (void*)g_vk.sem_finish[frame_idx], (void*)g_vk.fences[frame_idx], (int)fstatus);
    }

    VkResult qres = g_vk.QueueSubmit(g_vk.queue, 1, &submit_info, VK_NULL_HANDLE);
    if (qres != VK_SUCCESS) {
        static int logged_fail = 0;
        if (logged_fail++ < 20) {
            fprintf(stderr, "[dm] [RED] [vk-win] vkQueueSubmit FAILED cur_frame=%u frame_idx=%u res=%d draws=%u\n",
                    g_vk.cur_frame, frame_idx, (int)qres, g_vk.draw_count);
        }
    }
    if (need_shot) {
        VkResult widle = g_vk.DeviceWaitIdle(g_vk.device);
        if (logged_submit < 5) {
            logged_submit++;
            fprintf(stderr, "[dm] [vk-win] QueueSubmit #%d res=%d (DeviceWaitIdle res=%d)\n",
                    logged_submit, (int)qres, (int)widle);
        }

        if (g_vk.readback_mapped) {
            if (g_vk.InvalidateMappedMemoryRanges) {
                VkMappedMemoryRange range = {
                    .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                    .memory = g_vk.readback_mem,
                    .offset = 0,
                    .size = VK_WHOLE_SIZE
                };
                g_vk.InvalidateMappedMemoryRanges(g_vk.device, 1, &range);
            }
            uint32_t *px = (uint32_t *)g_vk.readback_mapped;
            static int logged_px = 0;
            if (logged_px < 5) {
                logged_px++;
                fprintf(stderr, "[dm] [vk-win] GPU Readback Frame #%d pixels[0..3]=%#010x %#010x %#010x %#010x\n",
                        logged_px, px[0], px[1], px[2], px[3]);
            }
        }
    }

    VkPresentInfoKHR pres_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &g_vk.sem_finish[frame_idx],
        .swapchainCount = 1,
        .pSwapchains = &g_vk.swapchain,
        .pImageIndices = &g_vk.active_img_idx
    };
    VkResult pres_res = g_vk.QueuePresentKHR(g_vk.queue, &pres_info);

    static int logged_frame = 0;
    if (logged_frame < 10) {
        logged_frame++;
        fprintf(stderr, "[dm] [vk-win] FRAME #%d PRESENTED TO SCREEN: draws=%u img=%u (present_res=%d)\n",
                logged_frame, g_vk.draw_count, g_vk.active_img_idx, (int)pres_res);
    }

    g_vk.pixels_ready = need_shot ? 1 : 0;
    g_vk.frame_active = 0;
    g_vk.staging_offset = 0;
    g_vk.cur_frame++;
    if (g_vk.image_count > 0) {
        g_vk.frame_idx = g_vk.cur_frame % g_vk.image_count;
    }
}

int vk_win_get_pixels(void **out_pixels, int *out_w, int *out_h) {
    if (!g_vk.active || !g_vk.pixels_ready || !g_vk.readback_mapped) return 0;
    if (out_pixels) *out_pixels = g_vk.readback_mapped;
    if (out_w) *out_w = (int)g_vk.width;
    if (out_h) *out_h = (int)g_vk.height;
    return 1;
}

void vk_win_present(const uint32_t *bgra, int width, int height) {
    if (!g_vk.active) return;

    // If graphics commands were recorded this frame, submit and present the GPU framebuffer
    if (g_vk.frame_active) {
        vk_win_end_frame();
        return;
    }

    if (!bgra) return;

    // CPU fallback presentation (copy buffer to swapchain)
    uint32_t frame_idx = g_vk.image_count > 0 ? (g_vk.cur_frame % g_vk.image_count) : 0;
    g_vk.frame_idx = frame_idx;
    // Synchronized per-frame with vkDeviceWaitIdle

    uint32_t img_idx = 0;
    VkResult res = g_vk.AcquireNextImageKHR(g_vk.device, g_vk.swapchain, UINT64_MAX, g_vk.sem_avail[frame_idx], VK_NULL_HANDLE, &img_idx);
    static int logged_acq = 0;
    if (logged_acq < 10) {
        logged_acq++;
        fprintf(stderr, "[dm] [vk-win] BEGIN_FRAME #%d: cur_frame=%u frame_idx=%u img_idx=%u acq_res=%d\n",
                logged_acq, g_vk.cur_frame, frame_idx, img_idx, (int)res);
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) return;

    if (g_vk.staging_mapped) {
        if (width == (int)g_vk.width && height == (int)g_vk.height) {
            memcpy(g_vk.staging_mapped, bgra, (size_t)g_vk.width * g_vk.height * 4);
        } else {
            uint32_t *dst = (uint32_t *)g_vk.staging_mapped;
            for (uint32_t dy = 0; dy < g_vk.height; dy++) {
                int sy = (int)((uint64_t)dy * height / g_vk.height);
                for (uint32_t dx = 0; dx < g_vk.width; dx++) {
                    int sx = (int)((uint64_t)dx * width / g_vk.width);
                    dst[dy * g_vk.width + dx] = bgra[sy * width + sx];
                }
            }
        }
    }

    VkCommandBuffer cb = g_vk.cmd_bufs[frame_idx];
    if (g_vk.ResetCommandBuffer) g_vk.ResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo cb_bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    g_vk.BeginCommandBuffer(cb, &cb_bi);

    VkImageMemoryBarrier barrier_dst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = g_vk.images[img_idx],
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };
    g_vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier_dst);

    VkBufferImageCopy copy_region = {
        .bufferOffset = 0,
        .bufferRowLength = g_vk.width,
        .bufferImageHeight = g_vk.height,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { g_vk.width, g_vk.height, 1 }
    };
    g_vk.CmdCopyBufferToImage(cb, g_vk.staging_buf, g_vk.images[img_idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

    VkImageMemoryBarrier barrier_present = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = g_vk.images[img_idx],
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };
    g_vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier_present);

    g_vk.EndCommandBuffer(cb);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &g_vk.sem_avail[frame_idx],
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &cb,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &g_vk.sem_finish[frame_idx]
    };
    static int logged_submit = 0;
    if (logged_submit < 5) {
        fprintf(stderr, "[dm] [vk-win] SUBMIT DETAILS #%d frame_idx=%u cb=%p sem_avail=%p sem_fin=%p fence=%p\n",
                logged_submit + 1, frame_idx, (void*)cb, (void*)g_vk.sem_avail[frame_idx], (void*)g_vk.sem_finish[frame_idx], (void*)g_vk.fences[frame_idx]);
    }
    VkResult qres = g_vk.QueueSubmit(g_vk.queue, 1, &submit_info, VK_NULL_HANDLE);
    VkResult widle = g_vk.DeviceWaitIdle(g_vk.device);
    if (qres != VK_SUCCESS) {
        static int logged_cfail = 0;
        if (logged_cfail++ < 20) {
            fprintf(stderr, "[dm] [RED] [vk-win] CPU fallback QueueSubmit FAILED cur_frame=%u frame_idx=%u res=%d\n",
                    g_vk.cur_frame, frame_idx, (int)qres);
        }
    }
    if (logged_submit < 5) {
        logged_submit++;
        fprintf(stderr, "[dm] [vk-win] QueueSubmit #%d res=%d (DeviceWaitIdle res=%d)\n",
                logged_submit, (int)qres, (int)widle);
    }

    if (g_vk.readback_mapped) {
        if (g_vk.InvalidateMappedMemoryRanges) {
            VkMappedMemoryRange range = {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = g_vk.readback_mem,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };
            g_vk.InvalidateMappedMemoryRanges(g_vk.device, 1, &range);
        }
        uint32_t *px = (uint32_t *)g_vk.readback_mapped;
        static int logged_px = 0;
        if (logged_px < 5) {
            logged_px++;
            fprintf(stderr, "[dm] [vk-win] GPU Readback Frame #%d pixels[0..3]=%#010x %#010x %#010x %#010x\n",
                    logged_px, px[0], px[1], px[2], px[3]);
        }
    }

    VkPresentInfoKHR pres_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &g_vk.sem_finish[frame_idx],
        .swapchainCount = 1,
        .pSwapchains = &g_vk.swapchain,
        .pImageIndices = &img_idx
    };
    g_vk.QueuePresentKHR(g_vk.queue, &pres_info);

    g_vk.cur_frame++;
}

void vk_win_shutdown(void) {
    if (!g_vk.h_dll) return;
    if (g_vk.device) {
        if (g_vk.DeviceWaitIdle) g_vk.DeviceWaitIdle(g_vk.device);

        for (int i = 0; i < g_vk_pipeline_count; i++) {
            if (g_vk_pipelines[i].pipeline && g_vk.DestroyPipeline)
                g_vk.DestroyPipeline(g_vk.device, g_vk_pipelines[i].pipeline, NULL);
            if (g_vk_pipelines[i].layout && g_vk.DestroyPipelineLayout)
                g_vk.DestroyPipelineLayout(g_vk.device, g_vk_pipelines[i].layout, NULL);
            if (g_vk_pipelines[i].desc_layout_vs && g_vk.DestroyDescriptorSetLayout)
                g_vk.DestroyDescriptorSetLayout(g_vk.device, g_vk_pipelines[i].desc_layout_vs, NULL);
            if (g_vk_pipelines[i].desc_layout_ps && g_vk.DestroyDescriptorSetLayout)
                g_vk.DestroyDescriptorSetLayout(g_vk.device, g_vk_pipelines[i].desc_layout_ps, NULL);
        }
        g_vk_pipeline_count = 0;

        if (g_vk.desc_pool && g_vk.DestroyDescriptorPool) g_vk.DestroyDescriptorPool(g_vk.device, g_vk.desc_pool, NULL);

        if (g_vk.vbuf_mapped && g_vk.UnmapMemory) g_vk.UnmapMemory(g_vk.device, g_vk.vbuf_mem);
        if (g_vk.vbuf && g_vk.DestroyBuffer) g_vk.DestroyBuffer(g_vk.device, g_vk.vbuf, NULL);
        if (g_vk.vbuf_mem && g_vk.FreeMemory) g_vk.FreeMemory(g_vk.device, g_vk.vbuf_mem, NULL);

        if (g_vk.dummy_sampler && g_vk.DestroySampler) g_vk.DestroySampler(g_vk.device, g_vk.dummy_sampler, NULL);
        if (g_vk.dummy_view && g_vk.DestroyImageView) g_vk.DestroyImageView(g_vk.device, g_vk.dummy_view, NULL);
        if (g_vk.dummy_img && g_vk.DestroyImage) g_vk.DestroyImage(g_vk.device, g_vk.dummy_img, NULL);
        if (g_vk.dummy_mem && g_vk.FreeMemory) g_vk.FreeMemory(g_vk.device, g_vk.dummy_mem, NULL);

        if (g_vk.dummy_storage_view && g_vk.DestroyImageView) g_vk.DestroyImageView(g_vk.device, g_vk.dummy_storage_view, NULL);
        if (g_vk.dummy_storage_img && g_vk.DestroyImage) g_vk.DestroyImage(g_vk.device, g_vk.dummy_storage_img, NULL);
        if (g_vk.dummy_storage_mem && g_vk.FreeMemory) g_vk.FreeMemory(g_vk.device, g_vk.dummy_storage_mem, NULL);

        if (g_vk.support_mapped && g_vk.UnmapMemory) g_vk.UnmapMemory(g_vk.device, g_vk.support_mem);
        if (g_vk.support_buf && g_vk.DestroyBuffer) g_vk.DestroyBuffer(g_vk.device, g_vk.support_buf, NULL);
        if (g_vk.support_mem && g_vk.FreeMemory) g_vk.FreeMemory(g_vk.device, g_vk.support_mem, NULL);

        if (g_vk.driver_mapped && g_vk.UnmapMemory) g_vk.UnmapMemory(g_vk.device, g_vk.driver_mem);
        if (g_vk.driver_buf && g_vk.DestroyBuffer) g_vk.DestroyBuffer(g_vk.device, g_vk.driver_buf, NULL);
        if (g_vk.driver_mem && g_vk.FreeMemory) g_vk.FreeMemory(g_vk.device, g_vk.driver_mem, NULL);

        if (g_vk.c1_mapped && g_vk.UnmapMemory) g_vk.UnmapMemory(g_vk.device, g_vk.c1_mem);
        if (g_vk.c1_buf && g_vk.DestroyBuffer) g_vk.DestroyBuffer(g_vk.device, g_vk.c1_buf, NULL);
        if (g_vk.c1_mem && g_vk.FreeMemory) g_vk.FreeMemory(g_vk.device, g_vk.c1_mem, NULL);

        if (g_vk.ubo_data_mapped && g_vk.UnmapMemory) g_vk.UnmapMemory(g_vk.device, g_vk.ubo_data_mem);
        if (g_vk.ubo_data_buf && g_vk.DestroyBuffer) g_vk.DestroyBuffer(g_vk.device, g_vk.ubo_data_buf, NULL);
        if (g_vk.ubo_data_mem && g_vk.FreeMemory) g_vk.FreeMemory(g_vk.device, g_vk.ubo_data_mem, NULL);

        if (g_vk.index_mapped && g_vk.UnmapMemory) g_vk.UnmapMemory(g_vk.device, g_vk.index_mem);
        if (g_vk.index_buf && g_vk.DestroyBuffer) g_vk.DestroyBuffer(g_vk.device, g_vk.index_buf, NULL);
        if (g_vk.index_mem && g_vk.FreeMemory) g_vk.FreeMemory(g_vk.device, g_vk.index_mem, NULL);

        for (uint32_t i = 0; i < g_vk.image_count; i++) {
            if (g_vk.framebuffers[i] && g_vk.DestroyFramebuffer) g_vk.DestroyFramebuffer(g_vk.device, g_vk.framebuffers[i], NULL);
            if (g_vk.image_views[i] && g_vk.DestroyImageView) g_vk.DestroyImageView(g_vk.device, g_vk.image_views[i], NULL);
        }

        if (g_vk.render_pass && g_vk.DestroyRenderPass) g_vk.DestroyRenderPass(g_vk.device, g_vk.render_pass, NULL);
        if (g_vk.render_pass_resume && g_vk.DestroyRenderPass) g_vk.DestroyRenderPass(g_vk.device, g_vk.render_pass_resume, NULL);

        if (g_vk.staging_mapped && g_vk.UnmapMemory) g_vk.UnmapMemory(g_vk.device, g_vk.staging_mem);
        if (g_vk.staging_buf && g_vk.DestroyBuffer) g_vk.DestroyBuffer(g_vk.device, g_vk.staging_buf, NULL);
        if (g_vk.staging_mem && g_vk.FreeMemory) g_vk.FreeMemory(g_vk.device, g_vk.staging_mem, NULL);

        if (g_vk.readback_mapped && g_vk.UnmapMemory) g_vk.UnmapMemory(g_vk.device, g_vk.readback_mem);
        if (g_vk.readback_buf && g_vk.DestroyBuffer) g_vk.DestroyBuffer(g_vk.device, g_vk.readback_buf, NULL);
        if (g_vk.readback_mem && g_vk.FreeMemory) g_vk.FreeMemory(g_vk.device, g_vk.readback_mem, NULL);

        for (uint32_t i = 0; i < g_vk.image_count; i++) {
            if (g_vk.sem_avail[i] && g_vk.DestroySemaphore) g_vk.DestroySemaphore(g_vk.device, g_vk.sem_avail[i], NULL);
            if (g_vk.sem_finish[i] && g_vk.DestroySemaphore) g_vk.DestroySemaphore(g_vk.device, g_vk.sem_finish[i], NULL);
            if (g_vk.fences[i] && g_vk.DestroyFence) g_vk.DestroyFence(g_vk.device, g_vk.fences[i], NULL);
        }

        for (int i = 0; i < g_vk_shader_count; i++) {
            if (g_vk_shaders[i].module && g_vk.DestroyShaderModule)
                g_vk.DestroyShaderModule(g_vk.device, g_vk_shaders[i].module, NULL);
        }
        g_vk_shader_count = 0;

        if (g_vk.cmd_pool && g_vk.DestroyCommandPool) g_vk.DestroyCommandPool(g_vk.device, g_vk.cmd_pool, NULL);
        if (g_vk.swapchain && g_vk.DestroySwapchainKHR) g_vk.DestroySwapchainKHR(g_vk.device, g_vk.swapchain, NULL);
        if (g_vk.DestroyDevice) g_vk.DestroyDevice(g_vk.device, NULL);
    }
    if (g_vk.surface && g_vk.DestroySurfaceKHR) g_vk.DestroySurfaceKHR(g_vk.instance, g_vk.surface, NULL);
    if (g_vk.instance && g_vk.DestroyInstance) g_vk.DestroyInstance(g_vk.instance, NULL);
    FreeLibrary(g_vk.h_dll);
    memset(&g_vk, 0, sizeof(g_vk));
}
