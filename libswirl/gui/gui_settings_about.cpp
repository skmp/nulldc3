/*
	This file is part of libswirl
*/
#include "license/bsd"



#include "types.h"
#include "gui.h"
#include "stdclass.h"
#include "imgui/imgui.h"
#include "gui_partials.h"
#include "gui_util.h"
#include "nulldc_version.h"
#include "utils/glwrap/GLES.h"

void gui_settings_about()
{
	if (ImGui::BeginTabItem("About"))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);
	    if (ImGui::CollapsingHeader("nullDC", ImGuiTreeNodeFlags_DefaultOpen))
	    {
			ImGui::Text("Version: %s", nulldc_version());
			ImGui::Text("Git Hash: %s%s", nulldc_git_hash(), nulldc_git_dirty() ? "-dirty" : "");
			ImGui::Text("Target: %s",
#if DC_PLATFORM == DC_PLATFORM_DREAMCAST
					"Dreamcast"
#elif DC_PLATFORM == DC_PLATFORM_NAOMI
					"Naomi"
#elif DC_PLATFORM == DC_PLATFORM_ATOMISWAVE
					"Atomiswave"
#else
					"Unknown"
#endif
					);
	    }
	    if (ImGui::CollapsingHeader("Platform", ImGuiTreeNodeFlags_DefaultOpen))
	    {
	    	ImGui::Text("CPU: %s",
#if HOST_CPU == CPU_X64
				"x86/64"
#elif HOST_CPU == CPU_GENERIC
				"Generic"
#elif HOST_CPU == CPU_ARM64
				"ARM64"
#else
				"Unknown"
#endif
					);
	    	ImGui::Text("Operating System: %s",
#if HOST_OS == OS_LINUX
				"Linux"
#elif HOST_OS == OS_DARWIN
				"OSX"
#elif HOST_OS == OS_WINDOWS
				"Windows"
#else
				"Unknown"
#endif
					);
	    }
	    if (ImGui::CollapsingHeader("Open GL", ImGuiTreeNodeFlags_DefaultOpen))
	    {
			ImGui::Text("Renderer: %s", (const char *)glGetString(GL_RENDERER));
			ImGui::Text("Version: %s", (const char *)glGetString(GL_VERSION));
	    }
		ImGui::PopStyleVar();
		ImGui::EndTabItem();
	}
}