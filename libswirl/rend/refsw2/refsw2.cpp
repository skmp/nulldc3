/*
	This file is part of libswirl
*/
#include "license/bsd"
#include "build.h"
#include "types.h"

#include <atomic>

u8* emu_vram;
extern u32 FrameCount;

// Set by the input code of every build, consumed by the renderers of the TA_LLE build
std::atomic<bool> refsw2_do_dump(false);

#if FEAT_TA == TA_LLE
#include "imgui/imgui.h"
#include "hw/pvr/Renderer_if.h"
#include "rend/gles/gles.h"
#include "rend/gles/imgui_impl_opengl3.h"

#include "hw/pvr/pvr_regs.h"
#include "hw/pvr/pvr_mem.h"
#include "rend/TexCache.h"
#include "utils/run_control.h"

#include <set>

extern FILE* rendlog;
extern const char* dump_textures;
extern std::set<u64> texture_dumps;

void RenderCORE();

struct refsw2rend : Renderer
{
	u8* vram;
	refsw2rend(u8* vram) : vram(vram) {
		emu_vram = vram;
	}
	void SetFBScale(float x, float y)
    {
        
    }

    virtual bool Process(TA_context* ctx) {

        // no-op for now
        return true;
    }


    // called on vblank
    virtual bool RenderFramebuffer() {
        Present();
        return true;
    }

    virtual bool Init() {
		return true;
	}

	virtual void Resize(int w, int h) {

    }

	virtual bool RenderPVR() {        
        if (refsw2_do_dump) {
            static char dump_folder[256];
            static char dump_log[256];
            static char dump_textures_path[256];

            static char vram_dump_path[256];
            static char pvr_regs_dump_path[256];

            auto time_now = time(nullptr);
            snprintf(dump_folder, sizeof(dump_folder), "refsw2-dump-%lld", (long long)time_now);
            snprintf(dump_log, sizeof(dump_log), "%s/refsw2.log", dump_folder, (long long)time_now);
            snprintf(dump_textures_path, sizeof(dump_textures_path), "%s/textures", dump_folder, (long long)time_now);

            snprintf(vram_dump_path, sizeof(vram_dump_path), "%s/vram.bin", dump_folder, (long long)time_now);
            snprintf(pvr_regs_dump_path, sizeof(pvr_regs_dump_path), "%s/pvr_regs.bin", dump_folder, (long long)time_now);

            make_directory(dump_folder);
            make_directory(dump_textures_path);
            rendlog = fopen(dump_log, "w");
            texture_dumps.clear();
            dump_textures = dump_textures_path;

            printf("Dumping VRAM to %s\n", vram_dump_path);
            {
                FILE* v0 = fopen(vram_dump_path, "wb");
                if (!v0) {
                    return false;
                }
                for (size_t i = 0; i < VRAM_SIZE; i += 4)
                {
                    auto v = vrp(vram, i);
                    if (1 != fwrite(v, sizeof(*v), 1, v0)) {
                        fclose(v0);
                        return false;
                    }
                }
                fclose(v0);
            }

            printf("Dumping PVR registers to %s\n", pvr_regs_dump_path);
            {
                FILE* v0 = fopen(pvr_regs_dump_path, "wb");
                if (!v0) {
                    return false;
                }
                if (1 != fwrite(pvr_regs, sizeof(pvr_regs), 1, v0)) {
                    fclose(v0);
                    return false;
                }
                fclose(v0);
            }

            printf("Dumping textures to %s\n", dump_textures);
            printf("Dumping render log to %s\n", dump_log);

        }
        
		RenderCORE();
            
        if (rendlog) {
            refsw2_do_dump = false;
            fclose(rendlog);
            rendlog = nullptr;
            dump_textures = nullptr;
        }

        FrameCount++;
		return false;
	}

 	virtual void Present()
    {

        // TODO: BG color, etc
        if (!FB_R_CTRL.fb_enable)
            return;
        if (FB_R_SIZE.fb_x_size == 0 || FB_R_SIZE.fb_y_size == 0)
            return;

        int width = (FB_R_SIZE.fb_x_size + 1) << 1; // in 16-bit words
        int height = FB_R_SIZE.fb_y_size + 1;
        int modulus = (FB_R_SIZE.fb_modulus - 1) << 1;

        int bpp;
        switch (FB_R_CTRL.fb_depth)
        {
        case fbde_0555:
        case fbde_565:
            bpp = 2;
            break;
        case fbde_888:
            bpp = 3;
            width = (width * 2) / 3;     // in pixels
            modulus = (modulus * 2) / 3; // in pixels
            break;
        case fbde_C888:
            bpp = 4;
            width /= 2;   // in pixels
            modulus /= 2; // in pixels
            break;
        default:
            die("Invalid framebuffer format\n");
            bpp = 4;
            break;
        }

        #if !defined(REFSW_OFFLINE)
        u32 addr = SPG_CONTROL.interlace && SPG_STATUS.fieldnum ? FB_R_SOF2 : FB_R_SOF1;
        #else
        u32 addr = FB_W_SOF1;
        #endif

        static PixelBuffer<u32> pb;
        if (pb.total_pixels != width * (SPG_CONTROL.interlace ? (height * 2 + 1) : height)) {
            pb.init(width, SPG_CONTROL.interlace ? (height * 2 + 1) : height);
        }

        u8 *dst = (u8 *)pb.data();

        if (SPG_CONTROL.interlace & SPG_STATUS.fieldnum) {
            dst += width * 4;
        }

#if !defined(REFSW_OFFLINE)
#define RED_5 0
#define BLUE_5 10
#define RED_6 0
#define BLUE_6 11
#define RED_8 0
#define BLUE_8 16
#else
#define RED_5 10
#define BLUE_5 0
#define RED_6 11
#define BLUE_6 0
#define RED_8 16
#define BLUE_8 0
#endif

        switch (FB_R_CTRL.fb_depth)
        {
        case fbde_0555: // 555 RGB
            for (int y = 0; y < height; y++)
            {
                for (int i = 0; i < width; i++)
                {
                    u16 src = pvr_read_area1_16(vram, addr);
                    *dst++ = (((src >> RED_5) & 0x1F) << 3) + FB_R_CTRL.fb_concat;
                    *dst++ = (((src >> 5) & 0x1F) << 3) + FB_R_CTRL.fb_concat;
                    *dst++ = (((src >> BLUE_5) & 0x1F) << 3) + FB_R_CTRL.fb_concat;
                    *dst++ = 0xFF;
                    addr += bpp;
                }
                addr += modulus * bpp;
                if (SPG_CONTROL.interlace) {
                    dst += width * 4;
                }
            }
            break;

        case fbde_565: // 565 RGB
            for (int y = 0; y < height; y++)
            {
                for (int i = 0; i < width; i++)
                {
                    u16 src = pvr_read_area1_16(vram, addr);
                    *dst++ = (((src >> RED_6) & 0x1F) << 3) + FB_R_CTRL.fb_concat;
                    *dst++ = (((src >> 5) & 0x3F) << 2) + (FB_R_CTRL.fb_concat >> 1);
                    *dst++ = (((src >> BLUE_6) & 0x1F) << 3) + FB_R_CTRL.fb_concat;
                    *dst++ = 0xFF;
                    addr += bpp;
                }
                addr += modulus * bpp;

                if (SPG_CONTROL.interlace) {
                    dst += width * 4;
                }
            }
            break;
        case fbde_888: // 888 RGB
            for (int y = 0; y < height; y++)
            {
                for (int i = 0; i < width; i++)
                {
                    if (addr & 1)
                    {
                        u32 src = pvr_read_area1_32(vram, addr - 1);
                        *dst++ = src >> RED_8;
                        *dst++ = src >> 8;
                        *dst++ = src >> BLUE_8;
                    }
                    else
                    {
                        u32 src = pvr_read_area1_32(vram, addr);
                        *dst++ = src >> (RED_8 + 8);
                        *dst++ = src >> 16;
                        *dst++ = src >> (BLUE_8 + 8);
                    }
                    *dst++ = 0xFF;
                    addr += bpp;
                }
                addr += modulus * bpp;

                if (SPG_CONTROL.interlace) {
                    dst += width * 4;
                }
            }
            break;
        case fbde_C888: // 0888 RGB
            for (int y = 0; y < height; y++)
            {
                for (int i = 0; i < width; i++)
                {
                    u32 src = pvr_read_area1_32(vram, addr);
                    *dst++ = src >> RED_8;
                    *dst++ = src >> 8;
                    *dst++ = src >> BLUE_8;
                    *dst++ = 0xFF;
                    addr += bpp;
                }
                addr += modulus * bpp;
                
                if (SPG_CONTROL.interlace) {
                    dst += width * 4;
                }
            }
            break;
        }

        // without a GL context the frame is done here
        if (runctl_present(pb.data(), width, SPG_CONTROL.interlace ? height * 2 : height))
            return;
        
	// TODO softrend without X11 (SDL f.e.)
    //die("Softrend doesn't know how to update the screen");
    // glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    static GLuint image_texture = 0;

    if (image_texture == 0) {
        glGenTextures(1, &image_texture);
    }
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Upload pixels into texture
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, SPG_CONTROL.interlace ? height * 2 : height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pb.data());

    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    void ImGui_Impl_NewFrame();
    ImGui_Impl_NewFrame();
    ImGui::NewFrame();
    auto display = ImGui::GetIO().DisplaySize;
    auto imgsize = display;
    auto topleft = ImVec2(0.0f, 0.0f);
    if (display.x > 640 && display.y > 480) {
        int scale = int(min(display.x / 640, display.y / 480));
        imgsize.x = 640 * scale;
        imgsize.y = 480 * scale;
        topleft.x = int(display.x - imgsize.x)/2;
        topleft.y = int(display.y - imgsize.y)/2;
    }
    ImGui::SetNextWindowPos(topleft);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
    ImGui::Begin("RefSW output", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);
    ImGui::Image((ImTextureID)(intptr_t)image_texture, imgsize);
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
};
static auto refsw2registration = RegisterRendererBackend(rendererbackend_t{ "refsw2", "Different refsw", 2, [](u8* vram) { return (Renderer*) new refsw2rend(vram); }, true });
#endif
