#include "ui/UI.h"

#include "core/Config.h"
#include "ui/Framework.h"
#include "ui/Icons.h"
#include "core/Input.h"

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

		SKSEMenuFramework::SetSection("Spell Hotbar NG");
		SKSEMenuFramework::AddSectionItem("Bindings", RenderBindingsPage);
		SKSEMenuFramework::AddSectionItem("Bar Layout", RenderBarPage);
		SKSEMenuFramework::AddSectionItem("Profiles", RenderProfilesPage);

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
