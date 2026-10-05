/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	The user interface of headless mode (config:Headless): there is none.

	No window and no GL context are created. The thread that would draw the user interface starts the
	guest program and waits for the run to end, and what the emulator thread queues for it (the frames)
	runs on the emulator thread itself, at the point where it is queued.
*/

#include <atomic>

#include "types.h"
#include "gui.h"
#include "gui_renderer.h"
#include "cfg/cfg.h"
#include "libswirl.h"
#include "hw/pvr/Renderer_if.h"
#include "oslib/threading.h"
#include "utils/run_control.h"

struct HeadlessGUI final : GUI {
    void Init() override { }
    void OpenSettings(std::function<void()> cb) override { cb(); }
    void RenderUI() override { }
    void DisplayNotification(const char* msg, int duration) override { }
    void RenderOSD() override { }
    void OpenOnboarding() override { }
    void RefreshFiles() override { }

    bool IsOpen() override { return false; }
    bool IsVJoyEdit() override { return false; }
    bool IsContentBrowser() override { return false; }
};

struct HeadlessGUIRenderer final : GUIRenderer {
    std::atomic<bool> keepRunning { true };
    cResetEvent stopped;

    void Start() override {
        keepRunning = true;
    }

    void Stop() override {
        keepRunning = false;
        stopped.Set();
    }

    // The renderer has to work without a GL context. auto is the one that renders nothing
    bool SelectRenderer() {
        if (settings.pvr.backend == "auto") {
            settings.pvr.backend = "none";
            return true;
        }

        for (const auto& backend : rend_get_backends()) {
            if (backend.slug == settings.pvr.backend)
                return backend.headless;
        }

        return false;
    }

    void UILoop() override {
        string image = cfgLoadStr("config", "image", "");

        if (image.empty()) {
            runctl_fail("headless: nothing to run, the command line has no image and no program");
            return;
        }

        if (!SelectRenderer()) {
            runctl_fail(("headless: no renderer " + settings.pvr.backend + " that runs without a GL context").c_str());
            return;
        }

        virtualDreamcast.reset(VirtualDreamcast::Create());

        if (!virtualDreamcast->Init()) {
            virtualDreamcast.reset();
            runctl_fail("headless: the machine did not initialise");
            return;
        }

        int rc = virtualDreamcast->StartGame(image);

        if (rc != 0) {
            virtualDreamcast.reset();
            runctl_fail(("headless: " + image + " did not start, error " + to_string(rc)).c_str());
            return;
        }

        while (keepRunning)
            stopped.Wait();

        // The emulator thread has stopped the CPU and is on its way out. This shuts the machine down and joins it
        virtualDreamcast.reset();
    }

    void QueueEmulatorFrame(std::function<bool()> callback) override {
        callback();
    }

    void WaitQueueEmpty() override { }

    void UIFrame() override { }

    bool CreateContext() override {
        return true;
    }
};

GUI* CreateHeadlessGUI() {
    return new HeadlessGUI();
}

GUIRenderer* CreateHeadlessGUIRenderer() {
    return new HeadlessGUIRenderer();
}
