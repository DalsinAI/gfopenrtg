/* Stream3D GPU-path probe (Team 2, 25 Nov 2026): can this Vulkan driver draw
 * with a graphics pipeline into the RTG frame's formats, and what do many
 * small draws and the frame's trip to and from host memory cost?
 *   vkprobe [W H] [DRAWS] [FRAMES]   (VKPROBE_32=1: a 32-bit frame; VKPROBE_DEVICE=name)
 * Loads libvulkan.so.1 itself (no link-time Vulkan). Build where it runs:
 *   cc -O2 -o vkprobe vkprobe.c -ldl   (the shaders are in tri_*.h:
 *   glslangValidator -V --vn tri_vert -o tri_vert.h tri.vert, and frag)
 * On a card run it as the machine's user (sudo -u "#1001"), which is in the
 * render group; as another user Mesa falls back to llvmpipe without a word. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "tri_vert.h"
#include "tri_frag.h"

static PFN_vkGetInstanceProcAddr gipa;
#define F(n) static PFN_##n n
F(vkCreateInstance); F(vkEnumeratePhysicalDevices); F(vkGetPhysicalDeviceProperties);
F(vkGetPhysicalDeviceQueueFamilyProperties); F(vkCreateDevice); F(vkGetDeviceQueue);
F(vkGetPhysicalDeviceFormatProperties); F(vkGetPhysicalDeviceMemoryProperties);
F(vkEnumerateDeviceExtensionProperties); F(vkCreateImage); F(vkGetImageMemoryRequirements);
F(vkAllocateMemory); F(vkBindImageMemory); F(vkCreateImageView); F(vkCreateRenderPass);
F(vkCreateFramebuffer); F(vkCreateShaderModule); F(vkCreatePipelineLayout);
F(vkCreateGraphicsPipelines); F(vkCreateCommandPool); F(vkAllocateCommandBuffers);
F(vkBeginCommandBuffer); F(vkEndCommandBuffer); F(vkCmdBeginRenderPass); F(vkCmdEndRenderPass);
F(vkCmdBindPipeline); F(vkCmdBindVertexBuffers); F(vkCmdDraw); F(vkCmdCopyBufferToImage);
F(vkCmdCopyImageToBuffer); F(vkCmdPipelineBarrier); F(vkCreateBuffer);
F(vkGetBufferMemoryRequirements); F(vkBindBufferMemory); F(vkMapMemory); F(vkQueueSubmit);
F(vkCreateFence); F(vkWaitForFences); F(vkResetFences); F(vkResetCommandBuffer);
F(vkCmdPushConstants); F(vkCmdSetViewport); F(vkCmdSetScissor);

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
#define CK(x) do { VkResult r_ = (x); if (r_) { printf("FAIL %s: %d\n", #x, r_); exit(1); } } while (0)

static VkDevice dev; static VkPhysicalDevice phys; static VkPhysicalDeviceMemoryProperties mp;
static uint32_t memtype(uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    printf("FAIL no memory type\n"); exit(1);
}

static void fmt_line(const char *name, VkFormat f)
{
    VkFormatProperties p; vkGetPhysicalDeviceFormatProperties(phys, f, &p);
    printf("format %-10s optimal: attach %s blend %s | linear: attach %s blend %s\n", name,
           p.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT ? "yes" : "no ",
           p.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT ? "yes" : "no ",
           p.linearTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT ? "yes" : "no ",
           p.linearTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT ? "yes" : "no ");
}

int main(int argc, char **argv)
{
    int W = argc > 2 ? atoi(argv[1]) : 640, H = argc > 2 ? atoi(argv[2]) : 480;
    int DRAWS = argc > 3 ? atoi(argv[3]) : 576, FRAMES = argc > 4 ? atoi(argv[4]) : 200;
    void *lib = dlopen("libvulkan.so.1", RTLD_NOW);
    if (!lib) { printf("FAIL no libvulkan.so.1\n"); return 1; }
    gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
#define GI(n) n = (PFN_##n)gipa(inst, #n)
    VkInstance inst = 0;
    GI(vkCreateInstance);
    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO, 0, "vkprobe", 1, 0, 0, VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0, &ai };
    CK(vkCreateInstance(&ici, 0, &inst));
    GI(vkEnumeratePhysicalDevices); GI(vkGetPhysicalDeviceProperties); GI(vkGetPhysicalDeviceQueueFamilyProperties);
    GI(vkCreateDevice); GI(vkGetPhysicalDeviceFormatProperties); GI(vkGetPhysicalDeviceMemoryProperties);
    GI(vkEnumerateDeviceExtensionProperties);
    uint32_t n = 8; VkPhysicalDevice pd[8];
    CK(vkEnumeratePhysicalDevices(inst, &n, pd));
    const char *want = getenv("VKPROBE_DEVICE");
    phys = pd[0];
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(pd[i], &pp);
        printf("device %u: %s (type %d, api %u.%u)\n", i, pp.deviceName, pp.deviceType,
               VK_VERSION_MAJOR(pp.apiVersion), VK_VERSION_MINOR(pp.apiVersion));
        if (want && strstr(pp.deviceName, want)) phys = pd[i];
        else if (!want && pp.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU && phys == pd[0] && i) phys = pd[i];
    }
    VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(phys, &pp);
    printf("using: %s\n", pp.deviceName);
    fmt_line("RGB565", VK_FORMAT_R5G6B5_UNORM_PACK16);
    fmt_line("BGRA8", VK_FORMAT_B8G8R8A8_UNORM);
    fmt_line("RGBA8", VK_FORMAT_R8G8B8A8_UNORM);
    {
        uint32_t ne = 0; vkEnumerateDeviceExtensionProperties(phys, 0, &ne, 0);
        VkExtensionProperties *ep = calloc(ne, sizeof *ep); vkEnumerateDeviceExtensionProperties(phys, 0, &ne, ep);
        int host = 0, dma = 0;
        for (uint32_t i = 0; i < ne; i++) {
            if (!strcmp(ep[i].extensionName, "VK_EXT_external_memory_host")) host = 1;
            if (!strcmp(ep[i].extensionName, "VK_EXT_external_memory_dma_buf")) dma = 1;
        }
        printf("VK_EXT_external_memory_host %s, VK_EXT_external_memory_dma_buf %s\n", host ? "yes" : "no", dma ? "yes" : "no");
    }
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    uint32_t qn = 8; VkQueueFamilyProperties qf[8]; vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, qf);
    uint32_t q = 0; while (q < qn && !(qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) q++;
    float pri = 1;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, 0, 0, q, 1, &pri };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, 0, 0, 1, &qci };
    CK(vkCreateDevice(phys, &dci, 0, &dev));
    PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)gipa(inst, "vkGetDeviceProcAddr");
#define GD(x) x = (PFN_##x)gdpa(dev, #x)
    GD(vkGetDeviceQueue); GD(vkCreateImage); GD(vkGetImageMemoryRequirements); GD(vkAllocateMemory); GD(vkBindImageMemory);
    GD(vkCreateImageView); GD(vkCreateRenderPass); GD(vkCreateFramebuffer); GD(vkCreateShaderModule); GD(vkCreatePipelineLayout);
    GD(vkCreateGraphicsPipelines); GD(vkCreateCommandPool); GD(vkAllocateCommandBuffers); GD(vkBeginCommandBuffer);
    GD(vkEndCommandBuffer); GD(vkCmdBeginRenderPass); GD(vkCmdEndRenderPass); GD(vkCmdBindPipeline); GD(vkCmdBindVertexBuffers);
    GD(vkCmdDraw); GD(vkCmdCopyBufferToImage); GD(vkCmdCopyImageToBuffer); GD(vkCmdPipelineBarrier); GD(vkCreateBuffer);
    GD(vkGetBufferMemoryRequirements); GD(vkBindBufferMemory); GD(vkMapMemory); GD(vkQueueSubmit); GD(vkCreateFence);
    GD(vkWaitForFences); GD(vkResetFences); GD(vkResetCommandBuffer); GD(vkCmdPushConstants); GD(vkCmdSetViewport); GD(vkCmdSetScissor);
    VkQueue queue; vkGetDeviceQueue(dev, q, 0, &queue);

    VkFormat fmt = getenv("VKPROBE_32") ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R5G6B5_UNORM_PACK16;
    int bpp = fmt == VK_FORMAT_B8G8R8A8_UNORM ? 4 : 2;
    /* the target: an optimal image; the frame in host memory is a buffer, copied in and out */
    VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, 0, 0, VK_IMAGE_TYPE_2D, fmt, { W, H, 1 }, 1, 1,
        VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT };
    VkImage img; CK(vkCreateImage(dev, &ii, 0, &img));
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 0, mr.size, memtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    VkDeviceMemory imem; CK(vkAllocateMemory(dev, &mai, 0, &imem)); CK(vkBindImageMemory(dev, img, imem, 0));
    VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, 0, 0, img, VK_IMAGE_VIEW_TYPE_2D, fmt, {0},
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VkImageView view; CK(vkCreateImageView(dev, &vci, 0, &view));
    /* host buffers: the frame and the vertices */
    VkBuffer fb, vb; VkDeviceMemory fbm, vbm; void *fbp, *vbp;
    size_t fbytes = (size_t)W * H * bpp, vbytes = (size_t)DRAWS * 6 * 6 * 4;
    {
        VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 0, 0, fbytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT };
        CK(vkCreateBuffer(dev, &bi, 0, &fb)); vkGetBufferMemoryRequirements(dev, fb, &mr);
        mai.allocationSize = mr.size; mai.memoryTypeIndex = memtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        CK(vkAllocateMemory(dev, &mai, 0, &fbm)); CK(vkBindBufferMemory(dev, fb, fbm, 0)); CK(vkMapMemory(dev, fbm, 0, fbytes, 0, &fbp));
        bi.size = vbytes; bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        CK(vkCreateBuffer(dev, &bi, 0, &vb)); vkGetBufferMemoryRequirements(dev, vb, &mr);
        mai.allocationSize = mr.size; mai.memoryTypeIndex = memtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        CK(vkAllocateMemory(dev, &mai, 0, &vbm)); CK(vkBindBufferMemory(dev, vb, vbm, 0)); CK(vkMapMemory(dev, vbm, 0, vbytes, 0, &vbp));
    }
    VkAttachmentDescription ad = { 0, fmt, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference ar = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sd = { 0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 0, 1, &ar };
    VkRenderPassCreateInfo rpi = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, 0, 0, 1, &ad, 1, &sd };
    VkRenderPass rp; CK(vkCreateRenderPass(dev, &rpi, 0, &rp));
    VkFramebufferCreateInfo fbi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, 0, 0, rp, 1, &view, W, H, 1 };
    VkFramebuffer fbo; CK(vkCreateFramebuffer(dev, &fbi, 0, &fbo));
    VkShaderModuleCreateInfo smi = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, 0, 0, sizeof tri_vert, tri_vert };
    VkShaderModule vs, fs; CK(vkCreateShaderModule(dev, &smi, 0, &vs));
    smi.codeSize = sizeof tri_frag; smi.pCode = tri_frag; CK(vkCreateShaderModule(dev, &smi, 0, &fs));
    VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT, 0, 8 };
    VkPipelineLayoutCreateInfo pli = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, 0, 0, 0, 0, 1, &pcr };
    VkPipelineLayout pl; CK(vkCreatePipelineLayout(dev, &pli, 0, &pl));
    VkPipelineShaderStageCreateInfo st[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main" } };
    VkVertexInputBindingDescription vbd = { 0, 24, VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription vad[2] = { { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8 } };
    VkPipelineVertexInputStateCreateInfo vis = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, 0, 0, 1, &vbd, 2, vad };
    VkPipelineInputAssemblyStateCreateInfo ias = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, 0, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkViewport vp = { 0, 0, W, H, 0, 1 }; VkRect2D sc = { {0,0}, {W, H} };
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, 0, 0, 1, &vp, 1, &sc };
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, 0, 0, 0, 0,
        VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0, 0, 0, 0, 1 };
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, 0, 0, VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState cba = { 1, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, 0xF };
    VkPipelineColorBlendStateCreateInfo cbs = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, 0, 0, 0, 0, 1, &cba };
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dys = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, 0, 0, 2, dyn };
    VkGraphicsPipelineCreateInfo gpi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, 0, 0, 2, st, &vis, &ias, 0, &vps, &rs, &ms, 0, &cbs, &dys, pl, rp, 0 };
    VkPipeline pipe; double t0 = now(); CK(vkCreateGraphicsPipelines(dev, 0, 1, &gpi, 0, &pipe));
    printf("pipeline made in %.1f ms\n", (now() - t0) * 1e3);
    VkCommandPoolCreateInfo cpi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, 0, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, q };
    VkCommandPool pool; CK(vkCreateCommandPool(dev, &cpi, 0, &pool));
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, 0, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
    VkCommandBuffer cb; CK(vkAllocateCommandBuffers(dev, &cai, &cb));
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO }; VkFence fence; CK(vkCreateFence(dev, &fci, 0, &fence));
    /* vertices: DRAWS quads of 2 triangles each over the frame (SMALL's shape) */
    float *v = vbp;
    for (int d = 0; d < DRAWS; d++) {
        float x = (float)(d % 24) * W / 24, y = (float)(d / 24 % 24) * H / 24, w = (float)W / 24, h = (float)H / 24;
        float c[4] = { (d * 37 % 255) / 255.f, (d * 91 % 255) / 255.f, (d * 53 % 255) / 255.f, 1 };
        float q6[6][2] = { {x,y},{x+w,y},{x,y+h},{x+w,y},{x+w,y+h},{x,y+h} };
        for (int k = 0; k < 6; k++) { *v++ = q6[k][0]; *v++ = q6[k][1]; memcpy(v, c, 16); v += 4; }
    }
    memset(fbp, 0x40, fbytes);
    VkBufferImageCopy bic = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, {0,0,0}, { W, H, 1 } };
    double tsub = 0, twait = 0; int first = 1;
    for (int mode = 0; mode < 2; mode++) {     /* 0: one draw call per quad; 1: one draw for the whole frame */
        double ts = now();
        for (int f = 0; f < FRAMES; f++) {
            VkCommandBufferBeginInfo bbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 0, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
            CK(vkResetCommandBuffer(cb, 0)); CK(vkBeginCommandBuffer(cb, &bbi));
            VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, 0, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, img, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
            first = 0;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &b);
            vkCmdCopyBufferToImage(cb, fb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);   /* the frame up */
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, 0, 0, 0, 1, &b);
            VkRenderPassBeginInfo rbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, 0, rp, fbo, { {0,0}, {W,H} } };
            vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            vkCmdSetViewport(cb, 0, 1, &vp);
            float inv[2] = { 2.f / W, 2.f / H };
            vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, inv);
            VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off);
            if (mode == 0)
                for (int d = 0; d < DRAWS; d++) {   /* a scissor change per draw: the state a Warp3D draw may bring */
                    VkRect2D s = { {0,0}, {W - (d & 1), H} };
                    vkCmdSetScissor(cb, 0, 1, &s);
                    vkCmdDraw(cb, 6, 1, d * 6, 0);
                }
            else { vkCmdSetScissor(cb, 0, 1, &sc); vkCmdDraw(cb, DRAWS * 6, 1, 0, 0); }
            vkCmdEndRenderPass(cb);
            b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &b);
            vkCmdCopyImageToBuffer(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, fb, 1, &bic);   /* the frame back */
            CK(vkEndCommandBuffer(cb));
            VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, 0, 0, 0, 0, 1, &cb };
            double a = now();
            CK(vkQueueSubmit(queue, 1, &si, fence));
            double bnow = now();
            CK(vkWaitForFences(dev, 1, &fence, 1, 5000000000ull)); CK(vkResetFences(dev, 1, &fence));
            tsub += bnow - a; twait += now() - bnow;
        }
        double el = now() - ts;
        printf("%s: %d frames %dx%d, %d quads each: %.1f fps (%.2f ms a frame: submit %.3f, wait %.3f)\n",
               mode ? "one draw a frame " : "one draw per quad", FRAMES, W, H, DRAWS, FRAMES / el, el * 1e3 / FRAMES,
               tsub * 1e3 / FRAMES, twait * 1e3 / FRAMES);
        tsub = twait = 0;
    }
    /* a pixel check: the first quad's centre has its colour */
    {
        int x = W / 48, y = H / 48;
        /* quad 0's colour is (0, 0, 0, 1): black, so 0000 (16-bit) or ff000000 (32-bit) */
        if (bpp == 2) { unsigned short p = ((unsigned short *)fbp)[y * W + x]; printf("pixel (%d,%d) = %04x (want 0000)\n", x, y, p); }
        else { unsigned p = ((unsigned *)fbp)[y * W + x]; printf("pixel (%d,%d) = %08x (want ff000000)\n", x, y, p); }
    }
    printf("OK\n");
    return 0;
}
