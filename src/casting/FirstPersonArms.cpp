#include "casting/FirstPersonArms.h"

#include "casting/CastAnim.h"

namespace FirstPersonArms
{
	namespace
	{
		constexpr float kHidden = 0.01f;     // an upper arm scaled below this was hidden by a camera mod
		constexpr float kHideScale = 0.001f;  // what the camera mods use

		constexpr std::string_view kUpperArms[] = { "NPC L UpperArm [LUar]"sv, "NPC R UpperArm [RUar]"sv };

		// The upper arms of a skeleton, looked up again when it changes (load, race change)
		struct Arms
		{
			const RE::NiAVObject* root{ nullptr };
			RE::NiAVObject*       upper[2]{};

			void Find(RE::NiAVObject* a_root)
			{
				if (root != a_root) {
					root = a_root;
					for (int i = 0; i < 2; ++i) {
						upper[i] = a_root ? a_root->GetObjectByName(kUpperArms[i]) : nullptr;
					}
				}
			}
		};
		Arms  firstPerson;
		Arms  body;
		float hiddenScale[2]{};  // 1st person arm scale before we showed it, 0 = not shown by us
		bool  logged{ false };

		void Update(RE::NiAVObject* a_node, RE::NiUpdateData* a_data)
		{
			RE::NiUpdateData data = a_data ? *a_data : RE::NiUpdateData{};
			a_node->Update(data);
		}

		// After a cast: the arms go back to what the camera mod had, so they don't show for a frame
		void Restore(RE::NiUpdateData* a_data)
		{
			for (int i = 0; i < 2; ++i) {
				if (hiddenScale[i] > 0.0f && firstPerson.upper[i]) {
					firstPerson.upper[i]->local.scale = hiddenScale[i];
					Update(firstPerson.upper[i], a_data);
				}
				hiddenScale[i] = 0.0f;
			}
		}

		void ShowArms(RE::NiUpdateData* a_data)
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			const auto player = RE::PlayerCharacter::GetSingleton();
			const auto root = player && camera && camera->IsInFirstPerson() ? player->Get3D(true) : nullptr;
			firstPerson.Find(root);
			if (!root || !CastAnim::Playing()) {
				Restore(a_data);
				return;
			}
			bool shown = false;
			for (int i = 0; i < 2; ++i) {
				const auto upper = firstPerson.upper[i];
				if (upper && upper->local.scale < kHidden) {
					if (!logged) {
						logged = true;
						logs::info("The 1st person arms are hidden by another mod (camera mod?): shown during hotbar casting animations");
					}
					hiddenScale[i] = upper->local.scale;
					upper->local.scale = 1.0f;
					Update(upper, a_data);
				}
				shown = shown || hiddenScale[i] > 0.0f;
			}
			// the camera mod draws the 3rd person body: its arms (casting too) are left out of this frame, like the
			// camera mod does while it shows the 1st person arms itself
			if (shown) {
				body.Find(player->Get3D(false));
				for (const auto upper : body.upper) {
					if (upper && upper->local.scale >= kHidden) {
						const float scale = upper->local.scale;
						upper->local.scale = kHideScale;
						Update(upper, a_data);
						upper->local.scale = scale;  // only this frame's drawing: the next update brings them back
					}
				}
			}
		}

		// The player's 3D update (SE 39446 / AE 40522) animates the 3rd person body, then updates the 1st person
		// skeleton with this call (+0xD7). Improved Camera 1.x hooks the same call and hides the arms after it; 2.x hides
		// them before it. Installed after every plugin loaded, so our part runs after theirs, before the frame is drawn.
		struct FirstPersonUpdate
		{
			static void thunk(RE::NiAVObject* a_object, RE::NiUpdateData* a_data)
			{
				func(a_object, a_data);
				ShowArms(a_data);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void Install()
	{
		const auto address = REL::RelocationID(39446, 40522).address() + 0xD7;
		if (*reinterpret_cast<const std::uint8_t*>(address) != 0xE8) {
			logs::warn("1st person skeleton update call not found: casting arms hidden by camera mods stay hidden");
			return;
		}
		FirstPersonUpdate::func = SKSE::GetTrampoline().write_call<5>(address, FirstPersonUpdate::thunk);
		logs::info("Installed 1st person skeleton update hook");
	}
}
