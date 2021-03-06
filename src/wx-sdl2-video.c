#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "wx-sdl2.h"
#include "video.h"
#include "wx-sdl2-video.h"

#include "wx-sdl2-video-gl3.h"
#include "wx-sdl2-video-renderer.h"

void video_blit_complete();

static BITMAP *buffer32_vscale;
BITMAP *screen;
SDL_Rect old_screen_rect;
SDL_Rect screen_rect;
SDL_Rect updated_rect;
SDL_Rect window_rect;
SDL_Rect blit_rect;
SDL_Rect texture_rect;
int updated = 0;

SDL_mutex* blitMutex = NULL;

static void sdl_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h);

int video_scale_mode = 1;
int video_vsync = 0;
int video_focus_dim = 0;
int video_fullscreen_mode = 0;

static sdl_render_driver sdl_render_drivers[] = {
                { RENDERER_AUTO, "auto", "Auto", 0, sdl2_renderer_create, sdl2_renderer_close, sdl2_renderer_available },
                { RENDERER_DIRECT3D, "direct3d", "Direct3D", 0, sdl2_renderer_create, sdl2_renderer_close, sdl2_renderer_available },
                { RENDERER_OPENGL, "opengl", "OpenGL", 0, sdl2_renderer_create, sdl2_renderer_close, sdl2_renderer_available },
                { RENDERER_OPENGLES2, "opengles2", "OpenGL ES 2", 0, sdl2_renderer_create, sdl2_renderer_close, sdl2_renderer_available },
                { RENDERER_OPENGLES, "opengles", "OpenGL ES", 0, sdl2_renderer_create, sdl2_renderer_close, sdl2_renderer_available },
                { RENDERER_SOFTWARE, "software", "Software", 0, sdl2_renderer_create, sdl2_renderer_close, sdl2_renderer_available },
                { RENDERER_GL3, "gl3", "OpenGL 3.0 (Experimental)", SDL_WINDOW_OPENGL, gl3_renderer_create, gl3_renderer_close, gl3_renderer_available }
};

sdl_render_driver requested_render_driver;

char current_render_driver_name[50];
static sdl_renderer_t* renderer = NULL;

void hline(BITMAP *b, int x1, int y, int x2, int col)
{
        if (y < 0 || y >= buffer32->h)
           return;

        for (; x1 < x2; x1++)
                ((uint32_t *)b->line[y])[x1] = col;
}

void blit(BITMAP *src, BITMAP *dst, int x1, int y1, int x2, int y2, int xs, int ys)
{
}

void stretch_blit(BITMAP *src, BITMAP *dst, int x1, int y1, int xs1, int ys1, int x2, int y2, int xs2, int ys2)
{
}

void rectfill(BITMAP *b, int x1, int y1, int x2, int y2, uint32_t col)
{
}

void set_palette(PALETTE p)
{
}

void destroy_bitmap(BITMAP *b)
{
        free(b);
}

BITMAP *create_bitmap(int x, int y)
{
        BITMAP *b = malloc(sizeof(BITMAP) + (y * sizeof(uint8_t *)));
        int c;
        b->dat = malloc(x * y * 4);
        for (c = 0; c < y; c++)
        {
                b->line[c] = b->dat + (c * x * 4);
        }
        b->w = x;
        b->h = y;
        return b;
}

sdl_render_driver* sdl_get_render_drivers(int* num)
{
        if (num)
                *num = SDL_arraysize(sdl_render_drivers);
        return sdl_render_drivers;
}

sdl_render_driver sdl_get_render_driver_by_name(const char* name, int def)
{
        int i;
        for (i = 0; i < SDL_arraysize(sdl_render_drivers); ++i)
        {
                if (!strcmp(sdl_render_drivers[i].sdl_id, name))
                {
                        return sdl_render_drivers[i];
                }
        }
        return sdl_render_drivers[def];
}

sdl_render_driver* sdl_get_render_driver_by_name_ptr(const char* name)
{
        int i;
        for (i = 0; i < SDL_arraysize(sdl_render_drivers); ++i)
        {
                if (!strcmp(sdl_render_drivers[i].sdl_id, name))
                {
                        return &sdl_render_drivers[i];
                }
        }
        return 0;
}

sdl_render_driver sdl_get_render_driver_by_id(int id, int def)
{
        int i;
        for (i = 0; i < SDL_arraysize(sdl_render_drivers); ++i)
        {
                if (sdl_render_drivers[i].id == id)
                {
                        return sdl_render_drivers[i];
                }
        }
        return sdl_render_drivers[def];
}


void sdl_scale(int scale, SDL_Rect src, SDL_Rect* dst, int w, int h)
{
        double t, b, l, r;
        int ratio_w, ratio_h;
        switch (scale)
        {
        case FULLSCR_SCALE_43:
                t = 0;
                b = src.h;
                l = (src.w / 2) - ((src.h * 4) / (3 * 2));
                r = (src.w / 2) + ((src.h * 4) / (3 * 2));
                if (l < 0)
                {
                        l = 0;
                        r = src.w;
                        t = (src.h / 2)
                                        - ((src.w * 3) / (4 * 2));
                        b = (src.h / 2)
                                        + ((src.w * 3) / (4 * 2));
                }
                break;
        case FULLSCR_SCALE_SQ:
                t = 0;
                b = src.h;
                l = (src.w / 2) - ((src.h * w) / (h * 2));
                r = (src.w / 2) + ((src.h * w) / (h * 2));
                if (l < 0)
                {
                        l = 0;
                        r = src.w;
                        t = (src.h / 2)
                                        - ((src.w * h) / (w * 2));
                        b = (src.h / 2)
                                        + ((src.w * h) / (w * 2));
                }
                break;
        case FULLSCR_SCALE_INT:
                ratio_w = src.w / w;
                ratio_h = src.h / h;
                if (ratio_h < ratio_w)
                        ratio_w = ratio_h;
                l = (src.w / 2) - ((w * ratio_w) / 2);
                r = (src.w / 2) + ((w * ratio_w) / 2);
                t = (src.h / 2) - ((h * ratio_w) / 2);
                b = (src.h / 2) + ((h * ratio_w) / 2);
                break;
        case FULLSCR_SCALE_FULL:
        default:
                l = 0;
                t = 0;
                r = src.w;
                b = src.h;
                break;
        }

        dst->x = l;
        dst->y = t;
        dst->w = r - l;
        dst->h = b - t;
}

static void set_updated_size(int x, int y, int w, int h)
{
        if (updated)
        {
                updated_rect.x = x < updated_rect.x ? x : updated_rect.x;
                updated_rect.y = y < updated_rect.y ? y : updated_rect.y;
                updated_rect.w = w > updated_rect.w ? w : updated_rect.w;
                updated_rect.h = h > updated_rect.h ? h : updated_rect.h;
        }
        else
        {
                updated_rect.x = x;
                updated_rect.y = y;
                updated_rect.w = w;
                updated_rect.h = h;
                updated = 1;
        }
}

static void sdl_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
        if (y1 == y2)
        {
                video_blit_complete();
                return; /*Nothing to do*/
        }

        int xx, yy;
        SDL_Rect rect;
        SDL_LockMutex(blitMutex);
        for (yy = y1; yy < y2; yy++)
        {
                if ((y + yy) >= 0 && (y + yy) < buffer32->h)
                        memcpy(screen->dat + (yy * screen->w * 4),
                                        &(((uint32_t *) buffer32->line[y + yy])[x]), w * 4);
        }
        set_updated_size(0, y1, w, y2 - y1);
//        set_updated_size(0, 0, w, h);
        blit_rect.w = w;
        blit_rect.h = h;
        SDL_UnlockMutex(blitMutex);
        video_blit_complete();
}

int sdl_is_fullscreen(SDL_Window* window) {
        int flags = SDL_GetWindowFlags(window);
        return (flags&SDL_WINDOW_FULLSCREEN) || (flags&SDL_WINDOW_FULLSCREEN_DESKTOP);
}

int sdl_video_init()
{
        blitMutex = SDL_CreateMutex();
        updated = 0;

        video_blit_memtoscreen_func = sdl_blit_memtoscreen;
        requested_render_driver = sdl_get_render_driver_by_id(RENDERER_AUTO, RENDERER_AUTO);

        buffer32_vscale = create_bitmap(2048, 2048);
        screen = create_bitmap(2048, 2048);

        return SDL_TRUE;
}

void sdl_video_close()
{
        requested_render_driver.renderer_close(renderer);
        renderer = NULL;
        destroy_bitmap(buffer32_vscale);
        destroy_bitmap(screen);
        SDL_DestroyMutex(blitMutex);
}

int sdl_renderer_init(SDL_Window* window)
{
        renderer = requested_render_driver.renderer_create();
        return renderer->init(window, requested_render_driver, screen);
}

void sdl_renderer_close()
{
        if (renderer)
                renderer->close();
        renderer = NULL;
}

int sdl_renderer_update(SDL_Window* window)
{
        int render = 0;
        SDL_LockMutex(blitMutex);
        if (updated)
        {
                updated = 0;
                renderer->update(window, updated_rect, screen);
                texture_rect.w = blit_rect.w;
                texture_rect.h = blit_rect.h;
                render = 1;
        }
        SDL_UnlockMutex(blitMutex);
        return render || renderer->always_update;
}

void sdl_renderer_present(SDL_Window* window)
{
        SDL_Rect wr;
        int l, t, r, b;
        SDL_GetWindowSize(window, &wr.w, &wr.h);
        sdl_scale(video_fullscreen_scale, wr, &wr, texture_rect.w, texture_rect.h);
        renderer->present(window, texture_rect, wr, screen);

}
