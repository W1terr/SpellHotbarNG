#include "ui/UI.h"

#include "core/Config.h"
#include "ui/Framework.h"
#include "ui/Icons.h"
#include "core/Input.h"
#include "core/Lang.h"

namespace UI
{
	namespace
	{
		std::vector<ImTextureID> textures;
		std::vector<bool>        textureTried;

		bool __stdcall OnInput(RE::InputEvent* a_event)
		{
			return Input::OnInputEvent(a_event);
		}

		bool IsFrameworkInstalled()
		{
			return SKSEMenuFramework::IsInstalled() && GetMenuFrameworkModule() != nullptr;
		}

		void __stdcall OnFrameworkEvent(SKSEMenuFramework::Model::EventType a_type)
		{
			if (a_type == SKSEMenuFramework::Model::EventType::kCloseMenu) {
				Input::CancelCapture();
				Config::Save();
			}
		}

		constexpr auto kSection = "Spell Hotbar NG";

		// The sidebar pages: English name (the translation key), page, name it has in the framework now
		struct SidebarPage
		{
			const char*                            english;
			SKSEMenuFramework::Model::RenderFunction render;
			std::string                            shown;
		};
		std::array pages{
			SidebarPage{ "Bindings", RenderBindingsPage, {} },
			SidebarPage{ "Bar Layout", RenderBarPage, {} },
			SidebarPage{ "Icons", RenderIconsPage, {} },
			SidebarPage{ "Profiles", RenderProfilesPage, {} },
		};
		std::atomic<bool> pageNamesOutdated{ false };

		// exported by newer SKSE Menu Framework versions, not in its header we use: renames a section or page at runtime
		using RenameSectionFunction = bool (*)(const char* a_path, const char* a_newName);

		RenameSectionFunction RenameSection()
		{
			static const auto func = SKSEMenuFramework::Model::Internal::GetFunction<RenameSectionFunction>("RenameSection");
			return func;
		}
	}

	bool CanRenamePages()
	{
		return RenameSection() != nullptr;
	}

	void PageNamesOutdated()
	{
		pageNamesOutdated = true;
	}

	void UpdatePageNames()
	{
		if (!pageNamesOutdated.exchange(false) || !RenameSection()) {
			return;
		}
		for (auto& page : pages) {
			const std::string wanted = Lang::T(page.english);
			if (wanted == page.shown) {
				continue;
			}
			if (RenameSection()(std::format("{}/{}", kSection, page.shown).c_str(), wanted.c_str())) {
				page.shown = wanted;
			} else {
				logs::warn("SKSE Menu Framework didn't rename the page \"{}\" to \"{}\"", page.shown, wanted);
			}
		}
	}

	bool IsBlockingWindowOpen()
	{
		return IsFrameworkInstalled() && SKSEMenuFramework::IsAnyBlockingWindowOpened();
	}

	bool Register()
	{
		if (!IsFrameworkInstalled()) {
			logs::critical("SKSE Menu Framework is not installed, Spell Hotbar NG can't draw or be configured");
			return false;
		}
		logs::info("SKSE Menu Framework {} found", SKSEMenuFramework::GetMenuFrameworkVersion());

		SKSEMenuFramework::SetSection(kSection);
		for (auto& page : pages) {
			page.shown = Lang::T(page.english);
			SKSEMenuFramework::AddSectionItem(page.shown, page.render);
		}

		SKSEMenuFramework::AddHudElement(RenderHud);
		SKSEMenuFramework::AddInputEvent(OnInput);
		SKSEMenuFramework::AddEvent(OnFrameworkEvent, 0.0f);
		return true;
	}

	ImTextureID AtlasTexture(int a_atlas)
	{
		if (a_atlas < 0 || a_atlas >= Icons::AtlasCount()) {
			return nullptr;
		}
		if (textures.size() < static_cast<std::size_t>(Icons::AtlasCount())) {
			textures.resize(Icons::AtlasCount(), nullptr);
			textureTried.resize(Icons::AtlasCount(), false);
		}
		if (!textureTried[a_atlas]) {
			textureTried[a_atlas] = true;
			const auto& path = Icons::AtlasPath(a_atlas);
			textures[a_atlas] = SKSEMenuFramework::LoadTexture(path);
			if (textures[a_atlas]) {
				logs::info("Loaded texture {}", path);
			} else {
				logs::error("Failed to load texture {}", path);
			}
		}
		return textures[a_atlas];
	}
}
