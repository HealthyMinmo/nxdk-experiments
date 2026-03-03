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
#include "texture_640x480.h"

static uint32_t *alloc_vertices;
static uint32_t  num_vertices;
static float     m_viewport[4][4];

typedef struct {
    float pos[3];      // Position (X, Y, Z)
    float normal[3];   // Normal (not used, but for alignment)
    float texcoord[2]; // Texture coordinates (U, V) - 0.0 to 1.0
} __attribute__((packed)) TexturedVertex;

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
} texture, uv_texture;

#define MAXRAM 0x03FFAFFF

static void matrix_viewport(float out[4][4], float x, float y, float width, float height, float z_min, float z_max);
static void init_shader(void);
static void init_textures(void);
static void set_attrib_pointer(unsigned int index, unsigned int format, unsigned int size, unsigned int stride, const void* data);
static void draw_arrays(unsigned int mode, int start, int count);

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
            vd[x] = (uint8_t)((y * 255) / (uh - 1));  // Full vertical gradient
        }
    }
}

/* Upload Y and packed UV (U in red, V in green) as two separate textures */
static void upload_yuv420_as_two_textures(const uint8_t *yPlane, const uint8_t *uPlane, const uint8_t *vPlane,
    int width, int height, int strideY, int strideU, int strideV)
{
    /* Upload Y plane as 32bpp texture (Y in red channel) */
    size_t y_size = (size_t)width * 4 * height;
    texture.width = width;
    texture.height = height;
    texture.pitch = width * 4;
    if (texture.addr) MmFreeContiguousMemory(texture.addr);
    texture.addr = MmAllocateContiguousMemoryEx(y_size, 0, MAXRAM, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (!texture.addr) return;

    for (int y = 0; y < height; y++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)texture.addr + (size_t)y * texture.pitch);
        const uint8_t *src = yPlane + (size_t)y * strideY;
        for (int x = 0; x < width; x++) {
            uint8_t yy = src[x];
            uint32_t pix = ((uint32_t)yy << 16) | (0xFFu << 24);
            dst[x] = pix;
        }
    }

    /* Upload packed UV as 32bpp texture (U in red, V in green) */
    int uv_width = (width + 1) / 2;
    int uv_height = (height + 1) / 2;
    size_t uv_size = (size_t)uv_width * 4 * uv_height;
    uv_texture.width = uv_width;
    uv_texture.height = uv_height;
    uv_texture.pitch = uv_width * 4;
    if (uv_texture.addr) MmFreeContiguousMemory(uv_texture.addr);
    uv_texture.addr = MmAllocateContiguousMemoryEx(uv_size, 0, MAXRAM, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (!uv_texture.addr) return;

    for (int y = 0; y < uv_height; y++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)uv_texture.addr + (size_t)y * uv_texture.pitch);
        const uint8_t *usrc = uPlane + (size_t)y * strideU;
        const uint8_t *vsrc = vPlane + (size_t)y * strideV;
        for (int x = 0; x < uv_width; x++) {
            uint8_t u = usrc[x];
            uint8_t v = vsrc[x];
            /* Pack U in red channel, V in green channel, keep alpha=255 */
            uint32_t pix = ((uint32_t)u << 16) | ((uint32_t)v << 8) | (0xFFu << 24);
            dst[x] = pix;
        }
    }
}

int main(void)
{
    uint32_t *p;
    int       i, status;
    int       width, height;
    int       start, last, now;
    int       fps, frames, frames_total;

    //XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

    XVideoSetMode(640, 480, 16, REFRESH_DEFAULT);
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
         * Setup texture stages 0 and 1 (Y and packed UV)
         */

        p = pb_begin();
        /* Stage 0: Y plane */
        p = pb_push2(p,NV20_TCL_PRIMITIVE_3D_TX_OFFSET(0),(DWORD)texture.addr & 0x03ffffff,0x0001122a);
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_NPOT_PITCH(0),texture.pitch<<16);
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_NPOT_SIZE(0),(texture.width<<16)|texture.height);
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_WRAP(0),0x00030303);
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_ENABLE(0),0x4003ffc0);
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_FILTER(0),0x04074000);

        /* Stage 1: Packed UV plane */
        p = pb_push2(p,NV20_TCL_PRIMITIVE_3D_TX_OFFSET(1),(DWORD)uv_texture.addr & 0x03ffffff,0x0001122a);
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_NPOT_PITCH(1),uv_texture.pitch<<16);
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_NPOT_SIZE(1),(uv_texture.width<<16)|uv_texture.height);
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_WRAP(1),0x00030303);
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_ENABLE(1),0x4003ffc0);
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_FILTER(1),0x04074000);
        pb_end(p);

        /* Disable other texture stages */
        p = pb_begin();
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_ENABLE(2),0x0003ffc0);
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_ENABLE(3),0x0003ffc0);
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
        pb_print("YUV420 Demo\n");
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
    /* Generate a procedural YUV420 test pattern and upload as Y + packed-UV textures */
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
    upload_yuv420_as_two_textures(yPlane, uPlane, vPlane, w, h, strideY, strideU, strideV);

    free(yPlane);
    free(uPlane);
    free(vPlane);
}



