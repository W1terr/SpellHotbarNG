#pragma once

// SKSE Menu Framework 3 API (https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API, LGPL-2.1).
// It pulls in windows.h, so only the UI translation units include it.
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#pragma warning(push, 0)
#include "SKSEMenuFramework.h"
#pragma warning(pop)

#undef GetObject
#undef PlaySound

namespace UI
{
	using namespace ImGuiMCP;

	void __stdcall RenderHud();

	void __stdcall RenderBarPage();
	void __stdcall RenderBindingsPage();
	void __stdcall RenderIconsPage();
	void __stdcall RenderProfilesPage();

	// Texture of an icon atlas, loaded on first use (nullptr if it failed)
	ImTextureID AtlasTexture(int a_atlas);

	// The sidebar page names follow a language change: PageNamesOutdated() after it, UpdatePageNames() renames them on
	// the next HUD frame (not while the framework draws its pages). Older framework versions can't rename pages
	// (CanRenamePages() false), there the names change after a restart.
	bool CanRenamePages();
	void PageNamesOutdated();
	void UpdatePageNames();
}
