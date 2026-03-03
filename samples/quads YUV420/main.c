/*
 * This sample provides a very basic demonstration of 3D rendering on the Xbox,
 * using pbkit. Based on the pbkit demo sources.
 */
#include <hal/video.h>
#include <hal/xbox.h>
#include <math.h>
#include <pbkit/pbkit.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <xboxkrnl/xboxkrnl.h>
#include <hal/debug.h>
#include <windows.h>
//#include "math3d.h"

#define DBG

static uint32_t *alloc_vertices;
static uint32_t  num_vertices;
static float     m_viewport[4][4];

//VECTOR v_light_color = {  1,   1,   1,  1 };
//VECTOR v_light_pos   = {  0, 140,   0,  1 };
//float light_ambient  = 0.125f;

//NV20 GPU expects BGRA textures, not RGBA!!!
//#include "texture_simple.h"
//#include "texture_320x240.h"
#include "texture_640x480.h"


typedef struct {
    float pos[3];
    float color[3];
} __attribute__((packed)) ColoredVertex;

static const ColoredVertex vertsOld[] = {
    //  X     Y     Z       R     G     B
    {{-1.0, -1.0,  1.0}, { 0.1,  0.1,  0.6} }, /* Background triangle 1 */
    {{-1.0,  1.0,  1.0}, { 0.0,  0.0,  0.0} },
    {{ 1.0,  1.0,  1.0}, { 0.0,  0.0,  0.0} },
    {{-1.0, -1.0,  1.0}, { 0.1,  0.1,  0.6} }, /* Background triangle 2 */
    {{ 1.0,  1.0,  1.0}, { 0.0,  0.0,  0.0} },
    {{ 1.0, -1.0,  1.0}, { 0.1,  0.1,  0.6} }
};

typedef struct {
    float pos[3];      // Position (X, Y, Z)
    float normal[3];   // Normal (not used, but for alignment)
    float texcoord[2]; // Texture coordinates (U, V) - 0.0 to 1.0
} __attribute__((packed)) TexturedVertex;


//For texture_simple.h -> this texture words on real hardware and Xemu
/*
static const TexturedVertex verts[] = {
    // X    Y    Z     NX   NY   NZ   U    V  (U,V as pixel coords like mesh)
    {{-1.0, -1.0, 1.0}, {0.0, 0.0, 1.0}, {0.0, 0.0}}, // Bottom-left (0,0)
    {{-1.0,  1.0, 1.0}, {0.0, 0.0, 1.0}, {0.0, 255.0}}, // Top-left (0,255)
    {{ 1.0,  1.0, 1.0}, {0.0, 0.0, 1.0}, {255.0, 255.0}}, // Top-right (255,255)
    
    {{-1.0, -1.0, 1.0}, {0.0, 0.0, 1.0}, {0.0, 0.0}}, // Bottom-left (0,0)
    {{ 1.0,  1.0, 1.0}, {0.0, 0.0, 1.0}, {255.0, 255.0}}, // Top-right (255,255)
    {{ 1.0, -1.0, 1.0}, {0.0, 0.0, 1.0}, {255.0, 0.0}}, // Bottom-right (255,0)
};
*/


//For texture_320x240.h -> works in xemu and real hw
/*
static const TexturedVertex verts[] = {
    {{-1.0, -1.0, 1.0}, {0.0, 0.0, 1.0}, {0.0, 0.0}},        // Bottom-left (0,0)
    {{-1.0,  1.0, 1.0}, {0.0, 0.0, 1.0}, {0.0, 239.0}},      // Top-left (0,239)
    {{ 1.0,  1.0, 1.0}, {0.0, 0.0, 1.0}, {319.0, 239.0}},    // Top-right (319,239)
    
    {{-1.0, -1.0, 1.0}, {0.0, 0.0, 1.0}, {0.0, 0.0}},        // Bottom-left (0,0)
    {{ 1.0,  1.0, 1.0}, {0.0, 0.0, 1.0}, {319.0, 239.0}},    // Top-right (319,239)
    {{ 1.0, -1.0, 1.0}, {0.0, 0.0, 1.0}, {319.0, 0.0}},      // Bottom-right (319,0)
};
*/


//For texture_640x480.h -> works in xemu and real hw

static const TexturedVertex verts[] = {
    {{-1.0, -1.0, 1.0}, {0.0, 0.0, 1.0}, {0.0, 0.0}},        // Bottom-left (0,0)
    {{-1.0,  1.0, 1.0}, {0.0, 0.0, 1.0}, {0.0, 479.0}},      // Top-left (0,479)
    {{ 1.0,  1.0, 1.0}, {0.0, 0.0, 1.0}, {639.0, 479.0}},    // Top-right (639,479)     
    
    {{-1.0, -1.0, 1.0}, {0.0, 0.0, 1.0}, {0.0, 0.0}},        // Bottom-left (0,0)
    {{ 1.0,  1.0, 1.0}, {0.0, 0.0, 1.0}, {639.0, 479.0}},    // Top-right (639,479)     
    {{ 1.0, -1.0, 1.0}, {0.0, 0.0, 1.0}, {639.0, 0.0}},      // Bottom-right (639,0)
};



#define MASK(mask, val) (((val) << (ffs(mask)-1)) & (mask))

struct {
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
    void     *addr;
} texture;

#define MAXRAM 0x03FFAFFF

static void matrix_viewport(float out[4][4], float x, float y, float width, float height, float z_min, float z_max);
static void init_shader(void);
static void init_textures(void);
static void upload_texture_from_yuv420(const uint8_t *yPlane, const uint8_t *uPlane, const uint8_t *vPlane,
    int width, int height, int strideY, int strideU, int strideV);
static void upload_yuv_planes_to_textures(const uint8_t *yPlane, const uint8_t *uPlane, const uint8_t *vPlane,
    int width, int height, int strideY, int strideU, int strideV);
static void set_attrib_pointer(unsigned int index, unsigned int format, unsigned int size, unsigned int stride, const void* data);
static void draw_arrays(unsigned int mode, int start, int count);

/* Allocate and upload Y/U/V planes to per-plane 32bpp textures (red channel contains plane value). */
static void upload_yuv_planes_to_textures(const uint8_t *yPlane, const uint8_t *uPlane, const uint8_t *vPlane,
    int width, int height, int strideY, int strideU, int strideV)
{
    size_t y_size, u_size, v_size;

    y_tex_width = width;
    y_tex_height = height;
    y_tex_pitch = y_tex_width * 4;
    y_size = (size_t)y_tex_pitch * y_tex_height;
    if (y_tex_addr) MmFreeContiguousMemory(y_tex_addr);
    y_tex_addr = MmAllocateContiguousMemoryEx(y_size, 0, MAXRAM, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (!y_tex_addr) return;

    u_tex_width = (width+1)/2;
    u_tex_height = (height+1)/2;
    u_tex_pitch = u_tex_width * 4;
    u_size = (size_t)u_tex_pitch * u_tex_height;
    if (u_tex_addr) MmFreeContiguousMemory(u_tex_addr);
    u_tex_addr = MmAllocateContiguousMemoryEx(u_size, 0, MAXRAM, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (!u_tex_addr) return;

    v_tex_width = (width+1)/2;
    v_tex_height = (height+1)/2;
    v_tex_pitch = v_tex_width * 4;
    v_size = (size_t)v_tex_pitch * v_tex_height;
    if (v_tex_addr) MmFreeContiguousMemory(v_tex_addr);
    v_tex_addr = MmAllocateContiguousMemoryEx(v_size, 0, MAXRAM, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (!v_tex_addr) return;

    /* Fill Y texture (red channel contains Y) */
    for (int y = 0; y < height; y++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)y_tex_addr + (size_t)y * y_tex_pitch);
        const uint8_t *src = yPlane + (size_t)y * strideY;
        for (int x = 0; x < width; x++) {
            uint8_t yy = src[x];
            uint32_t pix = ((uint32_t)yy << 16) | (0xFFu << 24);
            dst[x] = pix;
        }
    }

    /* Fill U and V textures (half-resolution) */
    for (int yy = 0; yy < u_tex_height; yy++) {
        uint32_t *udst = (uint32_t *)((uint8_t *)u_tex_addr + (size_t)yy * u_tex_pitch);
        uint32_t *vdst = (uint32_t *)((uint8_t *)v_tex_addr + (size_t)yy * v_tex_pitch);
        const uint8_t *usrc = uPlane + (size_t)yy * strideU;
        const uint8_t *vsrc = vPlane + (size_t)yy * strideV;
        for (int x = 0; x < u_tex_width; x++) {
            uint8_t uval = usrc[x];
            uint8_t vval = vsrc[x];
            udst[x] = ((uint32_t)uval << 16) | (0xFFu << 24);
            vdst[x] = ((uint32_t)vval << 16) | (0xFFu << 24);
        }
    }
}

/* Generate a simple YUV420 test pattern: horizontal luma gradient and
 * chroma ramps on subsampled U/V planes. Caller must provide suitably
 * allocated planes with given strides.
 */
static void generate_test_yuv420(uint8_t *yPlane, uint8_t *uPlane, uint8_t *vPlane,
    int width, int height, int strideY, int strideU, int strideV)
{
    /* Y: horizontal gradient */
    for (int y = 0; y < height; y++) {
        uint8_t *yd = yPlane + (size_t)y * strideY;
        for (int x = 0; x < width; x++) {
            yd[x] = (uint8_t)((x * 255) / (width - 1));
        }
    }

    /* U/V: subsampled ramps to show chroma */
    int uw = (width + 1) / 2;
    int uh = (height + 1) / 2;
    for (int y = 0; y < uh; y++) {
        uint8_t *ud = uPlane + (size_t)y * strideU;
        uint8_t *vd = vPlane + (size_t)y * strideV;
        for (int x = 0; x < uw; x++) {
            ud[x] = (uint8_t)((x * 255) / (uw - 1));
            vd[x] = (uint8_t)((y * 255) / (uh - 1));
        }
    }
}

/* Y/U/V texture handles (uploaded as 32bpp with plane value in red channel) */
static void *y_tex_addr = NULL;
static void *u_tex_addr = NULL;
static void *v_tex_addr = NULL;
static int y_tex_width = 0, y_tex_height = 0, y_tex_pitch = 0;
static int u_tex_width = 0, u_tex_height = 0, u_tex_pitch = 0;
static int v_tex_width = 0, v_tex_height = 0, v_tex_pitch = 0;

/* Main program function */
int main(void)
{
    uint32_t *p;
    int       i, status;
    int       width, height;
    int       start, last, now;
    int       fps, frames, frames_total;

    //XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    
    if (XVideoSetMode(1280, 720, 16, REFRESH_DEFAULT) == false) {
            XVideoSetMode(640, 480, 16, REFRESH_DEFAULT);
    }

    pb_set_color_format(NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5, false);   //Necessary when color depth is 16-bit


    if ((status = pb_init())) {
        debugPrint("pb_init Error %d\n", status);
        Sleep(2000);
        return 1;
    }

    pb_show_front_screen();

    /* Basic setup */
    width = pb_back_buffer_width();
    height = pb_back_buffer_height();

    /* Load constant rendering things (shaders, geometry) */
    init_shader();
    init_textures();

    alloc_vertices = MmAllocateContiguousMemoryEx(sizeof(verts), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    memcpy(alloc_vertices, verts, sizeof(verts));
    num_vertices = sizeof(verts)/sizeof(verts[0]);
    matrix_viewport(m_viewport, 0, 0, width, height, 0, 65536.0f);

    /* Setup to determine frames rendered every second */
    start = now = last = GetTickCount();
    frames_total = frames = fps = 0;

    while(1) {
        pb_wait_for_vbl();
        pb_reset();
        pb_target_back_buffer();

        /* Clear depth & stencil buffers */
        pb_erase_depth_stencil_buffer(0, 0, width, height);
        pb_fill(0, 0, width, height, 0x00000000);
        pb_erase_text_screen();

        while(pb_busy()) {
            /* Wait for completion... */
        }

        /*
            /* Setup Y/U/V texture stages (stage 0 = Y, 1 = U, 2 = V)
             * We upload each plane into a 32bpp texture where the red channel
             * contains the sampled plane value (0-255). The pixel shader
             * samples texY/texU/texV and reconstructs RGB.
             */
            p = pb_begin();
            p = pb_push2(p,NV20_TCL_PRIMITIVE_3D_TX_OFFSET(0),(DWORD)y_tex_addr & 0x03ffffff,0x0001122a);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_NPOT_PITCH(0), y_tex_pitch<<16);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_NPOT_SIZE(0),(y_tex_width<<16)|y_tex_height);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_WRAP(0),0x00030303);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_ENABLE(0),0x4003ffc0);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_FILTER(0),0x04074000); // linear

            p = pb_push2(p,NV20_TCL_PRIMITIVE_3D_TX_OFFSET(1),(DWORD)u_tex_addr & 0x03ffffff,0x0001122a);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_NPOT_PITCH(1), u_tex_pitch<<16);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_NPOT_SIZE(1),(u_tex_width<<16)|u_tex_height);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_WRAP(1),0x00030303);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_ENABLE(1),0x4003ffc0);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_FILTER(1),0x04074000); // linear to upsample

            p = pb_push2(p,NV20_TCL_PRIMITIVE_3D_TX_OFFSET(2),(DWORD)v_tex_addr & 0x03ffffff,0x0001122a);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_NPOT_PITCH(2), v_tex_pitch<<16);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_NPOT_SIZE(2),(v_tex_width<<16)|v_tex_height);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_WRAP(2),0x00030303);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_ENABLE(2),0x4003ffc0);
            p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_FILTER(2),0x04074000);
            pb_end(p);

        /* Send shader constants
         *
         * WARNING: Changing shader source code may impact constant locations!
         * Check the intermediate file (*.inl) for the expected locations after
         * changing the code.
         */
        p = pb_begin();

        /* Set shader constants cursor at C0 */
        p = pb_push1(p, NV097_SET_TRANSFORM_CONSTANT_LOAD, 96);

        /* Send the transformation matrix */
        pb_push(p++, NV097_SET_TRANSFORM_CONSTANT, 16);
        memcpy(p, m_viewport, 16*4); p+=16;

        pb_end(p);
        p = pb_begin();

        /* Clear all attributes */
        pb_push(p++, NV097_SET_VERTEX_DATA_ARRAY_FORMAT,16);
        for(i = 0; i < 16; i++) {
            *(p++) = 2;
        }
        pb_end(p);

        /* Set vertex position attribute (attribute 0 = POSITION) */
        set_attrib_pointer(0, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F,
                           3, sizeof(TexturedVertex), &alloc_vertices[0]);

        /* Set vertex normal attribute (not used, but matching mesh structure) */
        set_attrib_pointer(2, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F,
                           3, sizeof(TexturedVertex), &alloc_vertices[3]);

        /* Set texture coordinate attribute */
        set_attrib_pointer(9, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F,
                           2, sizeof(TexturedVertex), &alloc_vertices[6]);

        /* Begin drawing triangles */
        draw_arrays(NV097_SET_BEGIN_END_OP_TRIANGLES, 0, num_vertices);

        /* Draw some text on the screen */
        pb_print("Triangle Demo\n");
        pb_print("Frames: %d\n", frames_total);
        if (fps > 0) {
            pb_print("FPS: %d", fps);
        }
        pb_draw_text_screen();

        while(pb_busy()) {
            /* Wait for completion... */
        }

        /* Swap buffers (if we can) */
        while (pb_finished()) {
            /* Not ready to swap yet */
        }

        frames++;
        frames_total++;

        /* Latch FPS counter every second */
        now = GetTickCount();
        if ((now-last) > 1000) {
            fps = frames;
            frames = 0;
            last = now;
        }
    }

    /* Unreachable cleanup code */
    MmFreeContiguousMemory(alloc_vertices);
    pb_show_debug_screen();
    pb_kill();
    return 0;
}

/* Construct a viewport transformation matrix */
static void matrix_viewport(float out[4][4], float x, float y, float width, float height, float z_min, float z_max)
{
    memset(out, 0, 4*4*sizeof(float));
    out[0][0] = width/2.0f;
    out[1][1] = height/-2.0f;
    out[2][2] = z_max - z_min;
    out[3][3] = 1.0f;
    out[3][0] = x + width/2.0f;
    out[3][1] = y + height/2.0f;
    out[3][2] = z_min;
}

/* Load the shader we will render with */
static void init_shader(void)
{
    uint32_t *p;
    int       i;

    /* Setup vertex shader */
    uint32_t vs_program[] = {
        #include "vs.inl"
    };

    p = pb_begin();

    /* Set run address of shader */
    p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_START, 0);

    /* Set execution mode */
    p = pb_push1(p, NV097_SET_TRANSFORM_EXECUTION_MODE,
                 MASK(NV097_SET_TRANSFORM_EXECUTION_MODE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_PROGRAM)
                 | MASK(NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE_PRIV));

    p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN, 0);

    pb_end(p);

    /* Set cursor for program upload */
    p = pb_begin();
    p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_LOAD, 0);
    pb_end(p);

    /* Copy program instructions (16-bytes each) */
    for (i=0; i<sizeof(vs_program)/16; i++) {
        p = pb_begin();
        pb_push(p++, NV097_SET_TRANSFORM_PROGRAM, 4);
        memcpy(p, &vs_program[i*4], 4*4);
        p+=4;
        pb_end(p);
    }

    /* Setup fragment shader */
    p = pb_begin();
    #include "ps.inl"
    pb_end(p);
}

/* Set an attribute pointer */
static void set_attrib_pointer(unsigned int index, unsigned int format, unsigned int size, unsigned int stride, const void* data)
{
    uint32_t *p = pb_begin();
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + index*4,
                 MASK(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE, format) | \
                 MASK(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE, size) |  \
                 MASK(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE, stride));
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + index*4, (uint32_t)data & 0x03ffffff);
    pb_end(p);
}

/* Send draw commands for the triangles */
static void draw_arrays(unsigned int mode, int start, int count)
{
    uint32_t *p = pb_begin();
    p = pb_push1(p, NV097_SET_BEGIN_END, mode);

    p = pb_push1(p, 0x40000000|NV097_DRAW_ARRAYS, //bit 30 means all params go to same register 0x1810
                 MASK(NV097_DRAW_ARRAYS_COUNT, (count-1)) | MASK(NV097_DRAW_ARRAYS_START_INDEX, start));

    p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
    pb_end(p);
}

/* Load the textures we will render with */
static void init_textures(void)
{
    /* Generate a procedural YUV420 test pattern and upload it. */
    int w = texture_width;
    int h = texture_height;
    int strideY = w;
    int strideU = (w + 1) / 2;
    int strideV = (w + 1) / 2;

    uint8_t *yPlane = malloc((size_t)strideY * h);
    uint8_t *uPlane = malloc((size_t)strideU * ((h + 1) / 2));
    uint8_t *vPlane = malloc((size_t)strideV * ((h + 1) / 2));
    if (!yPlane || !uPlane || !vPlane) {
        free(yPlane);
        free(uPlane);
        free(vPlane);
        return;
    }

    generate_test_yuv420(yPlane, uPlane, vPlane, w, h, strideY, strideU, strideV);
    upload_yuv_planes_to_textures(yPlane, uPlane, vPlane, w, h, strideY, strideU, strideV);

    free(yPlane);
    free(uPlane);
    free(vPlane);
}

/* Helper: clamp int -> uint8_t */
static inline uint8_t clamp_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* Convert planar YUV420 (Y, U, V) to 32bpp BGRA and upload into texture.addr.
 * Parameters match the requested signature.
 */
static void upload_texture_from_yuv420(const uint8_t *yPlane, const uint8_t *uPlane, const uint8_t *vPlane,
    int width, int height, int strideY, int strideU, int strideV)
{
    size_t size = (size_t)width * (size_t)height * 4;
    texture.width = (uint16_t)width;
    texture.height = (uint16_t)height;
    texture.pitch = width * 4;
    texture.addr = MmAllocateContiguousMemoryEx(size, 0, MAXRAM, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (!texture.addr) {
        return;
    }

    for (int y = 0; y < height; y++) {
        uint32_t *dstRow = (uint32_t *)((uint8_t *)texture.addr + (size_t)y * texture.pitch);
        const uint8_t *yRow = yPlane + (size_t)y * strideY;
        const uint8_t *uRow = uPlane + (size_t)(y >> 1) * strideU;
        const uint8_t *vRow = vPlane + (size_t)(y >> 1) * strideV;

        for (int x = 0; x < width; x++) {
            int Y = yRow[x];
            int U = uRow[x >> 1];
            int V = vRow[x >> 1];

            int C = Y - 16;
            int D = U - 128;
            int E = V - 128;

            int R = (298 * C + 409 * E + 128) >> 8;
            int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
            int B = (298 * C + 516 * D + 128) >> 8;

            uint8_t r = clamp_u8(R);
            uint8_t g = clamp_u8(G);
            uint8_t b = clamp_u8(B);

            /* Store BGRA (little-endian: B | G<<8 | R<<16 | A<<24) */
            uint32_t pixel = (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16) | (0xFFu << 24);
            dstRow[x] = pixel;
        }
    }
}

