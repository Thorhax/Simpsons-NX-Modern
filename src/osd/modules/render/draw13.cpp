// license:BSD-3-Clause
// copyright-holders: Couriersud, Olivier Galibert, R. Belmont
//============================================================
//
//  draw13.cpp - SDL 2.0 drawing implementation
//
//  SDLMAME by Olivier Galibert and R. Belmont
//
//  SDL 2.0 renderer by Couriersud
//
//============================================================

#include "render_module.h"

#include "modules/osdmodule.h"

#if defined(OSD_SDL) && !defined(SDLMAME_SDL3)

// OSD headers
#include "sdlopts.h"
#include "window.h"

// lib/util
#include "options.h"

// emu
#include "emucore.h"
#include "render.h"

// standard SDL headers
#include <SDL2/SDL.h>

#if defined(__SWITCH__)
#include "nxperf.h"
#include "nxpicker.h"
#endif

// standard C headers
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <list>


namespace osd {

namespace {

struct quad_setup_data
{
	quad_setup_data() = default;

	void compute(const render_primitive &prim, const int prescale);

	int32_t   dudx = 0, dvdx = 0, dudy = 0, dvdy = 0;
	int32_t   startu = 0, startv = 0;
	int32_t   rotwidth = 0, rotheight = 0;
};

//============================================================
//  Textures
//============================================================

class renderer_sdl2;
struct copy_info_t;

/* texture_info holds information about a texture */
class texture_info
{
public:
	texture_info(renderer_sdl2 *renderer, const render_texinfo &texsource, const quad_setup_data &setup, const uint32_t flags);
	~texture_info();

	void set_data(const render_texinfo &texsource, const uint32_t flags);
	void render_quad(const render_primitive &prim, const int x, const int y);
	bool matches(const render_primitive &prim, const quad_setup_data &setup);

	copy_info_t const *compute_size_type();

	void                *m_pixels;            // pixels for the texture
	int                 m_pitch;

	copy_info_t const   *m_copyinfo;
	quad_setup_data     m_setup;

	osd_ticks_t         m_last_access;

	int raw_width() const { return m_texinfo.width; }
	int raw_height() const { return m_texinfo.height; }

	const render_texinfo &texinfo() const { return m_texinfo; }
	render_texinfo &texinfo() { return m_texinfo; }

	HashT hash() const { return m_hash; }
	uint32_t flags() const { return m_flags; }

private:
	bool is_pixels_owned() const;

	void set_coloralphamode(SDL_Texture *texture_id, const render_color *color);

	Uint32              m_sdl_access;
	renderer_sdl2 *     m_renderer;
	render_texinfo      m_texinfo;            // copy of the texture info
	HashT               m_hash;               // hash value for the texture (must be >= pointer size)
	uint32_t            m_flags;              // rendering flags

	SDL_Texture *       m_texture_id;
	bool                m_is_rotated;

	int                 m_format;             // texture format
	SDL_BlendMode       m_sdl_blendmode;
};

// inline functions and macros
#include "blit13.ipp"

//============================================================
//  TEXCOPY FUNCS
//============================================================

enum SDL_TEXFORMAT_E
{
	SDL_TEXFORMAT_ARGB32 = 0,
	SDL_TEXFORMAT_RGB32,
	SDL_TEXFORMAT_RGB32_PALETTED,
	SDL_TEXFORMAT_YUY16,
	SDL_TEXFORMAT_YUY16_PALETTED,
	SDL_TEXFORMAT_PALETTE16,
	SDL_TEXFORMAT_RGB15,
	SDL_TEXFORMAT_RGB15_PALETTED,
	SDL_TEXFORMAT_PALETTE16A,
	SDL_TEXFORMAT_PALETTE16_ARGB1555,
	SDL_TEXFORMAT_RGB15_ARGB1555,
	SDL_TEXFORMAT_RGB15_PALETTED_ARGB1555,
	SDL_TEXFORMAT_LAST = SDL_TEXFORMAT_RGB15_PALETTED_ARGB1555
};

struct copy_info_t
{
	int                 src_fmt;
	Uint32              dst_fmt;
	const blit_base     *blitter;
	Uint32              bm_mask;
	const char          *srcname;
	const char          *dstname;
	/* Statistics */
	mutable uint64_t    pixel_count;
	mutable int64_t     time;
	mutable int         samples;
	mutable int         perf;
	/* list */
	copy_info_t const   *next;
};

/* renderer_sdl2 is the information about SDL for the current screen */
class renderer_sdl2 : public osd_renderer
{
public:
	renderer_sdl2(
			osd_window &window,
			copy_info_t const *const (&blit_info)[SDL_TEXFORMAT_LAST + 1]);

	virtual ~renderer_sdl2()
	{
		destroy_all_textures();
		SDL_DestroyRenderer(m_sdl_renderer);
		m_sdl_renderer = nullptr;
	}

	virtual int create() override;
	virtual int draw(const int update) override;
	virtual int xy_to_render_target(const int x, const int y, int *xt, int *yt) override;
	virtual render_primitive_list *get_primitives() override;

	int RendererSupportsFormat(Uint32 format, Uint32 access, const char *sformat);

	SDL_Renderer *m_sdl_renderer;
	copy_info_t const *const (&m_blit_info)[SDL_TEXFORMAT_LAST + 1];

private:
	void render_quad(texture_info *texture, const render_primitive &prim, const int x, const int y);

	texture_info *texture_find(const render_primitive &prim, const quad_setup_data &setup);
	texture_info *texture_update(const render_primitive &prim);

	void destroy_all_textures();

	int32_t         m_blittimer;

	std::list<texture_info> m_texlist;                // list of active textures

	float           m_last_hofs;
	float           m_last_vofs;

	int             m_width;
	int             m_height;

	osd_dim         m_blit_dim;

	struct
	{
		Uint32  format;
		int     status;
	} fmt_support[30];

	// Stats
	int64_t         m_last_blit_time;
	int64_t         m_last_blit_pixels;
};


//============================================================
//  CONSTANTS
//============================================================

#define STAT_PIXEL_THRESHOLD (150*150)

enum
{
	TEXTURE_TYPE_NONE,
	TEXTURE_TYPE_PLAIN,
	TEXTURE_TYPE_SURFACE
};


//============================================================
//  Inline functions
//============================================================

static inline bool is_opaque(const float &a)
{
	return (a >= 1.0f);
}

static inline bool is_transparent(const float &a)
{
	return (a <  0.0001f);
}

//============================================================
//  CONSTRUCTOR & DESTRUCTOR
//============================================================

renderer_sdl2::renderer_sdl2(
		osd_window &window,
		copy_info_t const *const (&blit_info)[SDL_TEXFORMAT_LAST + 1])
	: osd_renderer(window)
	, m_sdl_renderer(nullptr)
	, m_blit_info(blit_info)
	, m_blittimer(0)
	, m_last_hofs(0)
	, m_last_vofs(0)
	, m_width(0)
	, m_height(0)
	, m_blit_dim(0, 0)
	, m_last_blit_time(0)
	, m_last_blit_pixels(0)
{
	for (int i = 0; i < 30; i++)
	{
		fmt_support[i].format = 0;
		fmt_support[i].status = 0;
	}
}

//============================================================
//  INLINES
//============================================================


static inline float round_nearest(float f)
{
	return floor(f + 0.5f);
}

static inline HashT texture_compute_hash(const render_texinfo &texture, const uint32_t flags)
{
	return (HashT)texture.base ^ (flags & (PRIMFLAG_BLENDMODE_MASK | PRIMFLAG_TEXFORMAT_MASK));
}

static inline SDL_BlendMode map_blendmode(const int blendmode)
{
	switch (blendmode)
	{
		case BLENDMODE_NONE:
			return SDL_BLENDMODE_NONE;
		case BLENDMODE_ALPHA:
			return SDL_BLENDMODE_BLEND;
		case BLENDMODE_RGB_MULTIPLY:
			return SDL_BLENDMODE_MOD;
		case BLENDMODE_ADD:
			return SDL_BLENDMODE_ADD;
		default:
			osd_printf_warning("Unknown Blendmode %d", blendmode);
	}
	return SDL_BLENDMODE_NONE;
}

void texture_info::set_coloralphamode(SDL_Texture *texture_id, const render_color *color)
{
	uint32_t sr = (uint32_t)(255.0f * color->r);
	uint32_t sg = (uint32_t)(255.0f * color->g);
	uint32_t sb = (uint32_t)(255.0f * color->b);
	uint32_t sa = (uint32_t)(255.0f * color->a);


	if (color->r >= 1.0f && color->g >= 1.0f && color->b >= 1.0f && is_opaque(color->a))
	{
		SDL_SetTextureColorMod(texture_id, 0xFF, 0xFF, 0xFF);
		SDL_SetTextureAlphaMod(texture_id, 0xFF);
	}
	/* coloring-only case */
	else if (is_opaque(color->a))
	{
		SDL_SetTextureColorMod(texture_id, sr, sg, sb);
		SDL_SetTextureAlphaMod(texture_id, 0xFF);
	}
	/* alpha and/or coloring case */
	else if (!is_transparent(color->a))
	{
		SDL_SetTextureColorMod(texture_id, sr, sg, sb);
		SDL_SetTextureAlphaMod(texture_id, sa);
	}
	else
	{
		SDL_SetTextureColorMod(texture_id, 0xFF, 0xFF, 0xFF);
		SDL_SetTextureAlphaMod(texture_id, 0x00);
	}
}

void texture_info::render_quad(const render_primitive &prim, const int x, const int y)
{
	SDL_Rect target_rect;

	target_rect.x = x;
	target_rect.y = y;
	target_rect.w = round_nearest(prim.bounds.x1) - round_nearest(prim.bounds.x0);
	target_rect.h = round_nearest(prim.bounds.y1) - round_nearest(prim.bounds.y0);

	SDL_SetTextureBlendMode(m_texture_id, m_sdl_blendmode);
	set_coloralphamode(m_texture_id, &prim.color);
	//printf("%d %d %d %d\n", target_rect.x, target_rect.y, target_rect.w, target_rect.h);
	// Arghhh .. Just another bug. SDL_RenderCopy has severe issues with scaling ...
#if defined(__SWITCH__)
	if (SDL_RenderCopy(m_renderer->m_sdl_renderer,  m_texture_id, nullptr, &target_rect) < 0)
		nxperf::sdl_fail.add(0);
#else
	SDL_RenderCopy(m_renderer->m_sdl_renderer,  m_texture_id, nullptr, &target_rect);
#endif
	//SDL_RenderCopyEx(m_renderer->m_sdl_renderer,  m_texture_id, nullptr, &target_rect, 0, nullptr, SDL_FLIP_NONE);
	//SDL_RenderCopyEx(m_renderer->m_sdl_renderer,  m_texture_id, nullptr, nullptr, 0, nullptr, SDL_FLIP_NONE);
}

void renderer_sdl2::render_quad(texture_info *texture, const render_primitive &prim, const int x, const int y)
{
	SDL_Rect target_rect;

	target_rect.x = x;
	target_rect.y = y;
	target_rect.w = round_nearest(prim.bounds.x1 - prim.bounds.x0);
	target_rect.h = round_nearest(prim.bounds.y1 - prim.bounds.y0);

	if (texture)
	{
		copy_info_t const *copyinfo = texture->m_copyinfo;
		copyinfo->time -= osd_ticks();
		texture->render_quad(prim, x, y);
		copyinfo->time += osd_ticks();

		copyinfo->pixel_count += std::max(STAT_PIXEL_THRESHOLD , (texture->raw_width() * texture->raw_height()));
		if (m_last_blit_pixels)
		{
			copyinfo->time += (m_last_blit_time * (int64_t) (texture->raw_width() * texture->raw_height())) / (int64_t) m_last_blit_pixels;
		}
		copyinfo->samples++;
		copyinfo->perf = (texture->m_copyinfo->pixel_count * (osd_ticks_per_second()/1000)) / std::max<int64_t>(texture->m_copyinfo->time, 1);
	}
	else
	{
		uint32_t sr = (uint32_t)(255.0f * prim.color.r);
		uint32_t sg = (uint32_t)(255.0f * prim.color.g);
		uint32_t sb = (uint32_t)(255.0f * prim.color.b);
		uint32_t sa = (uint32_t)(255.0f * prim.color.a);

		SDL_SetRenderDrawBlendMode(m_sdl_renderer, map_blendmode(PRIMFLAG_GET_BLENDMODE(prim.flags)));
		SDL_SetRenderDrawColor(m_sdl_renderer, sr, sg, sb, sa);
		SDL_RenderFillRect(m_sdl_renderer, &target_rect);
	}
}

int renderer_sdl2::RendererSupportsFormat(Uint32 format, Uint32 access, const char *sformat)
{
	int i;
	for (i = 0; fmt_support[i].format != 0; i++)
	{
		if (format == fmt_support[i].format)
		{
			return fmt_support[i].status;
		}
	}
	/* not tested yet */
	fmt_support[i].format = format;
	fmt_support[i + 1].format = 0;
	SDL_Texture *texid = SDL_CreateTexture(m_sdl_renderer, format, access, 16, 16);
	if (texid)
	{
		fmt_support[i].status = 1;
		SDL_DestroyTexture(texid);
		return 1;
	}
	osd_printf_verbose("Pixelformat <%s> error %s \n", sformat, SDL_GetError());
	osd_printf_verbose("Pixelformat <%s> not supported\n", sformat);
	fmt_support[i].status = 0;
	return 0;
}


//============================================================
//  sdl_info::create
//============================================================

static void drawsdl_show_info(struct SDL_RendererInfo *render_info)
{
#define RF_ENTRY(x) {x, #x }
	static struct {
		int flag;
		const char *name;
	} rflist[] =
		{
#if 0
			RF_ENTRY(SDL_RENDERER_SINGLEBUFFER),
			RF_ENTRY(SDL_RENDERER_PRESENTCOPY),
			RF_ENTRY(SDL_RENDERER_PRESENTFLIP2),
			RF_ENTRY(SDL_RENDERER_PRESENTFLIP3),
			RF_ENTRY(SDL_RENDERER_PRESENTDISCARD),
#endif
			RF_ENTRY(SDL_RENDERER_SOFTWARE),
			RF_ENTRY(SDL_RENDERER_PRESENTVSYNC),
			RF_ENTRY(SDL_RENDERER_ACCELERATED),
			RF_ENTRY(SDL_RENDERER_TARGETTEXTURE),
			{-1, nullptr}
		};
	int i;

	osd_printf_verbose("window: using renderer %s\n", render_info->name ? render_info->name : "<unknown>");
	for (i = 0; rflist[i].name != nullptr; i++)
		if (render_info->flags & rflist[i].flag)
			osd_printf_verbose("renderer: flag %s\n", rflist[i].name);
}


int renderer_sdl2::create()
{
	// create renderer

	/* Enable bilinear filtering in case it is supported.
	 * This applies to all texture operations. However, artwort is pre-scaled
	 * and thus shouldn't be affected.
	 */
	if (video_config.filter)
	{
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	}
	else
	{
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	}

	if (video_config.waitvsync)
		m_sdl_renderer = SDL_CreateRenderer(dynamic_cast<sdl_window_info &>(window()).platform_window(), -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
	else
		m_sdl_renderer = SDL_CreateRenderer(dynamic_cast<sdl_window_info &>(window()).platform_window(), -1, SDL_RENDERER_ACCELERATED);

	if (!m_sdl_renderer)
	{
		fatalerror("Error on creating renderer: %s\n", SDL_GetError());
	}

	//SDL_SelectRenderer(window().sdl_window);

	m_blittimer = 3;

	//SDL_RenderPresent(m_sdl_renderer);
	osd_printf_verbose("Leave renderer_sdl2::create\n");

	struct SDL_RendererInfo render_info;

	SDL_GetRendererInfo(m_sdl_renderer, &render_info);
	drawsdl_show_info(&render_info);

	return 0;
}


//============================================================
//  drawsdl_xy_to_render_target
//============================================================

int renderer_sdl2::xy_to_render_target(int x, int y, int *xt, int *yt)
{
	*xt = x - m_last_hofs;
	*yt = y - m_last_vofs;
	if (*xt<0 || *xt >= m_blit_dim.width())
		return 0;
	if (*yt<0 || *yt >= m_blit_dim.height())
		return 0;
	return 1;
}

//============================================================
//  drawsdl_destroy_all_textures
//============================================================

void renderer_sdl2::destroy_all_textures()
{
	if (window().m_primlist)
	{
		window().m_primlist->acquire_lock();
		m_texlist.clear();
		window().m_primlist->release_lock();
	}
	else
		m_texlist.clear();
}

//============================================================
//  sdl_info::draw
//============================================================

#if defined(__SWITCH__)
//============================================================
//  nx_draw_overlay - L + R stats readout
//
//  A tiny block font drawn with one SDL_RenderFillRects call:
//  MAME's UI text costs one blended texture draw per glyph,
//  which the Switch GPU driver makes expensive enough to slow
//  the game down.
//============================================================

static void nx_draw_overlay(SDL_Renderer *renderer)
{
	if (!nxperf::overlay.load(std::memory_order_relaxed))
		return;

	// 3x5 glyphs, one row per entry, bit 2 = left column
	struct glyph { char ch; uint8_t rows[5]; };
	static constexpr glyph s_font[] = {
		{ '0', { 7, 5, 5, 5, 7 } }, { '1', { 2, 6, 2, 2, 7 } }, { '2', { 7, 1, 7, 4, 7 } },
		{ '3', { 7, 1, 7, 1, 7 } }, { '4', { 5, 5, 7, 1, 1 } }, { '5', { 7, 4, 7, 1, 7 } },
		{ '6', { 7, 4, 7, 5, 7 } }, { '7', { 7, 1, 1, 1, 1 } }, { '8', { 7, 5, 7, 5, 7 } },
		{ '9', { 7, 5, 7, 1, 7 } }, { '.', { 0, 0, 0, 0, 2 } }, { '%', { 5, 1, 2, 4, 5 } },
		{ '/', { 1, 1, 2, 4, 4 } }, { 'S', { 7, 4, 7, 1, 7 } }, { 'P', { 7, 5, 7, 4, 4 } },
		{ 'D', { 6, 5, 5, 5, 6 } }, { 'F', { 7, 4, 6, 4, 4 } }, { 'K', { 5, 5, 6, 5, 5 } },
		{ 'I', { 7, 2, 2, 2, 7 } }, { 'C', { 7, 4, 4, 4, 7 } }, { 'U', { 5, 5, 5, 5, 7 } } };

	char text[64];
	unsigned const speed10 = nxperf::ov_speed10.load(std::memory_order_relaxed);
	std::snprintf(text, sizeof(text), "SPD %u.%u%%  FPS %u/%u  SKIP %u  CPU %u%%",
			speed10 / 10, speed10 % 10,
			nxperf::ov_drawn.load(std::memory_order_relaxed), nxperf::ov_frames.load(std::memory_order_relaxed),
			nxperf::ov_skip.load(std::memory_order_relaxed), nxperf::ov_busy.load(std::memory_order_relaxed));

	// top right corner, clear of overlays such as Tesla's Status Monitor on the left
	constexpr int scale = 5, margin = 16, pad = 8;
	int out_w = 0, out_h = 0;
	SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
	int const text_w = int(std::strlen(text)) * 4 * scale - scale;
	int const box_x = std::max(out_w - margin - text_w - 2 * pad, 0);
	SDL_Rect rects[64 * 5 * 3];
	int count = 0;
	int x = box_x + pad;
	for (char const *p = text; *p; p++, x += 4 * scale)
	{
		auto const g = std::find_if(std::begin(s_font), std::end(s_font), [c = *p] (glyph const &f) { return f.ch == c; });
		if (g == std::end(s_font))
			continue;
		for (int row = 0; row < 5; row++)
		{
			// merge each run of set pixels in a row into one rectangle
			for (int col = 0; col < 3; )
			{
				if (!BIT(g->rows[row], 2 - col)) { col++; continue; }
				int end = col;
				while (end < 3 && BIT(g->rows[row], 2 - end))
					end++;
				rects[count++] = SDL_Rect{ x + col * scale, margin + pad + row * scale, (end - col) * scale, scale };
				col = end;
			}
		}
	}

	SDL_BlendMode old_mode;
	Uint8 r, g, b, a;
	SDL_GetRenderDrawBlendMode(renderer, &old_mode);
	SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	SDL_Rect const box{ box_x, margin, text_w + 2 * pad, 5 * scale + 2 * pad };
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderFillRect(renderer, &box);
	SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
	SDL_RenderFillRects(renderer, rects, count);
	SDL_SetRenderDrawBlendMode(renderer, old_mode);
	SDL_SetRenderDrawColor(renderer, r, g, b, a);
}
//============================================================
//  nx_draw_picker - character picker popup
//
//  Same approach as the stats overlay: a block font drawn
//  with SDL_RenderFillRects, no textures.
//============================================================

namespace {

// 5x7 glyphs, '#' = pixel
struct big_glyph { char ch; char const *rows[7]; };
constexpr big_glyph s_big_font[] = {
	{ 'A', { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
	{ 'B', { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." } },
	{ 'C', { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." } },
	{ 'D', { "####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####." } },
	{ 'E', { "#####", "#....", "#....", "####.", "#....", "#....", "#####" } },
	{ 'F', { "#####", "#....", "#....", "####.", "#....", "#....", "#...." } },
	{ 'G', { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####" } },
	{ 'H', { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
	{ 'I', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####" } },
	{ 'J', { "..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.." } },
	{ 'K', { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" } },
	{ 'L', { "#....", "#....", "#....", "#....", "#....", "#....", "#####" } },
	{ 'M', { "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#" } },
	{ 'N', { "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#" } },
	{ 'O', { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
	{ 'P', { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." } },
	{ 'Q', { ".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#" } },
	{ 'R', { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" } },
	{ 'S', { ".####", "#....", "#....", ".###.", "....#", "....#", "####." } },
	{ 'T', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
	{ 'U', { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
	{ 'V', { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." } },
	{ 'W', { "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#" } },
	{ 'X', { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" } },
	{ 'Y', { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." } },
	{ 'Z', { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" } },
	{ '0', { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." } },
	{ '1', { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." } },
	{ '2', { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" } },
	{ '3', { "#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###." } },
	{ '4', { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." } },
	{ '5', { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." } },
	{ '6', { "..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###." } },
	{ '7', { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." } },
	{ '8', { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." } },
	{ '9', { ".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.." } },
	{ ':', { ".....", "..#..", "..#..", ".....", "..#..", "..#..", "....." } },
	{ '-', { ".....", ".....", ".....", "#####", ".....", ".....", "....." } },
	{ '/', { "....#", "....#", "...#.", "..#..", ".#...", "#....", "#...." } },
	{ '<', { "...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#." } },
	{ '>', { ".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..." } } };

class block_text
{
public:
	// glyph cell is 6x8 pixels of `scale` size (5x7 glyph plus spacing)
	static int width(char const *text, int scale) { return std::max(int(std::strlen(text)) * 6 - 1, 0) * scale; }

	void add(char const *text, int x, int y, int scale)
	{
		for (char const *c = text; *c; c++, x += 6 * scale)
		{
			auto const g = std::find_if(std::begin(s_big_font), std::end(s_big_font), [ch = *c] (big_glyph const &f) { return f.ch == ch; });
			if (g == std::end(s_big_font))
				continue;
			for (int row = 0; row < 7; row++)
			{
				// one rectangle per run of set pixels in a row
				for (int col = 0; col < 5; )
				{
					if (g->rows[row][col] != '#') { col++; continue; }
					int end = col;
					while ((end < 5) && (g->rows[row][end] == '#'))
						end++;
					if (m_count < std::size(m_rects))
						m_rects[m_count++] = SDL_Rect{ x + col * scale, y + row * scale, (end - col) * scale, scale };
					col = end;
				}
			}
		}
	}

	void add_centered(char const *text, int center_x, int y, int scale) { add(text, center_x - width(text, scale) / 2, y, scale); }

	void flush(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
	{
		SDL_SetRenderDrawColor(renderer, r, g, b, 255);
		SDL_RenderFillRects(renderer, m_rects, m_count);
		m_count = 0;
	}

private:
	SDL_Rect m_rects[2048];
	int m_count = 0;
};

void fill(SDL_Renderer *renderer, SDL_Rect const &rect, Uint8 r, Uint8 g, Uint8 b)
{
	SDL_SetRenderDrawColor(renderer, r, g, b, 255);
	SDL_RenderFillRect(renderer, &rect);
}

} // anonymous namespace

static void nx_draw_picker(SDL_Renderer *renderer)
{
	if (!nxpicker::open.load(std::memory_order_relaxed) || (nxpicker::slot_count <= 0))
		return;

	int const count = nxpicker::slot_count;
	int const selection = nxpicker::selection.load(std::memory_order_relaxed);
	std::uint32_t const taken = nxpicker::taken_mask.load(std::memory_order_relaxed);

	int out_w = 0, out_h = 0;
	SDL_GetRendererOutputSize(renderer, &out_w, &out_h);

	// layout in 1080p units, scaled to the output
	float const k = out_h / 1080.0f;
	auto px = [k] (int v) { return std::max(int(v * k + 0.5f), 1); };
	int const big = px(6), small = px(3);
	int const cell_w = px(250), cell_h = px(110), gap = px(20);
	int const box_w = count * cell_w + (count - 1) * gap + 2 * px(40);
	int const box_h = px(330);
	int const box_x = (out_w - box_w) / 2, box_y = (out_h - box_h) / 2;
	int const cx = out_w / 2;

	SDL_BlendMode old_mode;
	Uint8 r, g, b, a;
	SDL_GetRenderDrawBlendMode(renderer, &old_mode);
	SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

	// panel with a border
	fill(renderer, SDL_Rect{ box_x - px(6), box_y - px(6), box_w + px(12), box_h + px(12) }, 255, 217, 15);
	fill(renderer, SDL_Rect{ box_x, box_y, box_w, box_h }, 20, 40, 110);

	static block_text text;
	char title[48];
	std::snprintf(title, sizeof(title), "CONTROLLER %d: CHOOSE CHARACTER", nxpicker::controller.load(std::memory_order_relaxed));
	text.add_centered(title, cx, box_y + px(30), small + px(1));
	text.flush(renderer, 255, 255, 255);

	int const cells_y = box_y + px(100);
	for (int s = 0; s < count; s++)
	{
		int const x = box_x + px(40) + s * (cell_w + gap);
		bool const is_taken = BIT(taken, s);
		bool const is_selected = (s == selection) && !is_taken;
		if (is_selected)
			fill(renderer, SDL_Rect{ x, cells_y, cell_w, cell_h }, 255, 217, 15);
		else
			fill(renderer, SDL_Rect{ x, cells_y, cell_w, cell_h }, is_taken ? 45 : 60, is_taken ? 45 : 80, is_taken ? 60 : 160);

		char const *const name = nxpicker::slot_names[s] ? nxpicker::slot_names[s] : "?";
		text.add_centered(name, x + cell_w / 2, cells_y + (is_taken ? px(18) : (cell_h - 7 * big) / 2), big);
		text.flush(renderer, is_selected ? 20 : (is_taken ? 120 : 255), is_selected ? 40 : (is_taken ? 120 : 255), is_selected ? 110 : (is_taken ? 120 : 255));
		if (is_taken)
		{
			text.add_centered("TAKEN", x + cell_w / 2, cells_y + cell_h - px(18) - 7 * small, small);
			text.flush(renderer, 255, 110, 110);
		}
	}

	text.add_centered("< > MOVE   A: COIN IN   B: CANCEL", cx, cells_y + cell_h + px(45), small);
	text.flush(renderer, 200, 210, 255);

	SDL_SetRenderDrawBlendMode(renderer, old_mode);
	SDL_SetRenderDrawColor(renderer, r, g, b, a);
}
#endif

int renderer_sdl2::draw(int update)
{
	texture_info *texture=nullptr;
	float vofs, hofs;
	int blit_pixels = 0;

#if defined(__SWITCH__)
	{
		// Don't let the GPU fall behind: with an unbounded backlog the display
		// pipeline eventually starts discarding frames (1-2 fps on screen while
		// emulation runs at 100%).  Waiting for the previous frame here, before
		// this frame's texture uploads, keeps the backlog at one frame while the
		// GPU works in parallel with emulation (waiting right after present cost
		// the emulation thread ~1.7ms per frame).  The time waited is "gpu" in
		// the perf log.
		using gl_finish_func = void (*)(void);
		static gl_finish_func const s_gl_finish = reinterpret_cast<gl_finish_func>(SDL_GL_GetProcAddress("glFinish"));
		static bool s_logged = false;
		if (!s_logged)
		{
			s_logged = true;
			osd_printf_verbose("switch: glFinish %s (before each frame)\n", s_gl_finish ? "enabled" : "not available");
		}
		if (s_gl_finish)
		{
			nxperf::scope nxperf_scope(nxperf::gpu_finish);
			s_gl_finish();
		}
	}
#endif

	osd_dim wdim = window().get_size();

	if (has_flags(FI_CHANGED) || (wdim.width() != m_width) || (wdim.height() != m_height))
	{
		destroy_all_textures();
		m_width = wdim.width();
		m_height = wdim.height();
		SDL_RenderSetViewport(m_sdl_renderer, nullptr);
		m_blittimer = 3;
		clear_flags(FI_CHANGED);
	}

	//SDL_SelectRenderer(window().sdl_window);

	if (m_blittimer > 0)
	{
		/* SDL Underlays need alpha = 0 ! */
		SDL_SetRenderDrawBlendMode(m_sdl_renderer, SDL_BLENDMODE_NONE);
		//SDL_SetRenderDrawColor(0,0,0,255);
		SDL_SetRenderDrawColor(m_sdl_renderer, 0,0,0,0);
		SDL_RenderFillRect(m_sdl_renderer, nullptr);
		m_blittimer--;
	}

	// compute centering parameters
	vofs = hofs = 0.0f;

	if (video_config.centerv || video_config.centerh)
	{
		int ch, cw;

		ch = wdim.height();
		cw = wdim.width();

		if (video_config.centerv)
		{
			vofs = (ch - m_blit_dim.height()) / 2.0f;
		}
		if (video_config.centerh)
		{
			hofs = (cw - m_blit_dim.width()) / 2.0f;
		}
	}

	m_last_hofs = hofs;
	m_last_vofs = vofs;

	window().m_primlist->acquire_lock();

	// now draw
	for (render_primitive &prim : *window().m_primlist)
	{
		Uint8 sr, sg, sb, sa;

		switch (prim.type)
		{
			case render_primitive::LINE:
				sr = (int)(255.0f * prim.color.r);
				sg = (int)(255.0f * prim.color.g);
				sb = (int)(255.0f * prim.color.b);
				sa = (int)(255.0f * prim.color.a);

				SDL_SetRenderDrawBlendMode(m_sdl_renderer, map_blendmode(PRIMFLAG_GET_BLENDMODE(prim.flags)));
				SDL_SetRenderDrawColor(m_sdl_renderer, sr, sg, sb, sa);
				SDL_RenderDrawLine(m_sdl_renderer, prim.bounds.x0 + hofs, prim.bounds.y0 + vofs,
						prim.bounds.x1 + hofs, prim.bounds.y1 + vofs);
				break;
			case render_primitive::QUAD:
				texture = texture_update(prim);
				if (texture)
					blit_pixels += (texture->raw_height() * texture->raw_width());
				render_quad(texture, prim,
						round_nearest(hofs + prim.bounds.x0),
						round_nearest(vofs + prim.bounds.y0));
				break;
			default:
				throw emu_fatalerror("Unexpected render_primitive type\n");
		}
	}

	window().m_primlist->release_lock();

	m_last_blit_pixels = blit_pixels;
	m_last_blit_time = -osd_ticks();
	{
#if defined(__SWITCH__)
		nx_draw_overlay(m_sdl_renderer);
		nx_draw_picker(m_sdl_renderer);
		nxperf::scope nxperf_scope(nxperf::gpu_present);
#endif
		SDL_RenderPresent(m_sdl_renderer);
	}
	m_last_blit_time += osd_ticks();

	return 0;
}


//============================================================
//  texture handling
//============================================================

//============================================================
//  texture_compute_size and type
//============================================================

copy_info_t const *texture_info::compute_size_type()
{
	copy_info_t const *result = nullptr;
	int maxperf = 0;

	for (copy_info_t const *bi = m_renderer->m_blit_info[m_format]; bi != nullptr; bi = bi->next)
	{
		if ((m_is_rotated == bi->blitter->m_is_rot) && (m_sdl_blendmode == bi->bm_mask))
		{
			if (m_renderer->RendererSupportsFormat(bi->dst_fmt, m_sdl_access, bi->dstname))
			{
				int const perf = bi->perf;
				if (perf == 0)
				{
					return bi;
				}
				else if (perf > ((maxperf * 102) / 100))
				{
					result = bi;
					maxperf = perf;
				}
			}
		}
	}

	if (result)
		return result;

	// try last resort handlers
	for (copy_info_t const *bi = m_renderer->m_blit_info[m_format]; bi != nullptr; bi = bi->next)
	{
		if ((m_is_rotated == bi->blitter->m_is_rot) && (m_sdl_blendmode == bi->bm_mask))
			if (m_renderer->RendererSupportsFormat(bi->dst_fmt, m_sdl_access, bi->dstname))
				return bi;
	}
	//FIXME: crash implement a -do nothing handler
	return nullptr;
}

bool texture_info::is_pixels_owned() const
{
	// do we own / allocated it ?
	return (m_sdl_access == SDL_TEXTUREACCESS_STATIC) && !m_copyinfo->blitter->m_is_passthrough;
}

//============================================================
//  texture_info::matches
//============================================================

bool texture_info::matches(const render_primitive &prim, const quad_setup_data &setup)
{
	return  texinfo().base == prim.texture.base &&
			texinfo().width == prim.texture.width &&
			texinfo().height == prim.texture.height &&
			texinfo().rowpixels == prim.texture.rowpixels &&
			m_setup.dudx == setup.dudx &&
			m_setup.dvdx == setup.dvdx &&
			m_setup.dudy == setup.dudy &&
			m_setup.dvdy == setup.dvdy &&
			m_setup.startu == setup.startu &&
			m_setup.startv == setup.startv &&
			((flags() ^ prim.flags) & (PRIMFLAG_BLENDMODE_MASK | PRIMFLAG_TEXFORMAT_MASK)) == 0;
}

//============================================================
//  texture_create
//============================================================

texture_info::texture_info(renderer_sdl2 *renderer, const render_texinfo &texsource, const quad_setup_data &setup, uint32_t flags)
{
	// fill in the core data
	m_renderer = renderer;
	m_hash = texture_compute_hash(texsource, flags);
	m_flags = flags;
	m_texinfo = texsource;
	m_texinfo.seqid = -1; // force set data
	m_is_rotated = false;
	m_setup = setup;
	m_sdl_blendmode = map_blendmode(PRIMFLAG_GET_BLENDMODE(flags));
	m_pitch = 0;

	switch (PRIMFLAG_GET_TEXFORMAT(flags))
	{
		case TEXFORMAT_ARGB32:
			m_format = SDL_TEXFORMAT_ARGB32;
			break;
		case TEXFORMAT_RGB32:
			m_format = texsource.palette ? SDL_TEXFORMAT_RGB32_PALETTED : SDL_TEXFORMAT_RGB32;
			break;
		case TEXFORMAT_PALETTE16:
			m_format = SDL_TEXFORMAT_PALETTE16;
			break;
		case TEXFORMAT_YUY16:
			m_format = texsource.palette ? SDL_TEXFORMAT_YUY16_PALETTED : SDL_TEXFORMAT_YUY16;
			break;

		default:
			osd_printf_error("Unknown textureformat %d\n", PRIMFLAG_GET_TEXFORMAT(flags));
	}

	if (setup.rotwidth != m_texinfo.width || setup.rotheight != m_texinfo.height
			|| setup.dudx < 0 || setup.dvdy < 0 || (PRIMFLAG_GET_TEXORIENT(flags) != 0))
		m_is_rotated = true;
	else
		m_is_rotated = false;

	//m_sdl_access = SDL_TEXTUREACCESS_STATIC;
	m_sdl_access = SDL_TEXTUREACCESS_STREAMING;

	// Watch out for 0x0 textures ...
	if (!m_setup.rotwidth || !m_setup.rotheight)
		osd_printf_warning("Trying to create texture with zero dim\n");

	// set copy_info

	m_copyinfo = compute_size_type();

	m_texture_id = SDL_CreateTexture(m_renderer->m_sdl_renderer, m_copyinfo->dst_fmt, m_sdl_access,
			m_setup.rotwidth, m_setup.rotheight);

	if (!m_texture_id)
		osd_printf_error("Error creating texture: %d x %d, pixelformat %s error: %s\n", m_setup.rotwidth, m_setup.rotheight,
				m_copyinfo->dstname, SDL_GetError());
#if defined(__SWITCH__)
	if (m_texture_id)
	{
		nxperf::tex_live++;
		nxperf::tex_created++;
	}
	else
	{
		nxperf::sdl_fail.add(0);
	}
#endif

	if (m_sdl_access == SDL_TEXTUREACCESS_STATIC)
	{
		if (m_copyinfo->blitter->m_is_passthrough)
			m_pixels = nullptr;
		else
			m_pixels = malloc(m_setup.rotwidth * m_setup.rotheight * m_copyinfo->blitter->m_dest_bpp);
	}
	m_last_access = osd_ticks();
}

texture_info::~texture_info()
{
	if (is_pixels_owned() && m_pixels)
		free(m_pixels);
#if defined(__SWITCH__)
	if (m_texture_id)
		nxperf::tex_live--;
#endif
	SDL_DestroyTexture(m_texture_id);
}

//============================================================
//  texture_set_data
//============================================================

void texture_info::set_data(const render_texinfo &texsource, const uint32_t flags)
{
#if defined(__SWITCH__)
	nxperf::scope nxperf_scope(nxperf::tex_upload);
#endif
	m_copyinfo->time -= osd_ticks();
	if (m_sdl_access == SDL_TEXTUREACCESS_STATIC)
	{
		if (m_copyinfo->blitter->m_is_passthrough)
		{
			m_pixels = texsource.base;
			m_pitch = m_texinfo.rowpixels * m_copyinfo->blitter->m_dest_bpp;
		}
		else
		{
			m_pitch = m_setup.rotwidth * m_copyinfo->blitter->m_dest_bpp;
			m_copyinfo->blitter->texop(this, &texsource);
		}
#if defined(__SWITCH__)
		if (SDL_UpdateTexture(m_texture_id, nullptr, m_pixels, m_pitch) < 0)
			nxperf::sdl_fail.add(0);
#else
		SDL_UpdateTexture(m_texture_id, nullptr, m_pixels, m_pitch);
#endif
	}
	else
	{
#if defined(__SWITCH__)
		if (SDL_LockTexture(m_texture_id, nullptr, (void **)&m_pixels, &m_pitch) < 0)
		{
			nxperf::sdl_fail.add(0);
			m_copyinfo->time += osd_ticks();
			return;
		}
#else
		SDL_LockTexture(m_texture_id, nullptr, (void **)&m_pixels, &m_pitch);
#endif
		if ( m_copyinfo->blitter->m_is_passthrough )
		{
			const uint8_t *src = (uint8_t *)texsource.base;
			uint8_t *dst = (uint8_t *)m_pixels;
			int spitch = texsource.rowpixels * m_copyinfo->blitter->m_dest_bpp;
			int num = texsource.width * m_copyinfo->blitter->m_dest_bpp;
			int h = texsource.height;
			while (h--) {
				memcpy(dst, src, num);
				src += spitch;
				dst += m_pitch;
			}
		}
		else
			m_copyinfo->blitter->texop(this, &texsource);
		SDL_UnlockTexture(m_texture_id);
	}
	m_copyinfo->time += osd_ticks();
}

//============================================================
//  compute rotation setup
//============================================================

inline float signf(const float a)
{
	return (0.0f < a) - (a < 0.0f);
}

void quad_setup_data::compute(const render_primitive &prim, const int prescale)
{
	const render_quad_texuv *texcoords = &prim.texcoords;
	int texwidth = prim.texture.width;
	int texheight = prim.texture.height;
	float fdudx, fdvdx, fdudy, fdvdy;
	float width, height;
	float fscale;
	/* determine U/V deltas */
	if ((PRIMFLAG_GET_SCREENTEX(prim.flags)))
		fscale = (float) prescale;
	else
		fscale = 1.0f;

	fdudx = (texcoords->tr.u - texcoords->tl.u); // a a11
	fdvdx = (texcoords->tr.v - texcoords->tl.v); // c a21
	fdudy = (texcoords->bl.u - texcoords->tl.u); // b a12
	fdvdy = (texcoords->bl.v - texcoords->tl.v); // d a22

	width = fabsf(( fdudx * (float) (texwidth) + fdvdx * (float) (texheight)) ) * fscale;
	height = fabsf((fdudy * (float) (texwidth) + fdvdy * (float) (texheight)) ) * fscale;

	fdudx = signf(fdudx) / fscale;
	fdvdy = signf(fdvdy) / fscale;
	fdvdx = signf(fdvdx) / fscale;
	fdudy = signf(fdudy) / fscale;

#if 0
	printf("tl.u %f tl.v %f\n", texcoords->tl.u, texcoords->tl.v);
	printf("tr.u %f tr.v %f\n", texcoords->tr.u, texcoords->tr.v);
	printf("bl.u %f bl.v %f\n", texcoords->bl.u, texcoords->bl.v);
	printf("br.u %f br.v %f\n", texcoords->br.u, texcoords->br.v);
	/* compute start and delta U,V coordinates now */
#endif

	dudx = round_nearest(65536.0f * fdudx);
	dvdx = round_nearest(65536.0f * fdvdx);
	dudy = round_nearest(65536.0f * fdudy);
	dvdy = round_nearest(65536.0f * fdvdy);
	startu = round_nearest(65536.0f * (float) texwidth * texcoords->tl.u);
	startv = round_nearest(65536.0f * (float) texheight * texcoords->tl.v);

	/* clamp to integers */

	rotwidth = round_nearest(width);
	rotheight = round_nearest(height);

	//printf("%d %d rot %d %d\n", texwidth, texheight, rotwidth, rotheight);

	startu += (dudx + dudy) / 2;
	startv += (dvdx + dvdy) / 2;

}

//============================================================
//  texture_find
//============================================================

texture_info *renderer_sdl2::texture_find(const render_primitive &prim, const quad_setup_data &setup)
{
	const HashT texhash = texture_compute_hash(prim.texture, prim.flags);
	const osd_ticks_t now = osd_ticks();

	// find a match
	for (auto texture = m_texlist.begin(); texture != m_texlist.end(); )
	{
		if ((texture->hash() == texhash) && texture->matches(prim, setup))
		{
#if !defined(__SWITCH__)
			// would we choose another blitter based on performance?
			// (not on Switch: the timings flip between near-identical
			// blitters and the texture got recreated every ~2.5 seconds)
			if ((texture->m_copyinfo->samples & 0x7f) == 0x7f)
			{
				if (texture->m_copyinfo != texture->compute_size_type())
					return nullptr;
			}
#endif
			texture->m_last_access = now;
			return &*texture;
		}
		else
		{
			// free resources not needed any longer?
			if ((now - texture->m_last_access) > osd_ticks_per_second())
				texture = m_texlist.erase(texture);
			else
				++texture;
		}
	}

	// nothing found
	return nullptr;
}

//============================================================
//  texture_update
//============================================================

texture_info * renderer_sdl2::texture_update(const render_primitive &prim)
{
	quad_setup_data setup;
	texture_info *texture;

	setup.compute(prim, window().prescale());

	texture = texture_find(prim, setup);

	// if we didn't find one, create a new texture
	if (!texture && prim.texture.base)
	{
		// add us to the texture list
		texture = &m_texlist.emplace_front(this, prim.texture, setup, prim.flags);
	}

	if (texture)
	{
		if (prim.texture.base && (texture->texinfo().seqid != prim.texture.seqid))
		{
			texture->texinfo().seqid = prim.texture.seqid;
			// if we found it, but with a different seqid, copy the data
			texture->set_data(prim.texture, prim.flags);
		}

	}
	return texture;
}

render_primitive_list *renderer_sdl2::get_primitives()
{
	osd_dim nd = window().get_size();
	if (nd != m_blit_dim)
	{
		m_blit_dim = nd;
		notify_changed();
	}
	window().target()->set_bounds(m_blit_dim.width(), m_blit_dim.height(), window().pixel_aspect());
	return &window().target()->get_primitives();
}


class video_sdl2 : public osd_module, public render_module
{
public:
	video_sdl2()
		: osd_module(OSD_RENDERER_PROVIDER, "accel")
		, m_blit_info_initialized(false)
		, m_gllib_loaded(false)
	{
		std::fill(std::begin(m_blit_info), std::end(m_blit_info), nullptr);
	}
	~video_sdl2()
	{
		free_copy_info();
	}

	virtual int init(osd_interface &osd, osd_options const &options) override;
	virtual void exit() override { free_copy_info(); }

	virtual std::unique_ptr<osd_renderer> create(osd_window &window) override;

protected:
	virtual unsigned flags() const override { return FLAG_INTERACTIVE | FLAG_SDL_NEEDS_OPENGL; }

private:
	static inline constexpr Uint32 BM_ALL = UINT32_MAX; // SDL_BLENDMODE_MASK | SDL_BLENDMODE_BLEND | SDL_BLENDMODE_ADD | SDL_BLENDMODE_MOD

	void expand_copy_info();
	void free_copy_info();

	static void add_list(copy_info_t const *&head, copy_info_t const &element, Uint32 bm);

	copy_info_t const *m_blit_info[SDL_TEXFORMAT_LAST + 1];
	bool m_blit_info_initialized;
	bool m_gllib_loaded;

	static copy_info_t const s_blit_info_default[];
};

int video_sdl2::init(osd_interface &osd, osd_options const &options)
{
	osd_printf_verbose("Using SDL native texturing driver (SDL 2.0+)\n");

	// Load the GL library now - else MT will fail
	char const *libname = nullptr;
#if USE_OPENGL
	libname = dynamic_cast<sdl_options const &>(options).gl_lib();
	if (libname && (!*libname || !std::strcmp(libname, OSDOPTVAL_AUTO)))
		libname = nullptr;
#endif

	if (!m_gllib_loaded)
	{
		// No fatalerror here since not all video drivers support GL!
		if (SDL_GL_LoadLibrary(libname) != 0)
		{
			osd_printf_error("Unable to load OpenGL shared library: %s\n", libname ? libname : "<default>");
			m_gllib_loaded = true;
		}
		else
		{
			osd_printf_verbose("Loaded OpenGL shared library: %s\n", libname ? libname : "<default>");
		}
	}

	return 0;
}

std::unique_ptr<osd_renderer> video_sdl2::create(osd_window &window)
{
	if (!m_blit_info_initialized)
	{
		// On macOS, calling this from drawsdl2_init will prohibit fullscreen toggling.
		// It is than not possible to toggle from fullscreen to window mode.
		expand_copy_info();
		m_blit_info_initialized = true;
	}

	return std::make_unique<renderer_sdl2>(window, m_blit_info);
}

void video_sdl2::expand_copy_info()
{
	for (const copy_info_t *bi = s_blit_info_default; bi->src_fmt != -1; bi++)
	{
		if (bi->bm_mask == BM_ALL)
		{
			add_list(m_blit_info[bi->src_fmt], *bi, SDL_BLENDMODE_NONE);
			add_list(m_blit_info[bi->src_fmt], *bi, SDL_BLENDMODE_ADD);
			add_list(m_blit_info[bi->src_fmt], *bi, SDL_BLENDMODE_MOD);
			add_list(m_blit_info[bi->src_fmt], *bi, SDL_BLENDMODE_BLEND);
		}
		else
		{
			add_list(m_blit_info[bi->src_fmt], *bi, bi->bm_mask);
		}
	}
}

void video_sdl2::free_copy_info()
{
	if (m_blit_info_initialized)
	{
		for (int i = 0; i <= SDL_TEXFORMAT_LAST; i++)
		{
			for (copy_info_t const *bi = m_blit_info[i]; bi != nullptr; )
			{
				if (bi->pixel_count)
				{
					osd_printf_verbose(
							"%s -> %s %s blendmode 0x%02x, %d samples: %d KPixel/sec\n",
							bi->srcname,
							bi->dstname,
							bi->blitter->m_is_rot ? "rot" : "norot",
							bi->bm_mask,
							bi->samples,
							bi->perf);
				}
				delete std::exchange(bi, bi->next);
			}
			m_blit_info[i] = nullptr;
		}
		m_blit_info_initialized = false;
	}
}

void video_sdl2::add_list(copy_info_t const *&head, copy_info_t const &element, Uint32 bm)
{
	copy_info_t *const newci = new copy_info_t(element);

	newci->bm_mask = bm;
	newci->next = head;
	head = newci;
}


//============================================================
//  STATIC VARIABLES
//============================================================

#define ENTRY(a,b,f) { SDL_TEXFORMAT_ ## a, SDL_PIXELFORMAT_ ## b, &texcopy_ ## f, BM_ALL, #a, #b, 0, 0, 0, 0}
#define ENTRY_BM(a,b,f,bm) { SDL_TEXFORMAT_ ## a, SDL_PIXELFORMAT_ ## b, &texcopy_ ## f, bm, #a, #b, 0, 0, 0, 0}
#define ENTRY_LR(a,b,f) { SDL_TEXFORMAT_ ## a, SDL_PIXELFORMAT_ ## b, &texcopy_ ## f, BM_ALL, #a, #b, 0, 0, 0, -1}

copy_info_t const video_sdl2::s_blit_info_default[] =
{
	/* no rotation */
	ENTRY(ARGB32,           ARGB8888,   argb32_argb32),
	ENTRY_LR(ARGB32,        RGB888,     argb32_rgb32),
	/* Entry primarily for directfb */
	ENTRY_BM(ARGB32,        RGB888,     argb32_rgb32, SDL_BLENDMODE_ADD),
	ENTRY_BM(ARGB32,        RGB888,     argb32_rgb32, SDL_BLENDMODE_MOD),
	ENTRY_BM(ARGB32,        RGB888,     argb32_rgb32, SDL_BLENDMODE_NONE),

	ENTRY(RGB32,            ARGB8888,   rgb32_argb32),
	ENTRY(RGB32,            RGB888,     rgb32_rgb32),

	ENTRY(RGB32_PALETTED,   ARGB8888,   rgb32pal_argb32),
	ENTRY(RGB32_PALETTED,   RGB888,     rgb32pal_argb32),

	ENTRY(YUY16,            UYVY,       yuv16_uyvy),
	ENTRY(YUY16,            YUY2,       yuv16_yuy2),
	ENTRY(YUY16,            YVYU,       yuv16_yvyu),
	ENTRY(YUY16,            ARGB8888,   yuv16_argb32),
	ENTRY(YUY16,            RGB888,     yuv16_argb32),

	ENTRY(YUY16_PALETTED,   UYVY,       yuv16pal_uyvy),
	ENTRY(YUY16_PALETTED,   YUY2,       yuv16pal_yuy2),
	ENTRY(YUY16_PALETTED,   YVYU,       yuv16pal_yvyu),
	ENTRY(YUY16_PALETTED,   ARGB8888,   yuv16pal_argb32),
	ENTRY(YUY16_PALETTED,   RGB888,     yuv16pal_argb32),

	ENTRY(PALETTE16,        ARGB8888,   pal16_argb32),
	ENTRY(PALETTE16,        RGB888,     pal16_argb32),

	ENTRY(RGB15,            RGB555,     rgb15_rgb555),
	ENTRY(RGB15,            ARGB1555,   rgb15_argb1555),
	ENTRY(RGB15,            ARGB8888,   rgb15_argb32),
	ENTRY(RGB15,            RGB888,     rgb15_argb32),

	ENTRY(RGB15_PALETTED,   ARGB8888,   rgb15pal_argb32),
	ENTRY(RGB15_PALETTED,   RGB888,     rgb15pal_argb32),

	ENTRY(PALETTE16A,       ARGB8888,   pal16a_argb32),
	ENTRY(PALETTE16A,       RGB888,     pal16a_rgb32),

	/* rotation */
	ENTRY(ARGB32,           ARGB8888,   rot_argb32_argb32),
	ENTRY_LR(ARGB32,        RGB888,     rot_argb32_rgb32),
	/* Entry primarily for directfb */
	ENTRY_BM(ARGB32,        RGB888,     rot_argb32_rgb32, SDL_BLENDMODE_ADD),
	ENTRY_BM(ARGB32,        RGB888,     rot_argb32_rgb32, SDL_BLENDMODE_MOD),
	ENTRY_BM(ARGB32,        RGB888,     rot_argb32_rgb32, SDL_BLENDMODE_NONE),

	ENTRY(RGB32,            ARGB8888,   rot_rgb32_argb32),
	ENTRY(RGB32,            RGB888,     rot_argb32_argb32),

	ENTRY(RGB32_PALETTED,   ARGB8888,   rot_rgb32pal_argb32),
	ENTRY(RGB32_PALETTED,   RGB888,     rot_rgb32pal_argb32),

	ENTRY(YUY16,            ARGB8888,   rot_yuv16_argb32rot),
	ENTRY(YUY16,            RGB888,     rot_yuv16_argb32rot),

	ENTRY(YUY16_PALETTED,   ARGB8888,   rot_yuv16pal_argb32rot),
	ENTRY(YUY16_PALETTED,   RGB888,     rot_yuv16pal_argb32rot),

	ENTRY(PALETTE16,        ARGB8888,   rot_pal16_argb32),
	ENTRY(PALETTE16,        RGB888,     rot_pal16_argb32),

	ENTRY(RGB15,            RGB555,     rot_rgb15_argb1555),
	ENTRY(RGB15,            ARGB1555,   rot_rgb15_argb1555),
	ENTRY(RGB15,            ARGB8888,   rot_rgb15_argb32),
	ENTRY(RGB15,            RGB888,     rot_rgb15_argb32),

	ENTRY(RGB15_PALETTED,   ARGB8888,   rot_rgb15pal_argb32),
	ENTRY(RGB15_PALETTED,   RGB888,     rot_rgb15pal_argb32),

	ENTRY(PALETTE16A,       ARGB8888,   rot_pal16a_argb32),
	ENTRY(PALETTE16A,       RGB888,     rot_pal16a_rgb32),

	{ -1 },
};

} // anonymous namespace

} // namespace osd

#else // defined(OSD_SDL) && !defined(SDLMAME_SDL3)

namespace osd { namespace { MODULE_NOT_SUPPORTED(video_sdl2, OSD_RENDERER_PROVIDER, "accel") } }

#endif // defined(OSD_SDL) && !defined(SDLMAME_SDL3)

MODULE_DEFINITION(RENDERER_SDL2, osd::video_sdl2)
