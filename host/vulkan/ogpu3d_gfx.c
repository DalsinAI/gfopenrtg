/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Stream3D's TRIANGLES on the GPU's own graphics pipeline: see
 * ogpu3d_gfx.h. Built into ogpu_vk.c (#included, as ogpu3d_rec.c is). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ogpu3d_gfx.h"
#include "ogpu3d_gfx_spv.h"     /* ogpu3d_gfx_vert_spv[], ogpu3d_gfx_frag_spv[] (build_spv.sh gfx) */

#define GF_VBUF     (4UL << 20)     /* a group's vertices; a full buffer closes the group */
#define GF_TEXN     256             /* textures kept */
#define GF_TEXMEM   (64UL << 20)    /* and their bytes, at most */
#define GF_PIPES    96
#define GF_SAMPLERS 48
#define GF_SETS     4               /* descriptor sets a texture keeps, one a sampler */

/* The fragment shader's flags (ogpu3d_gfx.frag) */
#define GF_TEX 1
#define GF_GOURAUD 2
#define GF_SPEC 4
#define GF_Q565 8

/* Target formats as images: 0 B8G8R8A8, 1 R8G8B8A8, 2 R5G6B5; Z: 0 none, 1 D16, 2 D32 */
static const VkFormat gf_cfmt[3] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R5G6B5_UNORM_PACK16 };
static const VkFormat gf_dfmt[3] = { VK_FORMAT_UNDEFINED, VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT };

struct gf_vtx {                     /* ogpu3d_gfx.vert's inputs: 40 bytes */
    float pos[4];                   /* x, y (pixels), z (0..1), q */
    float tex[4];                   /* u, v, lod, fog w */
    ogpu_u8 col[4], spec[4];        /* RGBA; specular RGB and the vertex fog factor */
};

struct gf_push {                    /* the shaders' push constants: 80 bytes */
    float scale[2];
    int32_t flags, aref;
    int32_t cur[4], envc[4], fogc[4];
    float fogp[4];
};

struct gf_img {
    VkImage img;
    VkDeviceMemory mem;
    VkImageView view;
    int w, h, levels;
    VkFormat fmt;
};

struct gf_tex {
    int used;
    const ogpu_u8 *level0, *table;
    long bpr0;
    int w, h, levels, format;
    uint64_t hash;
    unsigned long checked;          /* the run it was last checked in */
    unsigned long group;            /* the group it was last used in */
    unsigned long last;             /* for the least recently used */
    size_t bytes;
    struct gf_img im;
    VkSampler smp[GF_SETS];
    VkDescriptorSet set[GF_SETS];
    int nsets;
};

struct ogpu3d_gfx {
    struct ogpu3d_gfx_cfg cfg;
    VkPhysicalDeviceMemoryProperties mp;
    uint32_t max_dim;
    VkShaderModule vs, fs;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout layout;
    VkDescriptorPool dpool;
    VkRenderPass rp[3][3];
    VkCommandPool pool;
    VkCommandBuffer cb, tcb;
    VkFence fence, tfence;
    VkBuffer vbuf, sbuf, tsbuf;
    VkDeviceMemory vmem, smem, tsmem;
    ogpu_u8 *vmap, *smap, *tsmap;
    size_t vused, ssize, sused, tssize;
    int scoherent;                  /* staging is coherent: else flushed and invalidated */
    ogpu_u8 *bounce;                /* two rows of cached memory: rows pass through it (gf_row_in, gf_row_out) */
    size_t bounce_size;
    struct gf_img col, dep;
    VkFramebuffer fb;
    VkImageView fb_c, fb_d;
    struct { uint32_t key; VkPipeline p; } pipes[GF_PIPES];
    int npipes;
    struct { uint32_t key; VkSampler s; } samplers[GF_SAMPLERS];
    int nsamplers;
    struct gf_tex tex[GF_TEXN], dummy;
    size_t tex_bytes;
    unsigned long run, groupno, clock;
    int failed;
    /* the open group */
    int open, in_pass, ci, di;
    VkPipeline bound;
    VkDescriptorSet bound_set;
    ogpu_u8 *dst; long dbpr; int dw, dh, dfmt;
    ogpu_u8 *zmem; long zbpr; int zw, zh, zfmt;
    int uy0, uy1;                   /* rows copied in */
    /* what the sink's begin took */
    int cap, ax0, ay0, ax1, ay1;
    struct o3_pipe pp;
    struct ogpu3d_tex tx;
    struct ogpu3d_gfx_stats st;
};

/* ---- small helpers ------------------------------------------------------------------- */

static uint32_t gf_rd32(const ogpu_u8 *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static double gf_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static float gf_float(uint32_t bits) { float f; memcpy(&f, &bits, 4); return f; }

/* log2 of x > 0 to about 1e-6, without libm */
static double gf_log2(double x) {
    int e = 0;
    double y, y2, s;
    while (x >= 2) { x *= 0.5; e++; }
    while (x < 1) { x *= 2; e--; }
    y = (x - 1) / (x + 1);
    y2 = y * y;
    s = y * (1 + y2 * (1.0 / 3 + y2 * (1.0 / 5 + y2 * (1.0 / 7 + y2 * (1.0 / 9 + y2 / 11)))));
    return e + 2 * s * 1.4426950408889634;
}

static int gf_memtype(const struct ogpu3d_gfx *g, uint32_t bits, VkMemoryPropertyFlags want) {
    uint32_t i;
    for (i = 0; i < g->mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (g->mp.memoryTypes[i].propertyFlags & want) == want) return (int)i;
    return -1;
}

static int gf_buffer(struct ogpu3d_gfx *g, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem,
                     ogpu_u8 **map, int *coherent) {
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryRequirements req;
    const VkMemoryPropertyFlags hv = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    void *m;
    int type;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(g->cfg.dev, &bi, 0, buf) != VK_SUCCESS) { *buf = 0; return 0; }
    vkGetBufferMemoryRequirements(g->cfg.dev, *buf, &req);
    /* The CPU reads the rows back from staging: cached memory where there is
     * some, coherent or not (V3D's coherent memory is uncached, and reading a
     * frame back from it took 30 ms on a Pi 5) */
    type = coherent ? gf_memtype(g, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT) : -1;
    if (type < 0) type = gf_memtype(g, req.memoryTypeBits, hv | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (type < 0) type = gf_memtype(g, req.memoryTypeBits, hv);
    if (coherent) *coherent = type >= 0 && (g->mp.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    if (type < 0) return 0;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = (uint32_t)type;
    if (vkAllocateMemory(g->cfg.dev, &ai, 0, mem) != VK_SUCCESS) { *mem = 0; return 0; }
    if (vkBindBufferMemory(g->cfg.dev, *buf, *mem, 0) != VK_SUCCESS) return 0;
    if (vkMapMemory(g->cfg.dev, *mem, 0, VK_WHOLE_SIZE, 0, &m) != VK_SUCCESS) return 0;
    *map = m;
    return 1;
}

static void gf_buffer_free(struct ogpu3d_gfx *g, VkBuffer *buf, VkDeviceMemory *mem, ogpu_u8 **map) {
    if (*buf) vkDestroyBuffer(g->cfg.dev, *buf, 0);
    if (*mem) vkFreeMemory(g->cfg.dev, *mem, 0);
    *buf = 0; *mem = 0; *map = 0;
}

static void gf_img_free(struct ogpu3d_gfx *g, struct gf_img *im) {
    if (im->view) vkDestroyImageView(g->cfg.dev, im->view, 0);
    if (im->img) vkDestroyImage(g->cfg.dev, im->img, 0);
    if (im->mem) vkFreeMemory(g->cfg.dev, im->mem, 0);
    memset(im, 0, sizeof *im);
}

static int gf_img(struct ogpu3d_gfx *g, struct gf_img *im, int w, int h, int levels, VkFormat fmt, VkImageUsageFlags usage,
                  VkImageAspectFlags aspect) {
    VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VkImageViewCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryRequirements req;
    int type;
    memset(im, 0, sizeof *im);
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = fmt;
    ii.extent.width = (uint32_t)w; ii.extent.height = (uint32_t)h; ii.extent.depth = 1;
    ii.mipLevels = (uint32_t)levels;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(g->cfg.dev, &ii, 0, &im->img) != VK_SUCCESS) { im->img = 0; return 0; }
    vkGetImageMemoryRequirements(g->cfg.dev, im->img, &req);
    type = gf_memtype(g, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type < 0) type = gf_memtype(g, req.memoryTypeBits, 0);
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = (uint32_t)type;
    if (type < 0 || vkAllocateMemory(g->cfg.dev, &ai, 0, &im->mem) != VK_SUCCESS) { im->mem = 0; gf_img_free(g, im); return 0; }
    if (vkBindImageMemory(g->cfg.dev, im->img, im->mem, 0) != VK_SUCCESS) { gf_img_free(g, im); return 0; }
    vi.image = im->img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = aspect;
    vi.subresourceRange.levelCount = (uint32_t)levels;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(g->cfg.dev, &vi, 0, &im->view) != VK_SUCCESS) { im->view = 0; gf_img_free(g, im); return 0; }
    im->w = w; im->h = h; im->levels = levels; im->fmt = fmt;
    return 1;
}

static void gf_barrier(VkCommandBuffer cb, VkImage img, VkImageAspectFlags aspect, int levels, VkImageLayout from, VkImageLayout to,
                       VkAccessFlags sa, VkAccessFlags da, VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
    VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.srcAccessMask = sa; b.dstAccessMask = da;
    b.oldLayout = from; b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.levelCount = (uint32_t)levels;
    b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, ss, ds, 0, 0, 0, 0, 0, 1, &b);
}

static int gf_wait(struct ogpu3d_gfx *g, VkFence *f) {
    VkResult r = vkWaitForFences(g->cfg.dev, 1, f, VK_TRUE, g->cfg.wait_ns ? g->cfg.wait_ns : 20000000000ULL);
    if (r != VK_SUCCESS) { g->failed = 1; return 0; }
    vkResetFences(g->cfg.dev, 1, f);
    return 1;
}

/* ---- set up ------------------------------------------------------------------------- */

static int gf_format_ok(VkPhysicalDevice phys, VkFormat f, VkFormatFeatureFlags want) {
    VkFormatProperties p;
    vkGetPhysicalDeviceFormatProperties(phys, f, &p);
    return (p.optimalTilingFeatures & want) == want;
}

static VkRenderPass gf_render_pass(struct ogpu3d_gfx *g, int ci, int di) {
    VkAttachmentDescription at[2];
    VkAttachmentReference cr = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference dr = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp;
    VkRenderPassCreateInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    VkRenderPass rp;
    if (g->rp[ci][di]) return g->rp[ci][di];
    memset(at, 0, sizeof at);
    memset(&sp, 0, sizeof sp);
    at[0].format = gf_cfmt[ci];
    at[0].samples = VK_SAMPLE_COUNT_1_BIT;
    at[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    at[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    at[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    at[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[0].initialLayout = at[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    at[1] = at[0];
    at[1].format = gf_dfmt[di];
    at[1].initialLayout = at[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &cr;
    if (di) sp.pDepthStencilAttachment = &dr;
    ri.attachmentCount = di ? 2 : 1;
    ri.pAttachments = at;
    ri.subpassCount = 1;
    ri.pSubpasses = &sp;
    if (vkCreateRenderPass(g->cfg.dev, &ri, 0, &rp) != VK_SUCCESS) return 0;
    g->rp[ci][di] = rp;
    return rp;
}

void ogpu3d_gfx_destroy(struct ogpu3d_gfx *g) {
    int i, j;
    VkDevice d;
    if (!g) return;
    d = g->cfg.dev;
    vkDeviceWaitIdle(d);
    for (i = 0; i < g->npipes; i++) vkDestroyPipeline(d, g->pipes[i].p, 0);
    for (i = 0; i < g->nsamplers; i++) vkDestroySampler(d, g->samplers[i].s, 0);
    for (i = 0; i < GF_TEXN; i++) if (g->tex[i].used) gf_img_free(g, &g->tex[i].im);
    gf_img_free(g, &g->dummy.im);
    gf_img_free(g, &g->col);
    gf_img_free(g, &g->dep);
    if (g->fb) vkDestroyFramebuffer(d, g->fb, 0);
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) if (g->rp[i][j]) vkDestroyRenderPass(d, g->rp[i][j], 0);
    gf_buffer_free(g, &g->vbuf, &g->vmem, &g->vmap);
    gf_buffer_free(g, &g->sbuf, &g->smem, &g->smap);
    gf_buffer_free(g, &g->tsbuf, &g->tsmem, &g->tsmap);
    free(g->bounce);
    if (g->fence) vkDestroyFence(d, g->fence, 0);
    if (g->tfence) vkDestroyFence(d, g->tfence, 0);
    if (g->pool) vkDestroyCommandPool(d, g->pool, 0);
    if (g->dpool) vkDestroyDescriptorPool(d, g->dpool, 0);
    if (g->layout) vkDestroyPipelineLayout(d, g->layout, 0);
    if (g->dsl) vkDestroyDescriptorSetLayout(d, g->dsl, 0);
    if (g->vs) vkDestroyShaderModule(d, g->vs, 0);
    if (g->fs) vkDestroyShaderModule(d, g->fs, 0);
    free(g);
}

static int gf_why(char *why, int whylen, const char *s) {
    if (why && whylen > 0) { strncpy(why, s, (size_t)whylen - 1); why[whylen - 1] = 0; }
    return 0;
}

static VkDescriptorSet gf_tex_set(struct ogpu3d_gfx *g, struct gf_tex *e, VkSampler s);

struct ogpu3d_gfx *ogpu3d_gfx_create(const struct ogpu3d_gfx_cfg *cfg, char *why, int whylen) {
    struct ogpu3d_gfx *g = calloc(1, sizeof *g);
    VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkDescriptorSetLayoutBinding b;
    VkDescriptorSetLayoutCreateInfo dli = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkPushConstantRange pcr;
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkDescriptorPoolSize ps;
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkPhysicalDeviceProperties pr;
    VkCommandBuffer cbs[2];
    const VkFormatFeatureFlags cf = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT
                                    | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    const VkFormatFeatureFlags df = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT
                                    | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    int i;
    if (!g) { gf_why(why, whylen, "no memory"); return 0; }
    g->cfg = *cfg;
    vkGetPhysicalDeviceMemoryProperties(cfg->phys, &g->mp);
    vkGetPhysicalDeviceProperties(cfg->phys, &pr);
    g->max_dim = pr.limits.maxImageDimension2D;
    if (pr.limits.maxViewportDimensions[0] < g->max_dim) g->max_dim = pr.limits.maxViewportDimensions[0];
    if (pr.limits.maxViewportDimensions[1] < g->max_dim) g->max_dim = pr.limits.maxViewportDimensions[1];
    if (pr.limits.maxFramebufferWidth < g->max_dim) g->max_dim = pr.limits.maxFramebufferWidth;
    if (pr.limits.maxFramebufferHeight < g->max_dim) g->max_dim = pr.limits.maxFramebufferHeight;
    for (i = 0; i < 3; i++)
        if (!gf_format_ok(cfg->phys, gf_cfmt[i], cf)) { gf_why(why, whylen, "a target format can't be drawn into"); goto bad; }
    if (!gf_format_ok(cfg->phys, gf_dfmt[1], df) || !gf_format_ok(cfg->phys, gf_dfmt[2], df)) {
        gf_why(why, whylen, "no D16 or D32 depth images");
        goto bad;
    }
    if (!gf_format_ok(cfg->phys, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        gf_why(why, whylen, "no filtered RGBA8 textures");
        goto bad;
    }
    smi.codeSize = sizeof ogpu3d_gfx_vert_spv;
    smi.pCode = ogpu3d_gfx_vert_spv;
    if (vkCreateShaderModule(cfg->dev, &smi, 0, &g->vs) != VK_SUCCESS) goto vkbad;
    smi.codeSize = sizeof ogpu3d_gfx_frag_spv;
    smi.pCode = ogpu3d_gfx_frag_spv;
    if (vkCreateShaderModule(cfg->dev, &smi, 0, &g->fs) != VK_SUCCESS) goto vkbad;
    memset(&b, 0, sizeof b);
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    dli.bindingCount = 1;
    dli.pBindings = &b;
    if (vkCreateDescriptorSetLayout(cfg->dev, &dli, 0, &g->dsl) != VK_SUCCESS) goto vkbad;
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(struct gf_push);
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &g->dsl;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(cfg->dev, &pli, 0, &g->layout) != VK_SUCCESS) goto vkbad;
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = (GF_TEXN + 1) * GF_SETS;
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpi.maxSets = (GF_TEXN + 1) * GF_SETS;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(cfg->dev, &dpi, 0, &g->dpool) != VK_SUCCESS) goto vkbad;
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = cfg->qfam;
    if (vkCreateCommandPool(cfg->dev, &cpi, 0, &g->pool) != VK_SUCCESS) goto vkbad;
    cai.commandPool = g->pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 2;
    if (vkAllocateCommandBuffers(cfg->dev, &cai, cbs) != VK_SUCCESS) goto vkbad;
    g->cb = cbs[0]; g->tcb = cbs[1];
    if (vkCreateFence(cfg->dev, &fi, 0, &g->fence) != VK_SUCCESS || vkCreateFence(cfg->dev, &fi, 0, &g->tfence) != VK_SUCCESS)
        goto vkbad;
    if (!gf_buffer(g, GF_VBUF, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &g->vbuf, &g->vmem, &g->vmap, 0)) goto vkbad;
    /* the texture for untextured triangles (the shader's set is always bound) */
    if (!gf_img(g, &g->dummy.im, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT))
        goto vkbad;
    {
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkSamplerCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        VkSampler s;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(g->tcb, &bi);
        gf_barrier(g->tcb, g->dummy.im.img, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        vkEndCommandBuffer(g->tcb);
        si.commandBufferCount = 1;
        si.pCommandBuffers = &g->tcb;
        if (vkQueueSubmit(cfg->queue, 1, &si, g->tfence) != VK_SUCCESS || !gf_wait(g, &g->tfence)) goto vkbad;
        sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod = 0.25f;
        if (vkCreateSampler(cfg->dev, &sci, 0, &s) != VK_SUCCESS) goto vkbad;
        g->samplers[0].key = 0xFFFFFFFFu;
        g->samplers[0].s = s;
        g->nsamplers = 1;
        if (!gf_tex_set(g, &g->dummy, s)) goto vkbad;
    }
    return g;
vkbad:
    gf_why(why, whylen, "a Vulkan object couldn't be made");
bad:
    ogpu3d_gfx_destroy(g);
    return 0;
}

/* ---- pipelines and samplers ------------------------------------------------------------ */

static VkBlendFactor gf_factor(int f) {
    switch (f) {
    case 1: return VK_BLEND_FACTOR_ZERO;
    case 3: return VK_BLEND_FACTOR_SRC_COLOR;
    case 4: return VK_BLEND_FACTOR_DST_COLOR;
    case 5: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 6: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 7: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 8: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 9: return VK_BLEND_FACTOR_DST_ALPHA;
    case 10: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 11: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case 12: return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case 13: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case 14: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case 15: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;     /* ONE, as the core reads anything else */
}

static VkCompareOp gf_compare(int f) {
    switch (f) {
    case 1: return VK_COMPARE_OP_NEVER;
    case 2: return VK_COMPARE_OP_LESS;
    case 3: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case 4: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case 5: return VK_COMPARE_OP_GREATER;
    case 6: return VK_COMPARE_OP_NOT_EQUAL;
    case 7: return VK_COMPARE_OP_EQUAL;
    }
    return VK_COMPARE_OP_ALWAYS;
}

/* key: colour format 2 bits, Z 2, blend 1, source 4, destination 4, Z test 1, function 4, write 1, mask 4 */
static VkPipeline gf_pipeline(struct ogpu3d_gfx *g, uint32_t key) {
    VkPipelineShaderStageCreateInfo st[2];
    VkVertexInputBindingDescription vb = { 0, sizeof(struct gf_vtx), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription va[4] = {
        { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
        { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16 },
        { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, 32 },
        { 3, 0, VK_FORMAT_R8G8B8A8_UNORM, 36 },
    };
    VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    VkPipelineViewportStateCreateInfo vp = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    VkPipelineDepthStencilStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState ba;
    VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkDynamicState dyn[3] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS };
    VkPipelineDynamicStateCreateInfo dy = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    VkGraphicsPipelineCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    VkPipeline p;
    int ci = (int)(key & 3), di = (int)((key >> 2) & 3), i;
    for (i = 0; i < g->npipes; i++) if (g->pipes[i].key == key) return g->pipes[i].p;
    if (g->npipes == GF_PIPES) return 0;
    memset(st, 0, sizeof st);
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = g->vs; st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = g->fs; st[1].pName = "main";
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 4; vi.pVertexAttributeDescriptions = va;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    vp.viewportCount = vp.scissorCount = 1;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    ds.depthTestEnable = di && ((key >> 15) & 1);
    ds.depthWriteEnable = ds.depthTestEnable && ((key >> 20) & 1);
    ds.depthCompareOp = gf_compare((int)((key >> 16) & 15));
    memset(&ba, 0, sizeof ba);
    ba.blendEnable = (key >> 4) & 1;
    ba.srcColorBlendFactor = ba.srcAlphaBlendFactor = gf_factor((int)((key >> 5) & 15));
    ba.dstColorBlendFactor = ba.dstAlphaBlendFactor = gf_factor((int)((key >> 9) & 15));
    ba.colorBlendOp = ba.alphaBlendOp = VK_BLEND_OP_ADD;
    ba.colorWriteMask = (key >> 21) & 15;
    cb.attachmentCount = 1; cb.pAttachments = &ba;
    dy.dynamicStateCount = 3; dy.pDynamicStates = dyn;
    pi.stageCount = 2; pi.pStages = st;
    pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia; pi.pViewportState = &vp;
    pi.pRasterizationState = &rs; pi.pMultisampleState = &ms; pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb; pi.pDynamicState = &dy;
    pi.layout = g->layout;
    pi.renderPass = gf_render_pass(g, ci, di);
    if (!pi.renderPass || vkCreateGraphicsPipelines(g->cfg.dev, VK_NULL_HANDLE, 1, &pi, 0, &p) != VK_SUCCESS) return 0;
    g->pipes[g->npipes].key = key;
    g->pipes[g->npipes++].p = p;
    g->st.pipelines++;
    return p;
}

static VkSamplerAddressMode gf_wrap(int w) {
    return w == OGPU_WRAP_REPEAT ? VK_SAMPLER_ADDRESS_MODE_REPEAT
         : w == OGPU_WRAP_BORDER ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

/* The border colours Vulkan has without an extension (ARGB32): -1 for any other. */
static int gf_border(uint32_t argb) {
    return argb == 0 ? VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK : argb == 0xFF000000u ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK
         : argb == 0xFFFFFFFFu ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE : -1;
}

/* Warp3D's filters (1 NEAREST, 2 LINEAR, 3-6 the mip ones) as the core
 * reads them: the level of detail is the core's, once a triangle, given to
 * textureLod; minification picks the filter as the core does. */
static VkSampler gf_sampler(struct ogpu3d_gfx *g, const struct ogpu3d_tex *tx) {
    VkSamplerCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    int minf = tx->min, mip = minf >= 3 && minf <= 6 && tx->levels > 1, b = gf_border(tx->border);
    uint32_t key = (uint32_t)(minf & 15) | (uint32_t)(tx->mag & 15) << 4 | (uint32_t)(tx->wrap_s & 3) << 8
                 | (uint32_t)(tx->wrap_t & 3) << 10 | (uint32_t)(b & 3) << 12 | (uint32_t)(mip ? tx->levels : 0) << 16;
    VkSampler s;
    int i;
    for (i = 0; i < g->nsamplers; i++) if (g->samplers[i].key == key) return g->samplers[i].s;
    if (g->nsamplers == GF_SAMPLERS) return 0;
    sci.magFilter = tx->mag == 2 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sci.minFilter = minf == 2 || minf == 5 || minf == 6 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sci.mipmapMode = minf == 4 || minf == 6 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = gf_wrap(tx->wrap_s);
    sci.addressModeV = gf_wrap(tx->wrap_t);
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.borderColor = b < 0 ? VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK : (VkBorderColor)b;
    sci.minLod = 0;
    /* no mip filter: level 0 whatever the detail (0.25: minified still picks minFilter) */
    sci.maxLod = mip ? (float)(tx->levels - 1) : 0.25f;
    if (vkCreateSampler(g->cfg.dev, &sci, 0, &s) != VK_SUCCESS) return 0;
    g->samplers[g->nsamplers].key = key;
    g->samplers[g->nsamplers++].s = s;
    return s;
}

/* ---- textures ------------------------------------------------------------------------- */

/* One texel as ogpu_3d.c's o3_texel reads it, as R, G, B, A bytes. */
static void gf_texel(const struct ogpu3d_tex *tx, const ogpu_u8 *p, ogpu_u8 *o) {
    int r, g, b, a = 255, hi, lo;
    switch (tx->format) {
    case OGPU_FMT_ARGB32: a = p[0]; r = p[1]; g = p[2]; b = p[3]; break;
    case OGPU_FMT_XRGB32: r = p[1]; g = p[2]; b = p[3]; break;
    case OGPU_FMT_BGRA32: b = p[0]; g = p[1]; r = p[2]; a = p[3]; break;
    case OGPU_FMT_RGBA32: r = p[0]; g = p[1]; b = p[2]; a = p[3]; break;
    case OGPU_FMT_ABGR32: a = p[0]; b = p[1]; g = p[2]; r = p[3]; break;
    case OGPU_FMT_RGB565: case OGPU_FMT_RGB565PC: case OGPU_FMT_RGB555: case OGPU_FMT_RGB555PC: {
        int r5, b5, gg;
        hi = tx->format == OGPU_FMT_RGB565PC || tx->format == OGPU_FMT_RGB555PC ? p[1] : p[0];
        lo = tx->format == OGPU_FMT_RGB565PC || tx->format == OGPU_FMT_RGB555PC ? p[0] : p[1];
        if (tx->format == OGPU_FMT_RGB565 || tx->format == OGPU_FMT_RGB565PC) {
            r5 = hi >> 3; gg = ((hi & 7) << 3) | (lo >> 5); b5 = lo & 31;
            g = (gg << 2) | (gg >> 4);
        } else {
            r5 = (hi >> 2) & 31; gg = ((hi & 3) << 3) | (lo >> 5); b5 = lo & 31;
            g = (gg << 3) | (gg >> 2);
        }
        r = (r5 << 3) | (r5 >> 2);
        b = (b5 << 3) | (b5 >> 2);
        break;
    }
    case OGPU_FMT_A8: r = g = b = 255; a = p[0]; break;
    default:                                            /* INDEX8, CLUT8 */
        if (tx->table) {
            const ogpu_u8 *q = tx->table + (p[0] << 2);
            a = q[0]; r = q[1]; g = q[2]; b = q[3];
        } else {
            r = g = b = p[0];
        }
    }
    o[0] = (ogpu_u8)r; o[1] = (ogpu_u8)g; o[2] = (ogpu_u8)b; o[3] = (ogpu_u8)a;
}

static int gf_lw(const struct ogpu3d_tex *tx, int l) { int w = tx->w >> l; return w < 1 ? 1 : w; }
static int gf_lh(const struct ogpu3d_tex *tx, int l) { int h = tx->h >> l; return h < 1 ? 1 : h; }

/* A 64-bit hash of the texels the texture's levels hold (and its table). */
static uint64_t gf_hash_bytes(uint64_t h, const ogpu_u8 *p, size_t n) {
    const uint64_t m = 0x9E3779B97F4A7C15ULL;
    while (n >= 8) {
        uint64_t v;
        memcpy(&v, p, 8);
        h = (h ^ v) * m;
        h ^= h >> 29;
        p += 8; n -= 8;
    }
    while (n--) h = (h ^ *p++) * m;
    return h;
}

static uint64_t gf_hash(const struct ogpu3d_tex *tx) {
    uint64_t h = 0x243F6A8885A308D3ULL;
    int l, y;
    for (l = 0; l < tx->levels; l++) {
        int w = gf_lw(tx, l), hh = gf_lh(tx, l);
        for (y = 0; y < hh; y++) h = gf_hash_bytes(h, tx->level[l] + tx->bpr[l] * y, (size_t)w * (size_t)tx->bpp);
    }
    if (tx->table) h = gf_hash_bytes(h, tx->table, 1024);
    return h;
}

static VkDescriptorSet gf_tex_set(struct ogpu3d_gfx *g, struct gf_tex *e, VkSampler s) {
    VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    VkDescriptorImageInfo ii;
    VkWriteDescriptorSet wr = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    VkDescriptorSet set;
    int i;
    for (i = 0; i < e->nsets; i++) if (e->smp[i] == s) return e->set[i];
    if (e->nsets == GF_SETS) return 0;
    dai.descriptorPool = g->dpool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &g->dsl;
    if (vkAllocateDescriptorSets(g->cfg.dev, &dai, &set) != VK_SUCCESS) return 0;
    ii.sampler = s;
    ii.imageView = e->im.view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    wr.dstSet = set;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &ii;
    vkUpdateDescriptorSets(g->cfg.dev, 1, &wr, 0, 0);
    e->smp[e->nsets] = s;
    e->set[e->nsets++] = set;
    return set;
}

static void gf_tex_free(struct ogpu3d_gfx *g, struct gf_tex *e) {
    if (e->nsets) vkFreeDescriptorSets(g->cfg.dev, g->dpool, (uint32_t)e->nsets, e->set);
    gf_img_free(g, &e->im);
    g->tex_bytes -= e->bytes;
    memset(e, 0, sizeof *e);
}

/* The texture's levels as RGBA8, sent and waited for (a texture is sent
 * once while its texels stay the same). */
static int gf_tex_upload(struct ogpu3d_gfx *g, struct gf_tex *e, const struct ogpu3d_tex *tx) {
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkBufferImageCopy rg[OGPU_TEX_LEVELS];
    size_t need = 0, at = 0;
    int l, x, y;
    for (l = 0; l < tx->levels; l++) need += (size_t)gf_lw(tx, l) * (size_t)gf_lh(tx, l) * 4;
    if (need > g->tssize) {
        size_t n = need < (1UL << 20) ? (1UL << 20) : need;
        gf_buffer_free(g, &g->tsbuf, &g->tsmem, &g->tsmap);
        g->tssize = 0;
        if (!gf_buffer(g, n, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &g->tsbuf, &g->tsmem, &g->tsmap, 0)) {
            gf_buffer_free(g, &g->tsbuf, &g->tsmem, &g->tsmap);
            return 0;
        }
        g->tssize = n;
    }
    memset(rg, 0, sizeof rg);
    for (l = 0; l < tx->levels; l++) {
        int w = gf_lw(tx, l), h = gf_lh(tx, l);
        ogpu_u8 *o = g->tsmap + at;
        for (y = 0; y < h; y++) {
            const ogpu_u8 *p = tx->level[l] + tx->bpr[l] * y;
            for (x = 0; x < w; x++, o += 4) gf_texel(tx, p + x * tx->bpp, o);
        }
        rg[l].bufferOffset = at;
        rg[l].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rg[l].imageSubresource.mipLevel = (uint32_t)l;
        rg[l].imageSubresource.layerCount = 1;
        rg[l].imageExtent.width = (uint32_t)w; rg[l].imageExtent.height = (uint32_t)h; rg[l].imageExtent.depth = 1;
        at += (size_t)w * (size_t)h * 4;
    }
    if (!gf_img(g, &e->im, tx->w, tx->h, tx->levels, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT))
        return 0;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g->tcb, &bi);
    gf_barrier(g->tcb, e->im.img, VK_IMAGE_ASPECT_COLOR_BIT, tx->levels, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyBufferToImage(g->tcb, g->tsbuf, e->im.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)tx->levels, rg);
    gf_barrier(g->tcb, e->im.img, VK_IMAGE_ASPECT_COLOR_BIT, tx->levels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vkEndCommandBuffer(g->tcb);
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g->tcb;
    if (vkQueueSubmit(g->cfg.queue, 1, &si, g->tfence) != VK_SUCCESS) { g->failed = 1; return 0; }
    if (!gf_wait(g, &g->tfence)) return 0;
    e->bytes = need + need / 3;
    g->tex_bytes += e->bytes;
    g->st.tex_uploads++;
    return 1;
}

/* The texture's entry, sent to the GPU if it isn't there with these texels.
 * May close the open group (to make room). 0: declined. */
static struct gf_tex *gf_texture(struct ogpu3d_gfx *g, const struct ogpu3d_tex *tx) {
    struct gf_tex *e = 0, *free_e = 0;
    uint64_t h = 0;
    int i, l, hashed = 0;
    if (tx->w < 1 || tx->h < 1 || (uint32_t)tx->w > g->max_dim || (uint32_t)tx->h > g->max_dim || tx->levels < 1
        || tx->levels > OGPU_TEX_LEVELS || !tx->bpp)
        return 0;
    for (l = 0; l < tx->levels; l++) if (!tx->level[l]) return 0;
    for (i = 0; i < GF_TEXN; i++) {
        struct gf_tex *c = &g->tex[i];
        if (!c->used) { if (!free_e) free_e = c; continue; }
        if (c->level0 == tx->level[0] && c->bpr0 == tx->bpr[0] && c->w == tx->w && c->h == tx->h && c->levels == tx->levels
            && c->format == tx->format && c->table == tx->table) {
            if (c->checked != g->run) {
                if (!hashed) { h = gf_hash(tx); hashed = 1; }
                if (c->hash != h) continue;     /* stale; another entry, or a new one */
                c->checked = g->run;
            }
            e = c;
            break;
        }
    }
    if (!e) {
        if (!hashed) h = gf_hash(tx);
        /* stale entries at this address go, when nothing open uses them */
        for (i = 0; i < GF_TEXN; i++) {
            struct gf_tex *c = &g->tex[i];
            if (c->used && c->level0 == tx->level[0] && (!g->open || c->group != g->groupno)) {
                gf_tex_free(g, c);
                if (!free_e || c < free_e) free_e = c;
            }
        }
        while (!free_e || g->tex_bytes > GF_TEXMEM) {
            struct gf_tex *lru = 0;
            for (i = 0; i < GF_TEXN; i++) {
                struct gf_tex *c = &g->tex[i];
                if (c->used && (!g->open || c->group != g->groupno) && (!lru || c->last < lru->last)) lru = c;
            }
            if (!lru) {
                if (!g->open) return 0;
                if (!ogpu3d_gfx_close(g)) return 0;    /* everything is free once the group has gone */
                continue;
            }
            gf_tex_free(g, lru);
            if (!free_e) free_e = lru;
        }
        e = free_e;
        if (!gf_tex_upload(g, e, tx)) { gf_img_free(g, &e->im); memset(e, 0, sizeof *e); return 0; }
        e->used = 1;
        e->level0 = tx->level[0]; e->table = tx->table; e->bpr0 = tx->bpr[0];
        e->w = tx->w; e->h = tx->h; e->levels = tx->levels; e->format = tx->format;
        e->hash = h;
        e->checked = g->run;
    }
    e->last = ++g->clock;
    return e;
}

/* ---- the frame in memory ----------------------------------------------------------------- */

static int gf_cbpp(int ci) { return ci == 2 ? 2 : 4; }

/* Rows move between memory and staging through cached memory, a whole row
 * at a time: staging, and video RAM on some devices, is uncached (V3D has
 * only that), where a load a byte costs the earth and memcpy's wide loads
 * do not. 0 when out of memory. */
static ogpu_u8 *gf_bounce(struct ogpu3d_gfx *g, size_t row) {
    if (2 * row > g->bounce_size) {
        ogpu_u8 *b = realloc(g->bounce, 2 * row);
        if (!b) return 0;
        g->bounce = b;
        g->bounce_size = 2 * row;
    }
    return g->bounce;
}

static uint32_t gf_ld32(const ogpu_u8 *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void gf_st32(ogpu_u8 *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t gf_swap32(uint32_t v) { return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24); }

/* One row of n pixels of format fmt from memory (p) into staging (o), as the image's. */
static void gf_row_in(ogpu_u8 *b, const ogpu_u8 *p, ogpu_u8 *o, int n, int fmt) {
    ogpu_u8 *c = b + (size_t)n * 4;
    int x;
    switch (fmt) {
    case OGPU_FMT_RGB565: case OGPU_FMT_Z16:
        memcpy(b, p, (size_t)n * 2);
        for (x = 0; x < n; x++) { c[2 * x] = b[2 * x + 1]; c[2 * x + 1] = b[2 * x]; }
        memcpy(o, c, (size_t)n * 2);
        return;
    case OGPU_FMT_ARGB32: case OGPU_FMT_ABGR32: case OGPU_FMT_XRGB32:
        memcpy(b, p, (size_t)n * 4);
        for (x = 0; x < n; x++) gf_st32(c + 4 * x, gf_swap32(gf_ld32(b + 4 * x)) | (fmt == OGPU_FMT_XRGB32 ? 0xFF000000u : 0));
        memcpy(o, c, (size_t)n * 4);
        return;
    case OGPU_FMT_Z32:
        memcpy(b, p, (size_t)n * 4);
        for (x = 0; x < n; x++) {
            float f = (float)((double)gf_swap32(gf_ld32(b + 4 * x)) / 4294967296.0);
            memcpy(c + 4 * x, &f, 4);
        }
        memcpy(o, c, (size_t)n * 4);
        return;
    }
    memcpy(o, p, (size_t)n * 4);        /* BGRA32, RGBA32: as they are */
}

/* One row back: staging (o) into memory (p). */
static void gf_row_out(ogpu_u8 *b, ogpu_u8 *p, const ogpu_u8 *o, int n, int fmt) {
    ogpu_u8 *c = b + (size_t)n * 4;
    int x;
    switch (fmt) {
    case OGPU_FMT_RGB565: case OGPU_FMT_Z16:
        memcpy(b, o, (size_t)n * 2);
        for (x = 0; x < n; x++) { c[2 * x] = b[2 * x + 1]; c[2 * x + 1] = b[2 * x]; }
        memcpy(p, c, (size_t)n * 2);
        return;
    case OGPU_FMT_ARGB32: case OGPU_FMT_ABGR32:
        memcpy(b, o, (size_t)n * 4);
        for (x = 0; x < n; x++) gf_st32(c + 4 * x, gf_swap32(gf_ld32(b + 4 * x)));
        memcpy(p, c, (size_t)n * 4);
        return;
    case OGPU_FMT_XRGB32:               /* X as it was */
        memcpy(b, o, (size_t)n * 4);
        memcpy(c, p, (size_t)n * 4);
        for (x = 0; x < n; x++)
            gf_st32(c + 4 * x, (gf_swap32(gf_ld32(b + 4 * x)) & 0xFFFFFF00u) | (gf_ld32(c + 4 * x) & 0xFFu));
        memcpy(p, c, (size_t)n * 4);
        return;
    case OGPU_FMT_Z32:
        memcpy(b, o, (size_t)n * 4);
        memcpy(c, p, (size_t)n * 4);
        for (x = 0; x < n; x++) {
            /* a Z the GPU left alone stays as it was, every bit of it */
            uint32_t old = gf_swap32(gf_ld32(c + 4 * x)), z;
            float f, was = (float)((double)old / 4294967296.0);
            double d;
            memcpy(&f, b + 4 * x, 4);
            if (!memcmp(&f, &was, 4)) continue;
            d = (double)f * 4294967296.0;
            z = d <= 0 ? 0 : d >= 4294967292.0 ? 0xFFFFFFFCu : (uint32_t)d & ~3u;
            gf_st32(c + 4 * x, gf_swap32(z));
        }
        memcpy(p, c, (size_t)n * 4);
        return;
    }
    memcpy(p, o, (size_t)n * 4);
}

/* Rows y0..y1 of the target (and its Z buffer) from memory into staging,
 * and copied into the images (outside a render pass). */
static void gf_rows_in(struct ogpu3d_gfx *g, int y0, int y1, int first) {
    VkBufferImageCopy rg;
    int bpp = gf_cbpp(g->ci), y, w = g->dw;
    ogpu_u8 *o = g->smap + g->sused;
    double t0 = gf_ms();
    memset(&rg, 0, sizeof rg);
    for (y = y0; y < y1; y++, o += (size_t)w * bpp) gf_row_in(g->bounce, g->dst + g->dbpr * y, o, w, g->dfmt);
    rg.bufferOffset = g->sused;
    rg.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rg.imageSubresource.layerCount = 1;
    rg.imageOffset.y = y0;
    rg.imageExtent.width = (uint32_t)w; rg.imageExtent.height = (uint32_t)(y1 - y0); rg.imageExtent.depth = 1;
    g->sused += (size_t)w * (size_t)(y1 - y0) * (size_t)bpp;
    gf_barrier(g->cb, g->col.img, VK_IMAGE_ASPECT_COLOR_BIT, 1,
               first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               first ? 0 : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
               first ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyBufferToImage(g->cb, g->sbuf, g->col.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
    gf_barrier(g->cb, g->col.img, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if (g->di) {
        int zy0 = y0, zy1 = y1 < g->zh ? y1 : g->zh, zw = g->zw < w ? g->zw : w, z32 = g->di == 2;
        VkImageLayout from = first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        gf_barrier(g->cb, g->dep.img, VK_IMAGE_ASPECT_DEPTH_BIT, 1, from, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   first ? 0 : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   first ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
        if (zy0 < zy1 && zw > 0) {
            o = g->smap + g->sused;
            for (y = zy0; y < zy1; y++, o += (size_t)zw * (z32 ? 4 : 2)) gf_row_in(g->bounce, g->zmem + g->zbpr * y, o, zw, g->zfmt);
            memset(&rg, 0, sizeof rg);
            rg.bufferOffset = g->sused;
            rg.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            rg.imageSubresource.layerCount = 1;
            rg.imageOffset.y = zy0;
            rg.imageExtent.width = (uint32_t)zw; rg.imageExtent.height = (uint32_t)(zy1 - zy0); rg.imageExtent.depth = 1;
            g->sused += (size_t)zw * (size_t)(zy1 - zy0) * (z32 ? 4 : 2);
            vkCmdCopyBufferToImage(g->cb, g->sbuf, g->dep.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        }
        gf_barrier(g->cb, g->dep.img, VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
    }
    g->st.in_ms += gf_ms() - t0;
}

static void gf_pass_begin(struct ogpu3d_gfx *g) {
    VkRenderPassBeginInfo rb = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    VkViewport vp;
    rb.renderPass = g->rp[g->ci][g->di];
    rb.framebuffer = g->fb;
    rb.renderArea.extent.width = (uint32_t)g->dw;
    rb.renderArea.extent.height = (uint32_t)g->dh;
    vkCmdBeginRenderPass(g->cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
    vp.x = 0; vp.y = 0; vp.width = (float)g->dw; vp.height = (float)g->dh; vp.minDepth = 0; vp.maxDepth = 1;
    vkCmdSetViewport(g->cb, 0, 1, &vp);
    {
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(g->cb, 0, 1, &g->vbuf, &off);
    }
    g->in_pass = 1;
    g->bound = 0;
    g->bound_set = 0;
}

/* A group into this target (and Z buffer: zmem 0 for none). 0: declined. */
static int gf_group_open(struct ogpu3d_gfx *g, const struct ogpu_surface *dst, int ci, const struct ogpu3d_buf *z, int y0, int y1) {
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    int di = z ? (z->format == OGPU_FMT_Z32 ? 2 : 1) : 0, w = dst->w, h = dst->h;
    size_t need;
    double t0 = gf_ms();
    if (g->cfg.before) g->cfg.before(g->cfg.user);
    g->st.before_ms += gf_ms() - t0;
    /* the images (and framebuffer) for this size and these formats; a new
     * image's view may have an old one's handle, so the framebuffer goes too */
    if ((g->col.img && (g->col.w != w || g->col.h != h || g->col.fmt != gf_cfmt[ci]))
        || (di && g->dep.img && (g->dep.w != w || g->dep.h != h || g->dep.fmt != gf_dfmt[di]))) {
        if (g->fb) vkDestroyFramebuffer(g->cfg.dev, g->fb, 0);
        g->fb = 0;
    }
    if (g->col.img && (g->col.w != w || g->col.h != h || g->col.fmt != gf_cfmt[ci])) gf_img_free(g, &g->col);
    if (!g->col.img && !gf_img(g, &g->col, w, h, 1, gf_cfmt[ci],
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT))
        return 0;
    if (di) {
        if (g->dep.img && (g->dep.w != w || g->dep.h != h || g->dep.fmt != gf_dfmt[di])) gf_img_free(g, &g->dep);
        if (!g->dep.img && !gf_img(g, &g->dep, w, h, 1, gf_dfmt[di],
                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                                       | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   VK_IMAGE_ASPECT_DEPTH_BIT))
            return 0;
    }
    if (!gf_render_pass(g, ci, di)) return 0;
    if (!g->fb || g->fb_c != g->col.view || g->fb_d != (di ? g->dep.view : 0) || g->ci != ci || g->di != di) {
        VkFramebufferCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        VkImageView v[2];
        if (g->fb) vkDestroyFramebuffer(g->cfg.dev, g->fb, 0);
        g->fb = 0;
        v[0] = g->col.view; v[1] = di ? g->dep.view : 0;
        fi.renderPass = g->rp[ci][di];
        fi.attachmentCount = di ? 2 : 1;
        fi.pAttachments = v;
        fi.width = (uint32_t)w; fi.height = (uint32_t)h; fi.layers = 1;
        if (vkCreateFramebuffer(g->cfg.dev, &fi, 0, &g->fb) != VK_SUCCESS) { g->fb = 0; return 0; }
        g->fb_c = v[0]; g->fb_d = v[1];
    }
    /* staging: every row in (once) and out, colour and Z */
    need = 2 * (size_t)w * (size_t)h * ((size_t)gf_cbpp(ci) + (di ? 4 : 0));
    if (need > g->ssize) {
        gf_buffer_free(g, &g->sbuf, &g->smem, &g->smap);
        g->ssize = 0;
        if (!gf_buffer(g, need, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &g->sbuf, &g->smem, &g->smap, &g->scoherent)) {
            gf_buffer_free(g, &g->sbuf, &g->smem, &g->smap);
            return 0;
        }
        g->ssize = need;
    }
    if (!gf_bounce(g, (size_t)w * 4)) return 0;
    g->ci = ci; g->di = di;
    g->dst = dst->pixels; g->dbpr = dst->bpr; g->dw = w; g->dh = h; g->dfmt = dst->format;
    if (di) { g->zmem = z->mem; g->zbpr = z->bpr; g->zw = z->w; g->zh = z->h; g->zfmt = z->format; }
    else g->zmem = 0;
    g->sused = 0;
    g->vused = 0;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(g->cb, &bi) != VK_SUCCESS) { g->failed = 1; return 0; }
    gf_rows_in(g, y0, y1, 1);
    g->uy0 = y0; g->uy1 = y1;
    gf_pass_begin(g);
    g->open = 1;
    g->groupno++;
    g->st.groups++;
    return 1;
}

/* Rows y0..y1 copied in, where they aren't yet. */
static void gf_group_rows(struct ogpu3d_gfx *g, int y0, int y1) {
    if (y0 >= g->uy0 && y1 <= g->uy1) return;
    vkCmdEndRenderPass(g->cb);
    g->in_pass = 0;
    if (y0 < g->uy0) { gf_rows_in(g, y0, g->uy0, 0); g->uy0 = y0; }
    if (y1 > g->uy1) { gf_rows_in(g, g->uy1, y1, 0); g->uy1 = y1; }
    gf_pass_begin(g);
}

int ogpu3d_gfx_open(const struct ogpu3d_gfx *g) { return g && g->open; }

int ogpu3d_gfx_close(struct ogpu3d_gfx *g) {
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkBufferImageCopy rg;
    VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    size_t cat, zat = 0;
    int bpp, y, w, zy1 = 0, zw = 0, z32 = 0;
    double t0, t1;
    if (!g || !g->open) return 1;
    g->open = 0;
    if (g->in_pass) vkCmdEndRenderPass(g->cb);
    g->in_pass = 0;
    w = g->dw;
    bpp = gf_cbpp(g->ci);
    cat = g->sused;
    gf_barrier(g->cb, g->col.img, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    memset(&rg, 0, sizeof rg);
    rg.bufferOffset = cat;
    rg.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rg.imageSubresource.layerCount = 1;
    rg.imageOffset.y = g->uy0;
    rg.imageExtent.width = (uint32_t)w; rg.imageExtent.height = (uint32_t)(g->uy1 - g->uy0); rg.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(g->cb, g->col.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g->sbuf, 1, &rg);
    if (g->di) {
        zy1 = g->uy1 < g->zh ? g->uy1 : g->zh;
        zw = g->zw < w ? g->zw : w;
        z32 = g->di == 2;
        zat = cat + (size_t)w * (size_t)(g->uy1 - g->uy0) * (size_t)bpp;
        gf_barrier(g->cb, g->dep.img, VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        if (g->uy0 < zy1 && zw > 0) {
            memset(&rg, 0, sizeof rg);
            rg.bufferOffset = zat;
            rg.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            rg.imageSubresource.layerCount = 1;
            rg.imageOffset.y = g->uy0;
            rg.imageExtent.width = (uint32_t)zw; rg.imageExtent.height = (uint32_t)(zy1 - g->uy0); rg.imageExtent.depth = 1;
            vkCmdCopyImageToBuffer(g->cb, g->dep.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g->sbuf, 1, &rg);
        }
    }
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(g->cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, 0, 0, 0);
    if (vkEndCommandBuffer(g->cb) != VK_SUCCESS) { g->failed = 1; return 0; }
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g->cb;
    if (!g->scoherent) {
        VkMappedMemoryRange mr = { .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
        mr.memory = g->smem; mr.size = VK_WHOLE_SIZE;
        vkFlushMappedMemoryRanges(g->cfg.dev, 1, &mr);
    }
    t0 = gf_ms();
    if (vkQueueSubmit(g->cfg.queue, 1, &si, g->fence) != VK_SUCCESS) { g->failed = 1; return 0; }
    if (!gf_wait(g, &g->fence)) return 0;
    t1 = gf_ms();
    g->st.wait_ms += t1 - t0;
    if (!g->scoherent) {
        VkMappedMemoryRange mr = { .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
        mr.memory = g->smem; mr.size = VK_WHOLE_SIZE;
        vkInvalidateMappedMemoryRanges(g->cfg.dev, 1, &mr);
    }
    vkResetCommandBuffer(g->cb, 0);
    /* the rows back into memory, in the target's byte order */
    {
        const ogpu_u8 *o = g->smap + cat;
        for (y = g->uy0; y < g->uy1; y++, o += (size_t)w * bpp) gf_row_out(g->bounce, g->dst + g->dbpr * y, o, w, g->dfmt);
    }
    if (g->di && g->uy0 < zy1 && zw > 0) {
        const ogpu_u8 *o = g->smap + zat;
        for (y = g->uy0; y < zy1; y++, o += (size_t)zw * (z32 ? 4 : 2)) gf_row_out(g->bounce, g->zmem + g->zbpr * y, o, zw, g->zfmt);
    }
    g->st.out_ms += gf_ms() - t1;
    return 1;
}

/* ---- TRIANGLES ------------------------------------------------------------------------- */

static int gf_begin(void *user, const struct ogpu3d *t, const struct o3_pipe *pp, int x0, int y0, int x1, int y1) {
    struct ogpu3d_gfx *g = user;
    (void)t;
    g->cap = 1;
    g->pp = *pp;
    if (pp->tx) { g->tx = *pp->tx; g->pp.tx = &g->tx; }
    g->ax0 = x0; g->ay0 = y0; g->ax1 = x1; g->ay1 = y1;
    return 1;               /* the core draws nothing: this file does */
}

static const struct ogpu3d_sink gf_sink = { gf_begin, 0, 0 };

static int gf_target_ci(int fmt) {
    switch (fmt) {
    case OGPU_FMT_ARGB32: case OGPU_FMT_XRGB32: case OGPU_FMT_BGRA32: return 0;
    case OGPU_FMT_RGBA32: case OGPU_FMT_ABGR32: return 1;
    case OGPU_FMT_RGB565: return 2;
    }
    return -1;
}

/* The channels (R 1, G 2, B 4, A 8) a raw pixel mask lets through. */
static uint32_t gf_channels(int fmt, uint32_t wm) {
    uint32_t r, gg, b, a, m = 0;
    switch (fmt) {
    case OGPU_FMT_RGB565: r = 0xF800; gg = 0x07E0; b = 0x001F; a = 0; break;
    case OGPU_FMT_BGRA32: b = 0xFF000000u; gg = 0xFF0000; r = 0xFF00; a = 0xFF; break;
    case OGPU_FMT_RGBA32: r = 0xFF000000u; gg = 0xFF0000; b = 0xFF00; a = 0xFF; break;
    case OGPU_FMT_ABGR32: a = 0xFF000000u; b = 0xFF0000; gg = 0xFF00; r = 0xFF; break;
    case OGPU_FMT_XRGB32: a = 0; r = 0xFF0000; gg = 0xFF00; b = 0xFF; break;
    default: a = 0xFF000000u; r = 0xFF0000; gg = 0xFF00; b = 0xFF; break;   /* ARGB32 */
    }
    if ((wm & r) == r) m |= 1;
    if ((wm & gg) == gg) m |= 2;
    if ((wm & b) == b) m |= 4;
    if (a && (wm & a) == a) m |= 8;
    return m;
}

static int gf_overlap(const ogpu_u8 *a, long alen, const ogpu_u8 *b, long blen) {
    return a < b + blen && b < a + alen;
}

/* One vertex as ogpu_3d.c's o3_decode_vertex reads it. */
struct gf_in {
    int32_t x, y;               /* 28.4, as the core rounds */
    ogpu_u8 col[4], spec[4];    /* RGBA; specular RGB and fog A */
    int32_t un, vn;
    uint32_t z, wbits;
};

static void gf_decode(const ogpu_u8 *p, uint32_t layout, struct gf_in *v) {
    int32_t x = (int32_t)gf_rd32(p), y = (int32_t)gf_rd32(p + 4);
    int at = 20;
    if (x > 0x3FFF0000) x = 0x3FFF0000;
    if (x < -0x3FFF0000) x = -0x3FFF0000;
    if (y > 0x3FFF0000) y = 0x3FFF0000;
    if (y < -0x3FFF0000) y = -0x3FFF0000;
    v->x = (x + 0x800) >> 12;
    v->y = (y + 0x800) >> 12;
    v->col[0] = p[9]; v->col[1] = p[10]; v->col[2] = p[11]; v->col[3] = p[8];
    v->un = (int32_t)gf_rd32(p + 12);
    v->vn = (int32_t)gf_rd32(p + 16);
    v->z = 0; v->wbits = 0x3F800000u;
    v->spec[0] = v->spec[1] = v->spec[2] = 0; v->spec[3] = 255;
    if (layout & OGPU_LAY_Z) { v->z = gf_rd32(p + at) & ~3u; at += 4; }   /* the core keeps 30 bits */
    if (layout & OGPU_LAY_W) { v->wbits = gf_rd32(p + at); at += 4; }
    if (layout & OGPU_LAY_SPEC) { v->spec[3] = p[at]; v->spec[0] = p[at + 1]; v->spec[1] = p[at + 2]; v->spec[2] = p[at + 3]; }
}

static int gf_vsize(uint32_t layout) {
    int n = 20;
    if (layout & OGPU_LAY_Z) n += 4;
    if (layout & OGPU_LAY_W) n += 4;
    if (layout & OGPU_LAY_SPEC) n += 4;
    if (layout & OGPU_LAY_UV1) n += 8;
    return n;
}

struct gf_draw {
    int persp, has_z, ztest, d16, tex;
    float tw, th;
};

/* One triangle into the vertex buffer: 0 when it draws nothing (no area). */
static int gf_triangle(struct ogpu3d_gfx *g, const struct gf_draw *dr, const struct gf_in *a, const struct gf_in *b,
                       const struct gf_in *c, struct gf_vtx *o) {
    const struct gf_in *v[3];
    int64_t area = (int64_t)(b->x - a->x) * (c->y - a->y) - (int64_t)(c->x - a->x) * (b->y - a->y);
    float q[3] = { 1, 1, 1 }, lod = 0;
    int k;
    (void)g;
    if (!area) return 0;
    v[0] = a; v[1] = b; v[2] = c;
    if (dr->persp) {
        /* clip w = 1 / w over the nearest's (the core's q is w over the largest w) */
        float wmax = 0, w[3];
        for (k = 0; k < 3; k++) {
            uint32_t e = (v[k]->wbits >> 23) & 255;
            w[k] = (v[k]->wbits >> 31) || !e ? 0 : gf_float(v[k]->wbits);
            if (e == 255) wmax = -1;
            else if (wmax >= 0 && w[k] > wmax) wmax = w[k];
        }
        if (wmax > 0)
            for (k = 0; k < 3; k++) {
                float r = w[k] / wmax;
                if (r < 1.0f / 134217728.0f) r = 1.0f / 134217728.0f;
                q[k] = 1.0f / r;
            }
    }
    if (dr->tex) {
        /* the level of detail, once a triangle, as the core works it out */
        double U[3], V[3], ta;
        for (k = 0; k < 3; k++) {
            int32_t u = v[k]->un, w = v[k]->vn;
            if (u > 0x7FFFFF) u = 0x7FFFFF;
            if (u < -0x7FFFFF) u = -0x7FFFFF;
            if (w > 0x7FFFFF) w = 0x7FFFFF;
            if (w < -0x7FFFFF) w = -0x7FFFFF;
            U[k] = u / 65536.0 * dr->tw;
            V[k] = w / 65536.0 * dr->th;
        }
        ta = (U[1] - U[0]) * (V[2] - V[0]) - (U[2] - U[0]) * (V[1] - V[0]);
        if (ta < 0) ta = -ta;
        lod = ta > 0 ? (float)(0.5 * gf_log2(ta / ((double)(area < 0 ? -area : area) / 256.0))) : -1.0f;
    }
    for (k = 0; k < 3; k++) {
        int32_t u = v[k]->un, w = v[k]->vn;
        float z;
        if (u > 0x7FFFFF) u = 0x7FFFFF;
        if (u < -0x7FFFFF) u = -0x7FFFFF;
        if (w > 0x7FFFFF) w = 0x7FFFFF;
        if (w < -0x7FFFFF) w = -0x7FFFFF;
        o[k].pos[0] = v[k]->x / 16.0f;
        o[k].pos[1] = v[k]->y / 16.0f;
        if (!dr->ztest) z = 0;
        else if (dr->d16) {
            /* the hardware rounds to 16 bits where the core keeps the top 16: half a step down */
            double d = ((double)v[k]->z / 65536.0 - 0.5) / 65535.0;
            z = d < 0 ? 0 : (float)d;
        } else z = (float)((double)v[k]->z / 4294967296.0);
        o[k].pos[2] = z;
        o[k].pos[3] = q[k];
        o[k].tex[0] = u / 65536.0f;
        o[k].tex[1] = w / 65536.0f;
        o[k].tex[2] = lod;
        o[k].tex[3] = gf_float(v[k]->wbits);
        memcpy(o[k].col, v[k]->col, 4);
        memcpy(o[k].spec, v[k]->spec, 4);
    }
    return 1;
}

int ogpu3d_gfx_triangles(struct ogpu3d_gfx *g, struct ogpu3d *t, const ogpu_u8 *cmd, long words, int *result) {
    const struct ogpu3d_sink *keep_sink;
    void *keep_user;
    struct ogpu_core *c = t->core;
    const ogpu_u8 *a = cmd + 4, *verts, *idx = 0;
    const struct o3_pipe *pp = &g->pp;
    const struct ogpu_surface *dst;
    struct gf_tex *te = 0;
    struct gf_draw dr;
    struct gf_push pc;
    VkSampler smp = 0;
    VkDescriptorSet set;
    VkPipeline pipe;
    VkRect2D sc;
    uint32_t vaddr, vcount, stride, iaddr, icount, isize, layout = 0, vsize, n, i, key, en, chan;
    int r, ci, three_d = words - 1 >= 9, prim, fog, blend, have = 0, first, bad = 0;
    const struct ogpu3d_buf *z = 0;
    struct gf_in vb[3];
    if (g->failed) return 0;
    /* the core's checks and settings, up to its sink's begin */
    g->cap = 0;
    keep_sink = t->sink; keep_user = t->sink_user;
    t->sink = &gf_sink; t->sink_user = g;
    r = ogpu3d_op(t, OGPU_OP_TRIANGLES, cmd, words);
    t->sink = keep_sink; t->sink_user = keep_user;
    if (!g->cap) { *result = r; return 1; }     /* an error, or nothing to draw: as the core says */
    if (g->ax0 >= g->ax1 || g->ay0 >= g->ay1) { *result = OGPU_OK; return 1; }

    /* what this pipeline can't draw as the core does */
    en = pp->en;
    dst = pp->dst;
    ci = gf_target_ci(pp->fmt);
    if (ci < 0 || !dst->pixels || dst->w < 1 || dst->h < 1 || (uint32_t)dst->w > g->max_dim || (uint32_t)dst->h > g->max_dim)
        goto decline;
    if (((en & OGPU_R3D_STENCIL) && t->stencil.mem) || ((en & OGPU_R3D_LOGICOP) && t->logicop != 4)
        || ((en & OGPU_R3D_STIPPLE) && t->stipple) || ((en & OGPU_R3D_CHROMA) && t->chroma > 1 && pp->tx))
        goto decline;
    fog = (en & OGPU_R3D_FOG) ? t->fogmode : 0;
    if (!pp->has_w && fog != OGPU_FOG_VERTEX) fog = 0;
    if (fog == OGPU_FOG_VERTEX && !pp->has_spec) fog = 0;
    if (fog < 0 || fog > OGPU_FOG_VERTEX) goto decline;
    memset(&dr, 0, sizeof dr);
    dr.ztest = (en & OGPU_R3D_ZTEST) && t->depth.mem;
    if (dr.ztest) {
        z = &t->depth;
        if ((z->format != OGPU_FMT_Z16 && z->format != OGPU_FMT_Z32)
            || gf_overlap(z->mem, z->bpr * z->h, dst->pixels, dst->bpr * dst->h))
            goto decline;
        dr.d16 = z->format == OGPU_FMT_Z16;
    }
    if (pp->tx) {
        if (pp->tx->min == OGPU3D_PIXELART || pp->tx->mag == OGPU3D_PIXELART
            || ((pp->tx->wrap_s == OGPU_WRAP_BORDER || pp->tx->wrap_t == OGPU_WRAP_BORDER) && gf_border(pp->tx->border) < 0))
            goto decline;
        dr.tex = 1;
        dr.tw = (float)pp->tx->w;
        dr.th = (float)pp->tx->h;
    }
    dr.persp = (en & OGPU_R3D_PERSPECTIVE) && pp->has_w;
    dr.has_z = pp->has_z;
    blend = (en & OGPU_R3D_BLEND) && !(pp->bsrc == 2 && pp->bdst == 1);

    /* the texture (this may close the group, to make room) */
    if (pp->tx) {
        if (!(te = gf_texture(g, pp->tx)) || !(smp = gf_sampler(g, pp->tx)) || !(set = gf_tex_set(g, te, smp))) goto decline;
    } else set = g->dummy.set[0];

    /* the vertices: the core has checked the command and mapped them once */
    vaddr = gf_rd32(a); vcount = gf_rd32(a + 4); stride = gf_rd32(a + 8);
    iaddr = gf_rd32(a + 12); icount = gf_rd32(a + 16); isize = gf_rd32(a + 20);
    if (three_d) layout = gf_rd32(a + 32);
    prim = (int)(layout & OGPU_LAY_PRIM_MASK);
    vsize = (uint32_t)gf_vsize(layout);
    verts = c->map(c->user, vaddr, stride * (vcount - 1) + vsize);
    if (!verts) goto decline;
    if (iaddr) { idx = c->map(c->user, iaddr, icount * isize); if (!idx) goto decline; }
    n = idx ? icount : vcount;
    if ((size_t)n * sizeof(struct gf_vtx) > GF_VBUF) goto decline;

    /* the group: this target and Z buffer, these rows */
    if (g->open && (g->dst != dst->pixels || g->dbpr != dst->bpr || g->dw != dst->w || g->dh != dst->h || g->dfmt != dst->format
                    || (dr.ztest && (g->zmem != z->mem || g->zbpr != z->bpr || g->zw != z->w || g->zh != z->h
                                     || g->zfmt != z->format))
                    || g->vused + (size_t)n * sizeof(struct gf_vtx) > GF_VBUF))
        if (!ogpu3d_gfx_close(g)) goto decline;
    if (!g->open) {
        if (!gf_group_open(g, dst, ci, dr.ztest ? z : 0, g->ay0, g->ay1)) goto decline;
        if (te) te->group = g->groupno;
    } else gf_group_rows(g, g->ay0, g->ay1);
    if (te) te->group = g->groupno;

    /* the pipeline */
    chan = gf_channels(pp->fmt, pp->wmask);
    key = (uint32_t)ci | (uint32_t)g->di << 2 | (uint32_t)blend << 4 | (uint32_t)(pp->bsrc & 15) << 5
        | (uint32_t)(pp->bdst & 15) << 9 | (uint32_t)(dr.ztest && g->di) << 15 | (uint32_t)(t->zfunc & 15) << 16
        | (uint32_t)((en & OGPU_R3D_ZWRITE) != 0) << 20 | chan << 21;
    if (!blend) key &= ~(0xFFu << 5);
    if (!(dr.ztest && g->di)) key &= ~(0x1Fu << 16);
    pipe = gf_pipeline(g, key);
    if (!pipe) { g->failed = 1; ogpu3d_gfx_close(g); return 0; }

    /* the triangles, as a list */
    first = (int)(g->vused / sizeof(struct gf_vtx));
    {
        struct gf_vtx *o = (struct gf_vtx *)(g->vmap + g->vused);
        uint32_t nv = 0;
        for (i = 0; i < n; i++) {
            uint32_t vi = i;
            if (idx) {
                if (isize == 1) vi = idx[i];
                else if (isize == 2) vi = ((uint32_t)idx[i * 2] << 8) | idx[i * 2 + 1];
                else vi = gf_rd32(idx + i * 4);
                if (vi >= vcount) { bad = 1; break; }   /* the core stops there, as here */
            }
            if (prim == OGPU_LAY_LIST) {
                gf_decode(verts + vi * stride, layout, &vb[have]);
                if (++have == 3) {
                    nv += 3 * (uint32_t)gf_triangle(g, &dr, &vb[0], &vb[1], &vb[2], o + nv);
                    have = 0;
                }
            } else {
                if (have == 3) {
                    if (prim == OGPU_LAY_STRIP) vb[0] = vb[1];
                    vb[1] = vb[2];
                    have = 2;
                }
                gf_decode(verts + vi * stride, layout, &vb[have]);
                if (++have == 3) nv += 3 * (uint32_t)gf_triangle(g, &dr, &vb[0], &vb[1], &vb[2], o + nv);
            }
        }
        g->vused += nv * sizeof(struct gf_vtx);
        n = nv;
    }
    g->st.draws++;
    *result = bad ? OGPU_ERR_UNSUPPORTED : OGPU_OK;
    if (!n) return 1;

    /* the push constants: the core's pixel, in the fragment shader */
    memset(&pc, 0, sizeof pc);
    pc.scale[0] = 2.0f / (float)g->dw;
    pc.scale[1] = 2.0f / (float)g->dh;
    pc.flags = (pp->tx ? GF_TEX : 0) | ((en & OGPU_R3D_GOURAUD) ? GF_GOURAUD : 0)
             | (((en & OGPU_R3D_SPECULAR) && pp->has_spec) ? GF_SPEC : 0) | (ci == 2 && !blend ? GF_Q565 : 0)
             | fog << 4 | (pp->env & 15) << 8 | (pp->base & 15) << 12
             | ((en & OGPU_R3D_ALPHATEST) && t->afunc >= 1 && t->afunc <= 7 ? t->afunc : 0) << 16;
    pc.aref = t->aref;
    for (i = 0; i < 4; i++) { pc.cur[i] = pp->cur[i]; pc.envc[i] = pp->envc[i]; pc.fogc[i] = pp->fog[i]; }
    {
        float s = gf_float(t->fogkey[1]), e = gf_float(t->fogkey[2]), d = gf_float(t->fogkey[3]);
        pc.fogp[0] = s == s ? s : 0; pc.fogp[1] = e == e ? e : 0; pc.fogp[2] = d == d ? d : 0;
    }
    if (g->bound != pipe) { vkCmdBindPipeline(g->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe); g->bound = pipe; }
    if (g->bound_set != set) {
        vkCmdBindDescriptorSets(g->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g->layout, 0, 1, &set, 0, 0);
        g->bound_set = set;
    }
    {
        float k[4];
        for (i = 0; i < 4; i++) k[i] = (float)pp->kc[i] / 255.0f;
        vkCmdSetBlendConstants(g->cb, k);
    }
    sc.offset.x = g->ax0; sc.offset.y = g->ay0;
    sc.extent.width = (uint32_t)(g->ax1 - g->ax0); sc.extent.height = (uint32_t)(g->ay1 - g->ay0);
    vkCmdSetScissor(g->cb, 0, 1, &sc);
    vkCmdPushConstants(g->cb, g->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
    vkCmdDraw(g->cb, n, 1, (uint32_t)first, 0);
    return 1;
decline:
    g->st.declined++;
    if (g->open) ogpu3d_gfx_close(g);
    return 0;
}

void ogpu3d_gfx_new_run(struct ogpu3d_gfx *g) { if (g) g->run++; }

void ogpu3d_gfx_stats(const struct ogpu3d_gfx *g, struct ogpu3d_gfx_stats *s) {
    if (g) *s = g->st;
    else memset(s, 0, sizeof *s);
}
