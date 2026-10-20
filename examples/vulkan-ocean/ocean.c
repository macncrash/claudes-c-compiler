/*
 * ocean.c -- a Vulkan ocean whose waves are simulated on the CPU.
 *
 * Every frame, the C code below evaluates a sum of Gerstner waves at every
 * vertex of a large grid (displaced positions plus analytic normals), splits
 * the work across threads, and streams the result straight into a mapped
 * Vulkan vertex buffer. The GPU only shades it. The "sim" time in the title
 * bar therefore measures machine code produced by whichever compiler built
 * this file.
 *
 * Controls: drag to orbit, scroll to zoom, space to pause, Esc to quit.
 *
 *   ./ocean [--grid N] [--waves N] [--threads N] [--novsync]
 *   ./ocean --bench FRAMES     simulate only (no window/GPU) and print timing
 */

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef COMPILER_NAME
#define COMPILER_NAME "unknown compiler"
#endif
#ifndef VULKAN_LIBRARY
#define VULKAN_LIBRARY "/opt/homebrew/lib/libvulkan.1.dylib"
#endif

/* SPIR-V generated from shaders/ by `glslc -mfmt=c`. */
static const uint32_t ocean_vert_spv[] =
#include "ocean.vert.inc"
;
static const uint32_t ocean_frag_spv[] =
#include "ocean.frag.inc"
;
static const uint32_t sky_vert_spv[] =
#include "sky.vert.inc"
;
static const uint32_t sky_frag_spv[] =
#include "sky.frag.inc"
;

#define PI 3.14159265358979f
#define FRAMES_IN_FLIGHT 2
#define MAX_IMAGES 8
#define MAX_WAVES 64
#define MAX_THREADS 16

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("ocean: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

#define VK_CHECK(call)                                                   \
    do {                                                                 \
        VkResult r_ = (call);                                            \
        if (r_ != VK_SUCCESS)                                            \
            die("%s failed at line %d (VkResult %d)", #call, __LINE__,   \
                (int)r_);                                                \
    } while (0)

/* ------------------------------------------------------------------------ */
/* Vector math                                                              */
/* ------------------------------------------------------------------------ */

typedef struct { float x, y, z; } vec3;

static vec3 v3(float x, float y, float z) { vec3 r = { x, y, z }; return r; }
static vec3 v3_sub(vec3 a, vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static float v3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static vec3 v3_cross(vec3 a, vec3 b)
{
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static vec3 v3_norm(vec3 a)
{
    float inv = 1.0f / sqrtf(v3_dot(a, a));
    return v3(a.x * inv, a.y * inv, a.z * inv);
}

/* Matrices are column-major, matching GLSL: m[col * 4 + row]. */
static void mat4_mul(float *out, const float *a, const float *b)
{
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = s;
        }
    memcpy(out, r, sizeof r);
}

/* Right-handed perspective for Vulkan clip space (y down, depth 0..1). */
static void mat4_perspective(float *m, float fovy, float aspect, float near, float far)
{
    float t = tanf(fovy * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = 1.0f / (aspect * t);
    m[5] = -1.0f / t;
    m[10] = far / (near - far);
    m[11] = -1.0f;
    m[14] = near * far / (near - far);
}

static void mat4_look_at(float *m, vec3 eye, vec3 center, vec3 up)
{
    vec3 f = v3_norm(v3_sub(center, eye));
    vec3 s = v3_norm(v3_cross(f, up));
    vec3 u = v3_cross(s, f);
    m[0] = s.x;  m[4] = s.y;  m[8] = s.z;   m[12] = -v3_dot(s, eye);
    m[1] = u.x;  m[5] = u.y;  m[9] = u.z;   m[13] = -v3_dot(u, eye);
    m[2] = -f.x; m[6] = -f.y; m[10] = -f.z; m[14] = v3_dot(f, eye);
    m[3] = 0;    m[7] = 0;    m[11] = 0;    m[15] = 1;
}

/* ------------------------------------------------------------------------ */
/* Ocean simulation                                                         */
/* ------------------------------------------------------------------------ */

typedef struct { float px, py, pz, nx, ny, nz; } Vertex;

typedef struct {
    int n;          /* grid is n x n vertices */
    int nwaves;
    float size;     /* world-space width of the grid */
    float k[MAX_WAVES], omega[MAX_WAVES], amp[MAX_WAVES], q[MAX_WAVES];
    float dx[MAX_WAVES], dz[MAX_WAVES], phase[MAX_WAVES];
    float *coord;   /* world x (and z) of each grid line */
    /*
     * A wave's phase at vertex (i, j) is k*dx*x_i + k*dz*z_j - omega*t + phase,
     * so its cos/sin factor into a column term and a row term. Columns are
     * fixed; rows are refreshed once per frame. Each vertex then needs one
     * complex multiply per wave instead of a sin and a cos.
     */
    float *col_cs;  /* [i][w][2]: cos, sin of k*dx*x_i */
    float *row_cs;  /* [j][w][2]: cos, sin of k*dz*z_j - omega*t + phase */
} Ocean;

static uint32_t rng_state = 0x9e3779b9u;

static float frand(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static void ocean_init(Ocean *o, int n, int nwaves, float size)
{
    o->n = n;
    o->nwaves = nwaves;
    o->size = size;
    o->coord = malloc(n * sizeof(float));
    o->col_cs = malloc((size_t)n * nwaves * 2 * sizeof(float));
    o->row_cs = malloc((size_t)n * nwaves * 2 * sizeof(float));
    if (!o->coord || !o->col_cs || !o->row_cs)
        die("out of memory");
    for (int i = 0; i < n; i++)
        o->coord[i] = ((float)i / (float)(n - 1) - 0.5f) * size;

    /* Wavelengths from long swell down to ~4 grid cells, spread around a
     * prevailing wind direction. */
    float spacing = size / (float)(n - 1);
    float longest = 70.0f;
    float shortest = fmaxf(4.0f * spacing, 2.5f);
    float wind = 2.4f;
    for (int w = 0; w < nwaves; w++) {
        float t = nwaves > 1 ? (float)w / (float)(nwaves - 1) : 0.0f;
        float lambda = longest * powf(shortest / longest, t);
        float angle = wind + (frand() * 2.0f - 1.0f) * 0.9f;
        o->k[w] = 2.0f * PI / lambda;
        o->omega[w] = sqrtf(9.81f * o->k[w]);
        o->amp[w] = lambda * 0.011f * (0.6f + 0.8f * frand());
        o->dx[w] = cosf(angle);
        o->dz[w] = sinf(angle);
        o->phase[w] = frand() * 2.0f * PI;
        o->q[w] = 0.8f / (o->k[w] * o->amp[w] * (float)nwaves);
    }

    for (int i = 0; i < n; i++)
        for (int w = 0; w < nwaves; w++) {
            float a = o->k[w] * o->dx[w] * o->coord[i];
            o->col_cs[(i * nwaves + w) * 2 + 0] = cosf(a);
            o->col_cs[(i * nwaves + w) * 2 + 1] = sinf(a);
        }
}

static void ocean_set_time(Ocean *o, float t)
{
    for (int j = 0; j < o->n; j++)
        for (int w = 0; w < o->nwaves; w++) {
            float a = o->k[w] * o->dz[w] * o->coord[j] - o->omega[w] * t + o->phase[w];
            o->row_cs[(j * o->nwaves + w) * 2 + 0] = cosf(a);
            o->row_cs[(j * o->nwaves + w) * 2 + 1] = sinf(a);
        }
}

/* Gerstner waves (GPU Gems ch. 1) for grid rows [j0, j1). */
static void ocean_eval_rows(const Ocean *o, int j0, int j1, Vertex *out)
{
    int n = o->n, nw = o->nwaves;
    for (int j = j0; j < j1; j++) {
        const float *row = &o->row_cs[j * nw * 2];
        float z = o->coord[j];
        for (int i = 0; i < n; i++) {
            const float *col = &o->col_cs[i * nw * 2];
            float px = o->coord[i], py = 0.0f, pz = z;
            float nx = 0.0f, ny = 1.0f, nz = 0.0f;
            for (int w = 0; w < nw; w++) {
                float cc = col[2 * w], cs = col[2 * w + 1];
                float rc = row[2 * w], rs = row[2 * w + 1];
                float c = cc * rc - cs * rs;   /* cos(col + row) */
                float s = cs * rc + cc * rs;   /* sin(col + row) */
                float a = o->amp[w];
                float qa = o->q[w] * a;
                float wa = o->k[w] * a;
                px += qa * o->dx[w] * c;
                pz += qa * o->dz[w] * c;
                py += a * s;
                nx -= o->dx[w] * wa * c;
                nz -= o->dz[w] * wa * c;
                ny -= o->q[w] * wa * s;
            }
            float inv = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz);
            Vertex *v = &out[j * n + i];
            v->px = px; v->py = py; v->pz = pz;
            v->nx = nx * inv; v->ny = ny * inv; v->nz = nz * inv;
        }
    }
}

typedef struct {
    const Ocean *ocean;
    int j0, j1;
    Vertex *out;
} SimJob;

static void *sim_thread(void *arg)
{
    SimJob *job = arg;
    ocean_eval_rows(job->ocean, job->j0, job->j1, job->out);
    return NULL;
}

static void ocean_simulate(Ocean *o, float t, Vertex *out, int nthreads)
{
    pthread_t threads[MAX_THREADS];
    SimJob jobs[MAX_THREADS];

    ocean_set_time(o, t);
    for (int i = 0; i < nthreads; i++) {
        jobs[i].ocean = o;
        jobs[i].j0 = o->n * i / nthreads;
        jobs[i].j1 = o->n * (i + 1) / nthreads;
        jobs[i].out = out;
    }
    for (int i = 1; i < nthreads; i++)
        if (pthread_create(&threads[i], NULL, sim_thread, &jobs[i]) != 0)
            die("pthread_create failed");
    sim_thread(&jobs[0]);
    for (int i = 1; i < nthreads; i++)
        pthread_join(threads[i], NULL);
}

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Simulate without a window or GPU; prints timing and a checksum so builds
 * from different compilers can be compared. */
static void run_bench(Ocean *o, int frames, int nthreads)
{
    size_t nverts = (size_t)o->n * o->n;
    Vertex *buf = malloc(nverts * sizeof(Vertex));
    if (!buf)
        die("out of memory");

    double best = 1e9, total = 0;
    for (int f = 0; f < frames; f++) {
        double t0 = now_seconds();
        ocean_simulate(o, (float)f / 60.0f, buf, nthreads);
        double dt = now_seconds() - t0;
        total += dt;
        if (dt < best)
            best = dt;
    }

    double sum = 0, maxh = -1e9, minh = 1e9;
    for (size_t i = 0; i < nverts; i++) {
        sum += buf[i].px + buf[i].py * 3.0 + buf[i].pz + buf[i].ny;
        if (buf[i].py > maxh) maxh = buf[i].py;
        if (buf[i].py < minh) minh = buf[i].py;
    }
    printf("compiler:   %s\n", COMPILER_NAME);
    printf("grid:       %d x %d (%zu vertices), %d waves, %d threads\n",
           o->n, o->n, nverts, o->nwaves, nthreads);
    printf("frames:     %d\n", frames);
    printf("sim time:   %.3f ms avg, %.3f ms best\n", total / frames * 1e3, best * 1e3);
    printf("throughput: %.1f M wave-evals/s\n",
           (double)nverts * o->nwaves * frames / total * 1e-6);
    printf("heights:    %.3f .. %.3f\n", minh, maxh);
    printf("checksum:   %.4f\n", sum / (double)nverts);
    free(buf);
}

/* ------------------------------------------------------------------------ */
/* Vulkan state                                                             */
/* ------------------------------------------------------------------------ */

typedef struct {
    float view_proj[16];
    float cam_pos_time[4];
    float sun_dir_aspect[4];
    float cam_fwd_tan[4];
    float cam_up[4];
} PushConstants;

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
} HostBuffer;

static SDL_Window *window;
static VkInstance instance;
static VkSurfaceKHR surface;
static VkPhysicalDevice gpu;
static VkPhysicalDeviceMemoryProperties mem_props;
static char gpu_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
static VkDevice device;
static uint32_t queue_family;
static VkQueue queue;
static VkFormat color_format;
static VkColorSpaceKHR color_space;
static const VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
static VkPresentModeKHR present_mode;
static VkRenderPass render_pass;
static VkPipelineLayout pipeline_layout;
static VkPipeline ocean_pipeline, sky_pipeline;
static VkCommandPool command_pool;

static VkSwapchainKHR swapchain;
static VkExtent2D extent;
static uint32_t image_count;
static VkImage images[MAX_IMAGES];
static VkImageView image_views[MAX_IMAGES];
static VkFramebuffer framebuffers[MAX_IMAGES];
static VkSemaphore render_done[MAX_IMAGES];
static VkImage depth_image;
static VkDeviceMemory depth_memory;
static VkImageView depth_view;

static struct {
    VkCommandBuffer cmd;
    VkSemaphore image_ready;
    VkFence in_flight;
    HostBuffer vertices;
} frames[FRAMES_IN_FLIGHT];

static HostBuffer indices;
static uint32_t index_count;

static bool has_extension(const VkExtensionProperties *props, uint32_t n, const char *name)
{
    for (uint32_t i = 0; i < n; i++)
        if (strcmp(props[i].extensionName, name) == 0)
            return true;
    return false;
}

static void create_instance(void)
{
    const char *exts[32];
    unsigned int sdl_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &sdl_count, NULL) || sdl_count > 30)
        die("SDL_Vulkan_GetInstanceExtensions: %s", SDL_GetError());
    SDL_Vulkan_GetInstanceExtensions(window, &sdl_count, exts);
    uint32_t ext_count = sdl_count;

    uint32_t avail_count = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &avail_count, NULL);
    VkExtensionProperties *avail = malloc(avail_count * sizeof *avail);
    vkEnumerateInstanceExtensionProperties(NULL, &avail_count, avail);

    /* MoltenVK is a "portability" driver; the loader hides it unless asked. */
    VkInstanceCreateFlags flags = 0;
    if (has_extension(avail, avail_count, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        exts[ext_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    free(avail);

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ccc ocean",
        .applicationVersion = 1,
        .pEngineName = "none",
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = flags,
        .pApplicationInfo = &app,
        .enabledExtensionCount = ext_count,
        .ppEnabledExtensionNames = exts,
    };
    VK_CHECK(vkCreateInstance(&ci, NULL, &instance));
}

static void pick_gpu(void)
{
    VkPhysicalDevice devs[8];
    uint32_t n = 8;
    vkEnumeratePhysicalDevices(instance, &n, devs);
    if (n == 0)
        die("no Vulkan device found (is MoltenVK installed?)");

    for (uint32_t d = 0; d < n; d++) {
        VkQueueFamilyProperties qf[16];
        uint32_t nqf = 16;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &nqf, qf);
        for (uint32_t q = 0; q < nqf; q++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devs[d], q, surface, &present);
            if ((qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                VkPhysicalDeviceProperties props;
                gpu = devs[d];
                queue_family = q;
                vkGetPhysicalDeviceProperties(gpu, &props);
                memcpy(gpu_name, props.deviceName, sizeof gpu_name);
                vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);
                return;
            }
        }
    }
    die("no GPU queue can present to this window");
}

static void create_device(void)
{
    const char *exts[2] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    uint32_t ext_count = 1;

    uint32_t avail_count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, NULL, &avail_count, NULL);
    VkExtensionProperties *avail = malloc(avail_count * sizeof *avail);
    vkEnumerateDeviceExtensionProperties(gpu, NULL, &avail_count, avail);
    /* The spec requires enabling this whenever the device offers it. */
    if (has_extension(avail, avail_count, "VK_KHR_portability_subset"))
        exts[ext_count++] = "VK_KHR_portability_subset";
    free(avail);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    VkDeviceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci,
        .enabledExtensionCount = ext_count,
        .ppEnabledExtensionNames = exts,
    };
    VK_CHECK(vkCreateDevice(gpu, &ci, NULL, &device));
    vkGetDeviceQueue(device, queue_family, 0, &queue);
}

static void choose_surface_format(bool vsync)
{
    VkSurfaceFormatKHR formats[64];
    uint32_t n = 64;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n, formats);
    if (n == 0)
        die("surface reports no formats");
    color_format = formats[0].format;
    color_space = formats[0].colorSpace;
    for (uint32_t i = 0; i < n; i++)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            color_format = formats[i].format;
            color_space = formats[i].colorSpace;
        }

    present_mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) {
        VkPresentModeKHR modes[8];
        uint32_t nm = 8;
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &nm, modes);
        for (uint32_t i = 0; i < nm; i++)
            if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR ||
                (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR && present_mode == VK_PRESENT_MODE_FIFO_KHR))
                present_mode = modes[i];
    }
}

static uint32_t find_memory_type(uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & want) == want)
            return i;
    die("no suitable memory type");
    return 0;
}

static void create_host_buffer(VkDeviceSize size, VkBufferUsageFlags usage, HostBuffer *b)
{
    VkBufferCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(device, &ci, NULL, &b->buffer));

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, b->buffer, &req);
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    VK_CHECK(vkAllocateMemory(device, &ai, NULL, &b->memory));
    VK_CHECK(vkBindBufferMemory(device, b->buffer, b->memory, 0));
    VK_CHECK(vkMapMemory(device, b->memory, 0, size, 0, &b->mapped));
}

static VkImageView create_view(VkImage image, VkFormat format, VkImageAspectFlags aspect)
{
    VkImageViewCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = { aspect, 0, 1, 0, 1 },
    };
    VkImageView view;
    VK_CHECK(vkCreateImageView(device, &ci, NULL, &view));
    return view;
}

static void create_depth_buffer(void)
{
    VkImageCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depth_format,
        .extent = { extent.width, extent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(device, &ci, NULL, &depth_image));

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, depth_image, &req);
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VK_CHECK(vkAllocateMemory(device, &ai, NULL, &depth_memory));
    VK_CHECK(vkBindImageMemory(device, depth_image, depth_memory, 0));
    depth_view = create_view(depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);
}

static void create_render_pass(void)
{
    VkAttachmentDescription attachments[2] = {
        {
            .format = color_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        },
        {
            .format = depth_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };
    VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depth_ref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = &depth_ref,
    };
    VkSubpassDependency dep = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dep,
    };
    VK_CHECK(vkCreateRenderPass(device, &ci, NULL, &render_pass));
}

static void destroy_swapchain_resources(void)
{
    for (uint32_t i = 0; i < image_count; i++) {
        vkDestroyFramebuffer(device, framebuffers[i], NULL);
        vkDestroyImageView(device, image_views[i], NULL);
        vkDestroySemaphore(device, render_done[i], NULL);
    }
    vkDestroyImageView(device, depth_view, NULL);
    vkDestroyImage(device, depth_image, NULL);
    vkFreeMemory(device, depth_memory, NULL);
}

static void create_swapchain(void)
{
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        int w, h;
        SDL_Vulkan_GetDrawableSize(window, &w, &h);
        extent.width = (uint32_t)w;
        extent.height = (uint32_t)h;
    }

    uint32_t want = caps.minImageCount + 1;
    if (caps.maxImageCount && want > caps.maxImageCount)
        want = caps.maxImageCount;

    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha))
        alpha = (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);

    VkSwapchainKHR old = swapchain;
    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = want,
        .imageFormat = color_format,
        .imageColorSpace = color_space,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = alpha,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = old,
    };
    VK_CHECK(vkCreateSwapchainKHR(device, &ci, NULL, &swapchain));
    if (old != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device, old, NULL);

    VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, NULL));
    if (image_count > MAX_IMAGES)
        die("swapchain has %u images; at most %d supported", image_count, MAX_IMAGES);
    VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images));

    create_depth_buffer();
    for (uint32_t i = 0; i < image_count; i++) {
        image_views[i] = create_view(images[i], color_format, VK_IMAGE_ASPECT_COLOR_BIT);
        VkImageView views[2] = { image_views[i], depth_view };
        VkFramebufferCreateInfo fci = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = render_pass,
            .attachmentCount = 2,
            .pAttachments = views,
            .width = extent.width,
            .height = extent.height,
            .layers = 1,
        };
        VK_CHECK(vkCreateFramebuffer(device, &fci, NULL, &framebuffers[i]));
        VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VK_CHECK(vkCreateSemaphore(device, &sci, NULL, &render_done[i]));
    }
}

static void recreate_swapchain(void)
{
    int w = 0, h = 0;
    SDL_Vulkan_GetDrawableSize(window, &w, &h);
    if (w == 0 || h == 0)
        return;   /* minimized; try again next frame */
    vkDeviceWaitIdle(device);
    destroy_swapchain_resources();
    create_swapchain();
}

static VkShaderModule create_shader(const uint32_t *code, size_t size)
{
    VkShaderModuleCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = code,
    };
    VkShaderModule m;
    VK_CHECK(vkCreateShaderModule(device, &ci, NULL, &m));
    return m;
}

static VkPipeline create_pipeline(const uint32_t *vs, size_t vs_size,
                                  const uint32_t *fs, size_t fs_size, bool is_ocean)
{
    VkShaderModule vmod = create_shader(vs, vs_size);
    VkShaderModule fmod = create_shader(fs, fs_size);
    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vmod, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fmod, .pName = "main" },
    };

    VkVertexInputBindingDescription binding = { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attrs[2] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, px) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nx) },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = is_ocean ? 1 : 0,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = is_ocean ? 2 : 0,
        .pVertexAttributeDescriptions = attrs,
    };
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = is_ocean,
        .depthWriteEnable = is_ocean,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    VkDynamicState dyn_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn_states,
    };
    VkGraphicsPipelineCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = pipeline_layout,
        .renderPass = render_pass,
        .subpass = 0,
    };
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, NULL, &pipeline));
    vkDestroyShaderModule(device, vmod, NULL);
    vkDestroyShaderModule(device, fmod, NULL);
    return pipeline;
}

static void create_pipelines(void)
{
    VkPushConstantRange range = {
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)
    };
    VkPipelineLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &range,
    };
    VK_CHECK(vkCreatePipelineLayout(device, &lci, NULL, &pipeline_layout));
    ocean_pipeline = create_pipeline(ocean_vert_spv, sizeof ocean_vert_spv,
                                     ocean_frag_spv, sizeof ocean_frag_spv, true);
    sky_pipeline = create_pipeline(sky_vert_spv, sizeof sky_vert_spv,
                                   sky_frag_spv, sizeof sky_frag_spv, false);
}

static void create_frames(int grid)
{
    VkCommandPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family,
    };
    VK_CHECK(vkCreateCommandPool(device, &pci, NULL, &command_pool));

    VkDeviceSize vsize = (VkDeviceSize)grid * grid * sizeof(Vertex);
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VkCommandBufferAllocateInfo ai = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VK_CHECK(vkAllocateCommandBuffers(device, &ai, &frames[i].cmd));
        VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VK_CHECK(vkCreateSemaphore(device, &sci, NULL, &frames[i].image_ready));
        VkFenceCreateInfo fci = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        VK_CHECK(vkCreateFence(device, &fci, NULL, &frames[i].in_flight));
        create_host_buffer(vsize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &frames[i].vertices);
    }

    /* Two triangles per grid cell. */
    index_count = (uint32_t)(grid - 1) * (grid - 1) * 6;
    create_host_buffer(index_count * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &indices);
    uint32_t *idx = indices.mapped;
    for (int j = 0; j < grid - 1; j++)
        for (int i = 0; i < grid - 1; i++) {
            uint32_t a = (uint32_t)(j * grid + i), b = a + 1;
            uint32_t c = a + (uint32_t)grid, d = c + 1;
            *idx++ = a; *idx++ = c; *idx++ = b;
            *idx++ = b; *idx++ = c; *idx++ = d;
        }
}

static void record_frame(VkCommandBuffer cmd, uint32_t image, VkBuffer vertices,
                         const PushConstants *pc)
{
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    VkClearValue clears[2];
    memset(clears, 0, sizeof clears);
    clears[1].depthStencil.depth = 1.0f;
    VkRenderPassBeginInfo rbi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffers[image],
        .renderArea = { { 0, 0 }, extent },
        .clearValueCount = 2,
        .pClearValues = clears,
    };
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = { 0, 0, (float)extent.width, (float)extent.height, 0, 1 };
    VkRect2D scissor = { { 0, 0 }, extent };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdPushConstants(cmd, pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof *pc, pc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sky_pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ocean_pipeline);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertices, &offset);
    vkCmdBindIndexBuffer(cmd, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));
}

/* ------------------------------------------------------------------------ */
/* Main                                                                     */
/* ------------------------------------------------------------------------ */

static void usage(void)
{
    fprintf(stderr,
            "usage: ocean [--grid N] [--waves N] [--threads N] [--novsync]\n"
            "       ocean --bench FRAMES [--grid N] [--waves N] [--threads N]\n");
    exit(2);
}

static int int_arg(int argc, char **argv, int *i, int lo, int hi)
{
    if (*i + 1 >= argc)
        usage();
    int v = atoi(argv[++*i]);
    if (v < lo || v > hi)
        die("%s must be between %d and %d", argv[*i - 1], lo, hi);
    return v;
}

int main(int argc, char **argv)
{
    int grid = 384, nwaves = 24, nthreads = 0, bench_frames = 0;
    bool vsync = true;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--grid") == 0)
            grid = int_arg(argc, argv, &i, 2, 2048);
        else if (strcmp(argv[i], "--waves") == 0)
            nwaves = int_arg(argc, argv, &i, 1, MAX_WAVES);
        else if (strcmp(argv[i], "--threads") == 0)
            nthreads = int_arg(argc, argv, &i, 1, MAX_THREADS);
        else if (strcmp(argv[i], "--bench") == 0)
            bench_frames = int_arg(argc, argv, &i, 1, 1000000);
        else if (strcmp(argv[i], "--novsync") == 0)
            vsync = false;
        else
            usage();
    }
    if (nthreads == 0) {
        long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
        nthreads = ncpu < 1 ? 1 : ncpu > MAX_THREADS ? MAX_THREADS : (int)ncpu;
    }

    Ocean ocean;
    ocean_init(&ocean, grid, nwaves, 300.0f);
    if (bench_frames) {
        run_bench(&ocean, bench_frames, nthreads);
        return 0;
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        die("SDL_Init: %s", SDL_GetError());
    if (SDL_Vulkan_LoadLibrary(NULL) != 0 && SDL_Vulkan_LoadLibrary(VULKAN_LIBRARY) != 0)
        die("could not load the Vulkan loader: %s", SDL_GetError());
    window = SDL_CreateWindow("ocean", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window)
        die("SDL_CreateWindow: %s", SDL_GetError());

    create_instance();
    if (!SDL_Vulkan_CreateSurface(window, instance, &surface))
        die("SDL_Vulkan_CreateSurface: %s", SDL_GetError());
    pick_gpu();
    create_device();
    choose_surface_format(vsync);
    create_render_pass();
    create_swapchain();
    create_pipelines();
    create_frames(grid);
    printf("ocean: %s, %dx%d grid, %d waves, %d threads, compiled with %s\n",
           gpu_name, grid, grid, nwaves, nthreads, COMPILER_NAME);

    float yaw = 0.6f, pitch = 0.22f, dist = 60.0f;
    bool running = true, paused = false, dragging = false, resized = false;
    double sim_t = 0, last = now_seconds(), stats_start = last, sim_accum = 0;
    int stats_frames = 0;
    uint32_t frame = 0;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE)
                    running = false;
                else if (ev.key.keysym.sym == SDLK_SPACE)
                    paused = !paused;
                break;
            case SDL_MOUSEBUTTONDOWN:
                dragging = true;
                break;
            case SDL_MOUSEBUTTONUP:
                dragging = false;
                break;
            case SDL_MOUSEMOTION:
                if (dragging) {
                    yaw -= (float)ev.motion.xrel * 0.005f;
                    pitch += (float)ev.motion.yrel * 0.004f;
                    pitch = fminf(fmaxf(pitch, 0.03f), 1.45f);
                }
                break;
            case SDL_MOUSEWHEEL:
                dist *= powf(0.92f, (float)ev.wheel.y);
                dist = fminf(fmaxf(dist, 8.0f), 250.0f);
                break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    resized = true;
                break;
            }
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(50);
            continue;
        }

        double t = now_seconds();
        double dt = t - last;
        last = t;
        if (!paused)
            sim_t += dt;
        if (!dragging)
            yaw += (float)dt * 0.04f;

        VkFence fence = frames[frame].in_flight;
        VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
        uint32_t image;
        VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                           frames[frame].image_ready, VK_NULL_HANDLE, &image);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            recreate_swapchain();
            continue;
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
            die("vkAcquireNextImageKHR failed (VkResult %d)", (int)r);
        VK_CHECK(vkResetFences(device, 1, &fence));

        /* The CPU work this demo is about. */
        double s0 = now_seconds();
        ocean_simulate(&ocean, (float)sim_t, frames[frame].vertices.mapped, nthreads);
        sim_accum += now_seconds() - s0;

        float aspect = (float)extent.width / (float)extent.height;
        float fovy = 55.0f * PI / 180.0f;
        vec3 eye = v3(dist * cosf(pitch) * sinf(yaw), dist * sinf(pitch) + 2.0f,
                      dist * cosf(pitch) * cosf(yaw));
        vec3 target = v3(0, 0, 0), up = v3(0, 1, 0);
        vec3 fwd = v3_norm(v3_sub(target, eye));
        vec3 sun = v3_norm(v3(-0.45f, 0.18f, -0.87f));

        PushConstants pc;
        float proj[16], view[16];
        mat4_perspective(proj, fovy, aspect, 0.1f, 1000.0f);
        mat4_look_at(view, eye, target, up);
        mat4_mul(pc.view_proj, proj, view);
        pc.cam_pos_time[0] = eye.x; pc.cam_pos_time[1] = eye.y;
        pc.cam_pos_time[2] = eye.z; pc.cam_pos_time[3] = (float)sim_t;
        pc.sun_dir_aspect[0] = sun.x; pc.sun_dir_aspect[1] = sun.y;
        pc.sun_dir_aspect[2] = sun.z; pc.sun_dir_aspect[3] = aspect;
        pc.cam_fwd_tan[0] = fwd.x; pc.cam_fwd_tan[1] = fwd.y;
        pc.cam_fwd_tan[2] = fwd.z; pc.cam_fwd_tan[3] = tanf(fovy * 0.5f);
        pc.cam_up[0] = up.x; pc.cam_up[1] = up.y; pc.cam_up[2] = up.z; pc.cam_up[3] = 0;

        VkCommandBuffer cmd = frames[frame].cmd;
        VK_CHECK(vkResetCommandBuffer(cmd, 0));
        record_frame(cmd, image, frames[frame].vertices.buffer, &pc);

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &frames[frame].image_ready,
            .pWaitDstStageMask = &wait_stage,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &render_done[image],
        };
        VK_CHECK(vkQueueSubmit(queue, 1, &si, fence));

        VkPresentInfoKHR pi = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &render_done[image],
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &image,
        };
        r = vkQueuePresentKHR(queue, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || resized) {
            resized = false;
            recreate_swapchain();
        } else if (r != VK_SUCCESS) {
            die("vkQueuePresentKHR failed (VkResult %d)", (int)r);
        }
        frame = (frame + 1) % FRAMES_IN_FLIGHT;

        stats_frames++;
        if (t - stats_start >= 0.5) {
            char title[256];
            snprintf(title, sizeof title,
                     "Ocean | %dx%d grid, %d waves, %d threads | sim %.2f ms | %.0f fps | built with %s%s",
                     grid, grid, nwaves, nthreads, sim_accum / stats_frames * 1e3,
                     stats_frames / (t - stats_start), COMPILER_NAME, paused ? " | paused" : "");
            SDL_SetWindowTitle(window, title);
            stats_start = t;
            stats_frames = 0;
            sim_accum = 0;
        }
    }

    vkDeviceWaitIdle(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
