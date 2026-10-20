/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "types.h"
#include "nulldc_version.h"
#include "gui.h"
#include "imgui/imgui.h"
#include "gui_partials.h"
#include "gui_util.h"
#include "rend/gles/imgui_impl_opengl3.h"
#include "cfg/cfg.h"
#include "oslib/oslib.h"

float CalcScreenScale(const char* headline) {
	auto size = ImGui::CalcTextSize(headline);

	float scale = 0;

	scale = screen_width/(size.x + 10);

	if (size.y * scale / screen_height > 0.4) {
		scale = (screen_height * 0.4) / size.y;
	}
	return scale;
}

void DrawTextCentered(const char* text) {
	auto size = ImGui::CalcTextSize(text);
	ImGui::SetCursorPosX(screen_width/2 - size.x / 2);
	ImGui::Text("%s", text);
}

void gui_welcome(ImFont* font64) {

	static auto endTime = os_GetSeconds() + 0;

	if (os_GetSeconds() > endTime) {
		gui_state = Main;
		return;
	}

	ImGui_Impl_NewFrame();
	ImGui::NewFrame();

	ImGui_ImplOpenGL3_DrawBackground();

	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(screen_width, screen_height));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);

	ImGui::PushFont(font64);
	ImGui::Begin("Welcome", NULL, /*ImGuiWindowFlags_AlwaysAutoResize |*/ ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
	
	
	ImGui::SetWindowFontScale(1);
	float scale = CalcScreenScale("nullDC");
	// the version whole, as the build made it (3.0.0-pre, 3.0.0-pre+g<h>, ...), no wider than the screen
	const char* version = nulldc_version();
	float version_scale = scale / 3.5f;
	if (CalcScreenScale(version) < version_scale)
		version_scale = CalcScreenScale(version);

	ImGui::SetWindowFontScale(scale);

	DrawTextCentered("nullDC");

	ImGui::SetWindowFontScale(version_scale);
	DrawTextCentered(version);

	ImGui::End();
	ImGui::PopFont();
	ImGui::PopStyleVar();

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData(), false);
}