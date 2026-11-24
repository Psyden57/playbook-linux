/*
 * pb-egltest — minimal presenting EGL/GLES2 Wayland client.
 *
 * Renders a known colour into an EGL window surface, reads the drawable back
 * with glReadPixels BEFORE the swap (this reads the same DRI image Mesa hands
 * to the compositor through wl_drm PRIME), prints a summary, then swaps and
 * keeps repainting so a screenshot can catch it.
 *
 * Usage: pb-egltest [width height] [seconds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include "xdg-shell-client-protocol.h"

#ifndef EGL_PLATFORM_WAYLAND_KHR
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#endif

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct xdg_wm_base *wm_base;
static struct wl_surface *surface;
static struct xdg_surface *xdg_surf;
static struct xdg_toplevel *toplevel;
static int configured;
static int cfg_w, cfg_h;

static void wm_ping(void *d, struct xdg_wm_base *b, uint32_t serial)
{
    (void)d;
    xdg_wm_base_pong(b, serial);
}

static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };

static void surf_configure(void *d, struct xdg_surface *s, uint32_t serial)
{
    (void)d;
    xdg_surface_ack_configure(s, serial);
    configured = 1;
}

static const struct xdg_surface_listener surf_listener = { .configure = surf_configure };

static void top_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h,
                          struct wl_array *states)
{
    (void)d; (void)t; (void)states;
    cfg_w = w;
    cfg_h = h;
}

static void top_close(void *d, struct xdg_toplevel *t)
{
    (void)d; (void)t;
    configured = 0;
}

static void top_bounds(void *d, struct xdg_toplevel *t, int32_t w, int32_t h)
{
    (void)d; (void)t; (void)w; (void)h;
}

static const struct xdg_toplevel_listener top_listener = {
    .configure = top_configure,
    .close = top_close,
};

static void registry_global(void *d, struct wl_registry *r, uint32_t name,
                            const char *iface, uint32_t version)
{
    (void)d;
    if (!strcmp(iface, "wl_compositor"))
        compositor = wl_registry_bind(r, name, &wl_compositor_interface,
                                      version < 4 ? version : 4);
    else if (!strcmp(iface, "xdg_wm_base"))
        wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface,
                                   version < 1 ? version : 1);
}

static void registry_remove(void *d, struct wl_registry *r, uint32_t name)
{
    (void)d; (void)r; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_remove,
};

static void summarize(const char *tag, int w, int h)
{
    unsigned char px[8 * 8 * 4];
    memset(px, 0, sizeof(px));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, px);
    int nz = 0, mx = 0, a0 = 0;
    for (int i = 0; i < 8 * 8 * 4; i += 4) {
        if (px[i] | px[i + 1] | px[i + 2]) nz++;
        int m = px[i]; if (px[i + 1] > m) m = px[i + 1]; if (px[i + 2] > m) m = px[i + 2];
        if (m > mx) mx = m;
        if (px[i + 3] == 0) a0++;
    }
    printf("%s %dx%d: first=(%d,%d,%d,%d) nz=%d/64 max=%d a0=%d glerr=0x%x\n",
           tag, w, h, px[0], px[1], px[2], px[3], nz, mx, a0, glGetError());
    fflush(stdout);
}

static void print_dri_maps(void)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512];
    char seen[256] = "";
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        const char *tags[] = { "pvr_dri", "swrast_dri", "kms_swrast_dri" };
        for (int i = 0; i < 3; i++)
            if (strstr(line, tags[i]) && !strstr(seen, tags[i])) {
                strncat(seen, tags[i], sizeof(seen) - strlen(seen) - 1);
                strncat(seen, " ", sizeof(seen) - strlen(seen) - 1);
            }
    }
    fclose(f);
    printf("dri mapped: %s\n", seen[0] ? seen : "(none)");
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int w = argc > 1 ? atoi(argv[1]) : 512;
    int h = argc > 2 ? atoi(argv[2]) : 300;
    int secs = argc > 3 ? atoi(argv[3]) : 60;

    display = wl_display_connect(NULL);
    if (!display) { fprintf(stderr, "wl_display_connect failed (%s)\n",
                            getenv("WAYLAND_DISPLAY")); return 1; }
    struct wl_registry *reg = wl_display_get_registry(display);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !wm_base) {
        fprintf(stderr, "missing compositor/xdg_wm_base\n");
        return 1;
    }
    xdg_wm_base_add_listener(wm_base, &wm_listener, NULL);

    surface = wl_compositor_create_surface(compositor);
    xdg_surf = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg_surf, &surf_listener, NULL);
    toplevel = xdg_surface_get_toplevel(xdg_surf);
    xdg_toplevel_add_listener(toplevel, &top_listener, NULL);
    xdg_toplevel_set_title(toplevel, "pb-egltest");
    xdg_toplevel_set_app_id(toplevel, "pb-egltest");
    wl_surface_commit(surface);

    while (!configured)
        if (wl_display_dispatch(display) < 0) {
            fprintf(stderr, "dispatch failed before configure\n");
            return 1;
        }
    if (cfg_w > 0) w = cfg_w;
    if (cfg_h > 0) h = cfg_h;
    printf("configured %dx%d\n", w, h);
    fflush(stdout);

    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display, NULL);
    if (!dpy || !eglInitialize(dpy, NULL, NULL)) {
        fprintf(stderr, "eglInitialize failed 0x%x\n", eglGetError());
        return 1;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint cfg_attr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint ncfg = 0;
    eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg);
    if (!ncfg) { fprintf(stderr, "no EGL config\n"); return 1; }
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    struct wl_egl_window *ew = wl_egl_window_create(surface, w, h);
    EGLSurface es = eglCreateWindowSurface(dpy, cfg, ew, NULL);
    if (!ctx || !es || !eglMakeCurrent(dpy, es, es, ctx)) {
        fprintf(stderr, "context/surface failed 0x%x\n", eglGetError());
        return 1;
    }
    printf("GL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\n",
           glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    print_dri_maps();
    fflush(stdout);

    glViewport(0, 0, w, h);
    glDisable(GL_BLEND);
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);   /* magenta */
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    summarize("pre-swap ", w, h);
    printf("swap=%d err=0x%x\n", eglSwapBuffers(dpy, es), eglGetError());
    fflush(stdout);

    /* Keep repainting (alternating colours) so a screenshot can catch it. */
    time_t end = time(NULL) + secs;
    int flip = 0;
    while (time(NULL) < end) {
        glClearColor(flip ? 0.0f : 1.0f, flip ? 1.0f : 0.0f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        if ((time(NULL) % 2) == 0)
            summarize("loop     ", w, h);
        eglSwapBuffers(dpy, es);
        wl_display_dispatch_pending(display);
        wl_display_flush(display);
        usleep(300000);
        flip = !flip;
    }
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return 0;
}
