#include "raylib.h"
#include "postfx.h"

// Postprocess pass for v2: render the world to an offscreen RT, then draw it
// to the backbuffer through a shader that posterizes (color quantization),
// dithers (Bayer 4x4), and applies a slight warm tint. The look is borrowed
// from Dungeon of Quake's postprocess.frag, simplified for clarity.

static RenderTexture2D g_rt;
static Shader          g_post;
static int             g_tintLoc;
static int             g_curW, g_curH;
static bool            g_inited;

static const char *kPostFxFs =
    "#version 330 core\n"
    "in vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec3 tintColor;\n"
    "out vec4 finalColor;\n"
    "const float kBayer[16] = float[16](\n"
    "   0.0,  8.0,  2.0, 10.0,\n"
    "  12.0,  4.0, 14.0,  6.0,\n"
    "   3.0, 11.0,  1.0,  9.0,\n"
    "  15.0,  7.0, 13.0,  5.0);\n"
    "void main(){\n"
    "  vec3 col = texture(texture0, fragTexCoord).rgb * tintColor;\n"
    "  ivec2 fc = ivec2(gl_FragCoord.xy);\n"
    "  float d = (kBayer[(fc.x & 3) * 4 + (fc.y & 3)] / 16.0) - 0.5;\n"
    "  col += vec3(d * (1.0/24.0));\n"
    "  // Posterize at 10 levels, gamma 0.85 — done with explicit lerp so\n"
    "  // the cost is two pow() per fragment rather than three.\n"
    "  col = pow(col, vec3(0.85));\n"
    "  col = floor(col * 10.0) * (1.0/10.0);\n"
    "  col = pow(col, vec3(1.0/0.85));\n"
    "  finalColor = vec4(clamp(col, 0.0, 1.0), 1.0);\n"
    "}\n";

void PostFxInit(int w, int h) {
    if (g_inited) PostFxShutdown();
    g_rt   = LoadRenderTexture(w, h);
    g_post = LoadShaderFromMemory(0, kPostFxFs);
    g_tintLoc = GetShaderLocation(g_post, "tintColor");
    float tint[3] = {1.05f, 0.95f, 0.85f};
    SetShaderValue(g_post, g_tintLoc, tint, SHADER_UNIFORM_VEC3);
    g_curW = w; g_curH = h;
    g_inited = true;
}

void PostFxShutdown(void) {
    if (!g_inited) return;
    UnloadRenderTexture(g_rt);
    UnloadShader(g_post);
    g_inited = false;
}

static void EnsureSize(int w, int h) {
    if (w == g_curW && h == g_curH) return;
    UnloadRenderTexture(g_rt);
    g_rt = LoadRenderTexture(w, h);
    g_curW = w; g_curH = h;
}

void PostFxBeginCapture(void) {
    if (!g_inited) { BeginDrawing(); return; }  // graceful fallback
    int w = GetScreenWidth(), h = GetScreenHeight();
    EnsureSize(w, h);
    BeginTextureMode(g_rt);
}

void PostFxEndCapture(void) {
    if (!g_inited) { EndDrawing(); return; }
    EndTextureMode();
    BeginDrawing();
    ClearBackground(BLACK);
    BeginShaderMode(g_post);
    // raylib RenderTextures are stored upside-down; negative source-h flips Y.
    Rectangle src = { 0, 0, (float)g_rt.texture.width, -(float)g_rt.texture.height };
    Rectangle dst = { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() };
    DrawTexturePro(g_rt.texture, src, dst, (Vector2){0,0}, 0.0f, WHITE);
    EndShaderMode();
    EndDrawing();
}
