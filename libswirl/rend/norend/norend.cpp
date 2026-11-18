/*
	This file is part of libswirl
*/
#include "license/bsd"



#include "hw/pvr/Renderer_if.h"
#include "oslib/oslib.h"

struct norend : Renderer
{
	bool Init()
	{
		return true;
	}
	void SetFBScale(float x, float y) { }
	void Resize(int w, int h) { }
	void Term() { }


    bool Process(TA_context* ctx) { return true; }

	bool RenderPVR()
	{
		return true;//!pvrrc.isRTT;
	}

    bool RenderFramebuffer()
    {
        return true;
    }

	void Present() { }
};

// refsw3-ta still runs, CORE is what is left out: the guest sees what it sees with refsw3 or refsw2, except in VRAM
// (system test frame.refsw2)
Renderer* rend_norend(u8* vram) { return new ::norend(); }

static auto norend = RegisterRendererBackend(rendererbackend_t{ "none", "No PVR Rendering (+ refsw3-ta)", -2, rend_norend, true, true });
