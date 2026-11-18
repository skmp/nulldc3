/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once

#include "ta_ctx.h"

extern u32 VertexCount;
extern u32 FrameCount;


void rend_init_renderer(u8* vram);
void rend_term_renderer();
void rend_vblank();

void rend_start_render(u8* vram);
void rend_end_render();

void rend_set_fb_scale(float x,float y);
void rend_resize(int width, int height);
void rend_text_invl(vram_block* bl);

///////
extern TA_context* _pvrrc;

#define pvrrc (_pvrrc->rend)

struct Renderer;

struct rendererbackend_t {
	string slug;
	string desc;
	int priority;
	Renderer* (*create)(u8* vram);
	bool headless = false;	// works without a GL context: what it presents goes to runctl_present
	bool lle_ta = false;	// draws what refsw3-ta (refsw3/refta) wrote into video memory; false: what the emulator's own TA
							// (hw/pvr/ta.cpp, "HLE") passes to Process()
};

// The TA the renderer chosen in the settings wants (settings.pvr.backend, or what "auto" picks), unless
// settings.pvr.ForceTaHle: true refsw3-ta, false the emulator's own. hw/pvr/ta_lle.h latches it at TA_LIST_INIT.
bool rend_wants_lle_ta();

struct Renderer
{
	rendererbackend_t backendInfo;	// gets filled by renderer_if

	virtual bool Init()=0;
	
	virtual void Resize(int w, int h)=0;
	virtual void SetFBScale(float x, float y) = 0;

	virtual bool Process(TA_context* ctx)=0;
	virtual bool RenderPVR()=0;
    virtual bool RenderFramebuffer() = 0;
	virtual bool RenderLastFrame() { return false; }

	virtual void Present()=0;

	virtual u32 GetTexture(TSP tsp, TCW tcw) { return 0; }

	virtual ~Renderer() { }
};

extern bool renderer_enabled;	// Signals the renderer thread to exit
extern bool renderer_changed;	// Signals the renderer thread to switch renderer

extern bool RegisterRendererBackend(const rendererbackend_t& backend);
vector<rendererbackend_t> rend_get_backends();