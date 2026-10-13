/* glcore-interpose: make Wine's winemac.drv hand out an OpenGL 4 *core* CGL context when a
 * Windows app asks for a legacy WGL context (attrib_list NULL). On macOS a legacy context is
 * GL 2.1; Windows/Linux drivers return their newest compatibility version instead, which is what
 * glutin/egui_glow-based apps (e.g. Project Reclaimer) expect.
 *
 * Only converts the CGLChoosePixelFormat call made by winemac.drv's create_context() for an
 * accelerated renderer (attribs contain kCGLPFAMinimumPolicy + kCGLPFAAccelerated and NO
 * kCGLPFARendererID / kCGLPFAOpenGLProfile). The driver's startup probe ({DisplayMask} only),
 * its pixel-format enumeration and renderer queries (all carry kCGLPFARendererID) are untouched,
 * so Wine's gl_info stays correct. Aux/accum/pbuffer attributes are dropped (illegal in core).
 *
 * Usage: DYLD_INSERT_LIBRARIES=/path/libglcore_interpose.dylib GLCORE_INTERPOSE=1 wine app.exe
 *   GLCORE_INTERPOSE_PROFILE=32|41 (default 41), GLCORE_INTERPOSE_LOG=1 for stderr logging.
 */
#include <OpenGL/OpenGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int env_on(const char *n) { const char *e = getenv(n); return e && *e == '1'; }

static int takes_value(CGLPixelFormatAttribute a)
{
    switch (a) {
    case kCGLPFAColorSize: case kCGLPFAAlphaSize: case kCGLPFADepthSize: case kCGLPFAStencilSize:
    case kCGLPFAAccumSize: case kCGLPFASampleBuffers: case kCGLPFASamples: case kCGLPFAAuxBuffers:
    case kCGLPFARendererID: case kCGLPFADisplayMask: case kCGLPFAOpenGLProfile:
    case kCGLPFAVirtualScreenCount:
        return 1;
    default: return 0;
    }
}

CGLError my_CGLChoosePixelFormat(const CGLPixelFormatAttribute *attribs, CGLPixelFormatObj *pix, GLint *npix)
{
    int has_min = 0, has_accel = 0, has_renderer = 0, has_profile = 0, n = 0;
    CGLPixelFormatAttribute buf[128];

    if (!env_on("GLCORE_INTERPOSE") || !attribs) return CGLChoosePixelFormat(attribs, pix, npix);

    for (int i = 0; attribs[i] != 0; i++) {
        CGLPixelFormatAttribute a = attribs[i];
        if (a == kCGLPFAMinimumPolicy) has_min = 1;
        if (a == kCGLPFAAccelerated) has_accel = 1;
        if (a == kCGLPFARendererID) has_renderer = 1;
        if (a == kCGLPFAOpenGLProfile) has_profile = 1;
        if (takes_value(a)) i++;
    }
    if (!(has_min && has_accel && !has_renderer && !has_profile))
        return CGLChoosePixelFormat(attribs, pix, npix);

    for (int i = 0; attribs[i] != 0 && n < 120; i++) {
        CGLPixelFormatAttribute a = attribs[i];
        int tv = takes_value(a);
        if (a == kCGLPFAAuxBuffers || a == kCGLPFAAccumSize || a == kCGLPFAPBuffer) { if (tv) i++; continue; }
        buf[n++] = a;
        if (tv) buf[n++] = attribs[++i];
    }
    const char *prof = getenv("GLCORE_INTERPOSE_PROFILE");
    buf[n++] = kCGLPFAOpenGLProfile;
    buf[n++] = (prof && !strcmp(prof, "32")) ? (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core
                                             : (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core;
    buf[n] = 0;

    CGLError err = CGLChoosePixelFormat(buf, pix, npix);
    if (env_on("GLCORE_INTERPOSE_LOG"))
        fprintf(stderr, "[glcore-interpose] converted legacy request to core profile: err=%d npix=%d\n", (int)err, npix ? (int)*npix : -1);
    if (err != kCGLNoError || !*pix) {
        err = CGLChoosePixelFormat(attribs, pix, npix);
        if (env_on("GLCORE_INTERPOSE_LOG")) fprintf(stderr, "[glcore-interpose] core failed, fell back to legacy: err=%d\n", (int)err);
    }
    return err;
}

__attribute__((used)) static struct { const void *replacement, *replacee; }
_interpose_cgl __attribute__((section("__DATA,__interpose"))) = { (const void *)my_CGLChoosePixelFormat, (const void *)CGLChoosePixelFormat };

/* ---- GLSL version rewrite: macOS core profiles accept only #version 150+. Rewrite 130/140 -> 150. ---- */
#include <OpenGL/gl3.h>
void my_glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)
{
    if (!env_on("GLCORE_INTERPOSE") || count <= 0 || !string) { glShaderSource(shader, count, string, length); return; }
    size_t total = 0;
    for (GLsizei i = 0; i < count; i++) total += (length && length[i] >= 0) ? (size_t)length[i] : (string[i] ? strlen(string[i]) : 0);
    char *src = malloc(total + 32); if (!src) { glShaderSource(shader, count, string, length); return; }
    size_t off = 0;
    for (GLsizei i = 0; i < count; i++) {
        size_t l = (length && length[i] >= 0) ? (size_t)length[i] : (string[i] ? strlen(string[i]) : 0);
        if (l) memcpy(src + off, string[i], l); off += l;
    }
    src[off] = 0;
    int changed = 0;
    for (const char *v; (v = strstr(src, "#version 1")); ) {
        if (!strncmp(v, "#version 130", 12) || !strncmp(v, "#version 140", 12)) { memcpy((char *)v, "#version 150", 12); changed = 1; }
        else break;
    }
    if (env_on("GLCORE_INTERPOSE_LOG")) {
        const char *v = strstr(src, "#version");
        fprintf(stderr, "[glcore-interpose] glShaderSource shader=%u len=%zu version=%.14s%s\n", shader, off, v ? v : "(none)", changed ? " (rewritten)" : "");
    }
    const GLchar *one = src; GLint onelen = (GLint)off;
    glShaderSource(shader, 1, &one, &onelen);
    free(src);
}
__attribute__((used)) static struct { const void *replacement, *replacee; }
_interpose_gss __attribute__((section("__DATA,__interpose"))) = { (const void *)my_glShaderSource, (const void *)glShaderSource };

/* ---- winemac.drv main-thread self-deadlock workaround ----
 * winemac's viewWillDraw -> macdrv_update_opengl_context -> OnMainThread(^{[context update];})
 * can run ON the main thread (seen with a D3DMetal swap chain + a late-attached GL context on the
 * same window). OnMainThread then queues the block for the main thread and waits on a dispatch
 * semaphore forever -> the Cocoa main thread beachballs and every Wine GL thread that needs the
 * main thread hangs with it. We catch a forever-wait on the main thread whose caller is
 * winemac.so and pump the run loop while waiting, so the queued block runs and signals us.
 * Disable with GLCORE_INTERPOSE_PUMP=0. */
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <pthread.h>
#include <CoreFoundation/CoreFoundation.h>

static int caller_is_winemac(const void *ret)
{
    Dl_info info;
    return dladdr(ret, &info) && info.dli_fname && strstr(info.dli_fname, "winemac.so");
}

intptr_t my_dispatch_semaphore_wait(dispatch_semaphore_t sem, dispatch_time_t timeout)
{
    const char *pump = getenv("GLCORE_INTERPOSE_PUMP");
    if (timeout != DISPATCH_TIME_FOREVER || !pthread_main_np() || (pump && *pump == '0')
        || !caller_is_winemac(__builtin_return_address(0)))
        return dispatch_semaphore_wait(sem, timeout);

    static int logged;
    if (env_on("GLCORE_INTERPOSE_LOG") && !logged++)
        fprintf(stderr, "[glcore-interpose] winemac forever-wait on main thread: pumping run loop instead\n");
    for (;;) {
        if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_MSEC)) == 0)
            return 0;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, true);
    }
}
__attribute__((used)) static struct { const void *replacement, *replacee; }
_interpose_dsw __attribute__((section("__DATA,__interpose"))) = { (const void *)my_dispatch_semaphore_wait, (const void *)dispatch_semaphore_wait };
