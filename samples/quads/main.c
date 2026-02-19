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

/*
//For texture_simple.h
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

//For texture_320x240.h
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

//For texture_640x480.h
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
static void set_attrib_pointer(unsigned int index, unsigned int format, unsigned int size, unsigned int stride, const void* data);
static void draw_arrays(unsigned int mode, int start, int count);

/* Main program function */
int main(void)
{
    uint32_t *p;
    int       i, status;
    int       width, height;
    int       start, last, now;
    int       fps, frames, frames_total;

    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

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
         * Setup texture stages
        */

        /* Enable texture stage 0 */
        /* FIXME: Use constants instead of the hardcoded values below */
        /*
        * Note to self on scaling:
        *   x04074000 = Linear filtering (with AA)
        *   0x02022000 = Presumably nearest neighbor or point sampling (no AA)
        */
        p = pb_begin();
        p = pb_push2(p,NV20_TCL_PRIMITIVE_3D_TX_OFFSET(0),(DWORD)texture.addr & 0x03ffffff,0x0001122a); //set stage 0 texture address & format
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_NPOT_PITCH(0),texture.pitch<<16); //set stage 0 texture pitch (pitch<<16)
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_NPOT_SIZE(0),(texture.width<<16)|texture.height); //set stage 0 texture width & height ((witdh<<16)|height)
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_WRAP(0),0x00030303);//set stage 0 texture modes (0x0W0V0U wrapping: 1=wrap 2=mirror 3=clamp 4=border 5=clamp to edge)
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_ENABLE(0),0x4003ffc0); //set stage 0 texture enable flags
        //p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_FILTER(0),0x04074000); //set stage 0 texture filters (AA!)
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_FILTER(0),0x02022000); //set stage 0 texture filters (no AA - nearest neighbor)
        pb_end(p);

        /* Disable other texture stages */
        p = pb_begin();
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_ENABLE(1),0x0003ffc0);//set stage 1 texture enable flags (bit30 disabled)
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_ENABLE(2),0x0003ffc0);//set stage 2 texture enable flags (bit30 disabled)
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_ENABLE(3),0x0003ffc0);//set stage 3 texture enable flags (bit30 disabled)
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_WRAP(1),0x00030303);//set stage 1 texture modes (0x0W0V0U wrapping: 1=wrap 2=mirror 3=clamp 4=border 5=clamp to edge)
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_WRAP(2),0x00030303);//set stage 2 texture modes (0x0W0V0U wrapping: 1=wrap 2=mirror 3=clamp 4=border 5=clamp to edge)
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_WRAP(3),0x00030303);//set stage 3 texture modes (0x0W0V0U wrapping: 1=wrap 2=mirror 3=clamp 4=border 5=clamp to edge)
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_FILTER(1),0x02022000);//set stage 1 texture filters (no AA, stage not even used)
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_FILTER(2),0x02022000);//set stage 2 texture filters (no AA, stage not even used)
        p = pb_push1(p,NV20_TCL_PRIMITIVE_3D_TX_FILTER(3),0x02022000);//set stage 3 texture filters (no AA, stage not even used)
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
    texture.width = texture_width;
    texture.height = texture_height;
    texture.pitch = texture.width*4;
    texture.addr = MmAllocateContiguousMemoryEx(texture.pitch*texture.height, 0, MAXRAM, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    memcpy(texture.addr, texture_rgba, sizeof(texture_rgba));
}

