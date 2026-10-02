#include "casting/Actions.h"
#include "core/Bindings.h"
#include "core/Config.h"
#include "ui/Framework.h"
#include "ui/Icons.h"
#include "core/Input.h"
#include "ui/ItemIcons.h"
#include "core/Keys.h"
#include "core/Lang.h"
#include "ui/UI.h"

namespace UI
{
	namespace
	{
		ImU32 Color(int r, int g, int b, float a)
		{
			return IM_COL32(r, g, b, static_cast<int>(std::clamp(a, 0.0f, 1.0f) * 255.0f));
		}

		// a configured color with the bar opacity applied
		ImU32 Color(const RGBA& a_color, float a_alpha)
		{
			const auto channel = [](float a_value) { return static_cast<int>(std::clamp(a_value, 0.0f, 1.0f) * 255.0f + 0.5f); };
			return IM_COL32(channel(a_color[0]), channel(a_color[1]), channel(a_color[2]), channel(a_color[3] * a_alpha));
		}

		void DrawIcon(ImDrawList* a_list, const Icons::Icon& a_icon, ImVec2 a_min, ImVec2 a_max, ImU32 a_tint)
		{
			if (!a_icon.Valid()) {
				return;
			}
			const auto texture = a_icon.texture ? a_icon.texture : AtlasTexture(a_icon.atlas);
			if (!texture) {
				return;
			}
			ImDrawListManager::AddImage(a_list, texture, a_min, a_max, ImVec2{ a_icon.u0, a_icon.v0 }, ImVec2{ a_icon.u1, a_icon.v1 }, a_tint);
		}

		// text with a dark outline, a_alpha fades the outline along with the text
		void DrawText(ImDrawList* a_list, float a_size, ImVec2 a_pos, const char* a_text, const RGBA& a_color, float a_alpha)
		{
			const auto  font = GetFont();
			const auto  shadow = Color(0, 0, 0, a_alpha * a_color[3] * 0.9f);
			const float o = std::max(1.0f, a_size / 14.0f);
			for (const auto& d : { ImVec2{ -o, 0 }, ImVec2{ o, 0 }, ImVec2{ 0, -o }, ImVec2{ 0, o } }) {
				ImDrawListManager::AddText(a_list, font, a_size, ImVec2{ a_pos.x + d.x, a_pos.y + d.y }, shadow, a_text);
			}
			ImDrawListManager::AddText(a_list, font, a_size, a_pos, Color(a_color, a_alpha), a_text);
		}

		void DrawText(ImDrawList* a_list, float a_size, ImVec2 a_pos, const std::string& a_text, const RGBA& a_color, float a_alpha)
		{
			DrawText(a_list, a_size, a_pos, a_text.c_str(), a_color, a_alpha);
		}

		ImVec2 TextSize(float a_size, const std::string& a_text)
		{
			const auto base = CalcTextSize(a_text.c_str());
			const float scale = a_size / std::max(1.0f, GetFontSize());
			return { base.x * scale, base.y * scale };
		}

		bool MenuOpen(std::string_view a_name)
		{
			return RE::UI::GetSingleton()->IsMenuOpen(a_name);
		}

		// Whether the bar should be drawn, and in which state. nullopt = hidden right away (menus, loading, paused);
		// visible = false fades it out (visibility setting: combat / weapon drawn).
		struct Context
		{
			bool  bindMenu{ false };
			bool  preview{ false };  // settings window open
			bool  visible{ true };
			float alpha{ 1.0f };
		};

		// Show / hide animation: fade plus a short slide in from the screen edge the bar is anchored to
		constexpr float kFadeInTime = 0.25f;
		constexpr float kFadeOutTime = 0.3f;
		constexpr float kOpacityTime = 0.3f;    // faded <-> full opacity (fade out of combat)
		constexpr float kSlideDistance = 14.0f;  // pixels at 1080p

		constexpr float kPeekTime = 1.5f;        // Oblivion style: the hidden main bar stays this long after a slot key

		float                                 shown{ 0.0f };      // 0 hidden .. 1 fully shown
		float                                 mainShown{ 0.0f };  // same for the main bar alone (Oblivion style hides it)
		float                                 opacity{ 0.0f };  // animated bar opacity
		std::chrono::steady_clock::time_point lastFrame{};

		float Approach(float a_value, float a_target, float a_step)
		{
			return a_value < a_target ? std::min(a_value + a_step, a_target) : std::max(a_value - a_step, a_target);
		}

		float SmoothStep(float a_t)
		{
			return a_t * a_t * (3.0f - 2.0f * a_t);
		}

		std::optional<Context> GetContext()
		{
			const auto  ui = RE::UI::GetSingleton();
			const auto  player = RE::PlayerCharacter::GetSingleton();
			const auto& settings = Config::Get();
			if (!ui || !player || !player->Is3DLoaded() || Icons::AtlasCount() == 0) {
				return std::nullopt;
			}
			if (MenuOpen(RE::MainMenu::MENU_NAME) || MenuOpen(RE::LoadingMenu::MENU_NAME) || MenuOpen(RE::FaderMenu::MENU_NAME) ||
				MenuOpen(RE::RaceSexMenu::MENU_NAME)) {
				return std::nullopt;
			}

			Context ctx;
			ctx.preview = IsBlockingWindowOpen();
			ctx.bindMenu = settings.showInMenus && Input::InBindMenu();

			if (!ctx.preview && !ctx.bindMenu) {
				if (!ui->IsShowingMenus() || ui->GameIsPaused()) {
					return std::nullopt;
				}
				if (MenuOpen(RE::DialogueMenu::MENU_NAME) || MenuOpen(RE::MapMenu::MENU_NAME) || MenuOpen(RE::BookMenu::MENU_NAME) ||
					MenuOpen(RE::LockpickingMenu::MENU_NAME) || MenuOpen(RE::Console::MENU_NAME)) {
					return std::nullopt;
				}

				const bool combat = player->IsInCombat();
				const bool drawn = player->AsActorState()->IsWeaponDrawn();
				switch (settings.visibility) {
				case Visibility::kNever:
					ctx.visible = false;
					break;
				case Visibility::kCombat:
					ctx.visible = combat;
					break;
				case Visibility::kWeaponDrawn:
					ctx.visible = drawn;
					break;
				case Visibility::kCombatOrWeaponDrawn:
					ctx.visible = combat || drawn;
					break;
				case Visibility::kSneaking:
					ctx.visible = player->AsActorState()->IsSneaking();
					break;
				default:
					break;
				}
				ctx.alpha = settings.fadeOutOfCombat && !combat ? settings.fadedOpacity : settings.opacity;
			} else {
				ctx.alpha = settings.opacity;
			}
			if (ctx.alpha <= 0.01f) {
				ctx.visible = false;
			}
			return ctx;
		}

		ImVec2 AnchorPoint(Anchor a_anchor, ImVec2 a_screen)
		{
			const int   idx = static_cast<int>(a_anchor);
			const float fx[] = { 0.0f, 0.5f, 1.0f };
			return { a_screen.x * fx[idx % 3], a_screen.y * fx[idx / 3] };
		}

		std::string ShortKeyName(std::uint32_t a_key)
		{
			return a_key == Keys::kNone ? std::string{} : Keys::Name(a_key);  // "-" is also the minus key's name
		}

		// Top left corner of a bar of the given size; a_slide moves it towards the screen edge it's anchored to
		ImVec2 Origin(Anchor a_anchor, float a_offsetX, float a_offsetY, ImVec2 a_size, ImVec2 a_screen, float a_scale, float a_slide)
		{
			const auto  anchor = AnchorPoint(a_anchor, a_screen);
			const int   idx = static_cast<int>(a_anchor);
			const float pivotX = (idx % 3) * 0.5f;
			const float pivotY = (idx / 3) * 0.5f;
			return { anchor.x - a_size.x * pivotX + a_offsetX * a_scale + (pivotX - 0.5f) * 2.0f * a_slide,
				anchor.y - a_size.y * pivotY + a_offsetY * a_scale + (pivotY - 0.5f) * 2.0f * a_slide };
		}

		// The game's Shout key, shown on the power slot of the Oblivion style ready slots
		std::uint32_t ShoutKey()
		{
			const auto controlMap = RE::ControlMap::GetSingleton();
			const auto userEvents = RE::UserEvents::GetSingleton();
			if (!controlMap || !userEvents) {
				return Keys::kNone;
			}
			const auto key = controlMap->GetMappedKey(userEvents->shout, RE::INPUT_DEVICE::kKeyboard);
			return key == 0xFF || key == static_cast<std::uint32_t>(-1) ? Keys::kNone : key;
		}

		// Everything slots are drawn with, worked out once per frame
		struct SlotStyle
		{
			ImDrawList* list{ nullptr };
			const BarColors* colors{ nullptr };
			bool        showCooldownText{ true };
			bool        showItemCount{ true };
			float       size{ 0.0f };
			float       alpha{ 1.0f };
			float       labelSize{ 0.0f };
			float       smallSize{ 0.0f };
			float       inset{ 0.0f };
			Icons::Icon barEmpty;
			Icons::Icon barOverlay;
			Icons::Icon barHighlight;
			ImU32       white{};
			ImU32       frame{};
			ImU32       background{};
			ImU32       unavailable{};
			ImU32       noMagicka{};
			ImU32       equipped{};
			ImU32       cooldown{};
			ImU32       channel{};
			ImU32       charge{};
			ImU32       chargeBack{};
		};

		SlotStyle MakeStyle(const Settings& a_settings, float a_size, float a_alpha)
		{
			const auto& colors = a_settings.colors;
			SlotStyle   st;
			st.list = GetBackgroundDrawList();
			st.colors = &colors;
			st.showCooldownText = a_settings.showCooldownText;
			st.showItemCount = a_settings.showItemCount;
			st.size = a_size;
			st.alpha = a_alpha;
			st.labelSize = std::max(10.0f, a_size * 0.30f);
			st.smallSize = std::max(9.0f, a_size * 0.26f);
			st.inset = a_size * 0.06f;
			// slot frame: the Nordic UI style
			st.barEmpty = Icons::Named("BAR_EMPTY", true);
			st.barOverlay = Icons::Named("BAR_OVERLAY", true);
			st.barHighlight = Icons::Named("BAR_HIGHLIGHT");
			st.white = Color(255, 255, 255, a_alpha);
			st.frame = Color(colors.frame, a_alpha);
			st.background = Color(colors.slotBackground, a_alpha);
			st.unavailable = Color(110, 110, 110, a_alpha * 0.6f);
			st.noMagicka = Color(colors.noMagicka, a_alpha);
			st.equipped = Color(colors.equipped, a_alpha);
			st.cooldown = Color(colors.cooldown, a_alpha);
			st.channel = Color(colors.channeling, a_alpha);
			st.charge = Color(colors.chargeBar, a_alpha);
			st.chargeBack = Color(0, 0, 0, a_alpha * 0.7f);
			return st;
		}

		// One slot at a_p0. a_marked = equipped highlight, a_cast = this slot's spell is charging / channeling.
		void DrawSlot(const SlotStyle& st, ImVec2 p0, RE::TESForm* a_form, Hand a_hand, bool a_marked, const Actions::CastInfo* a_cast, const std::string& a_key)
		{
			const auto   list = st.list;
			const auto&  colors = *st.colors;
			const float  size = st.size;
			const float  alpha = st.alpha;
			const float  inset = st.inset;
			const ImVec2 p1{ p0.x + size, p0.y + size };

			DrawIcon(list, st.barEmpty, p0, p1, st.background);

			if (a_form) {
				const bool available = Actions::IsAvailable(a_form);
				const bool affordable = Actions::CanAfford(a_form, a_hand);
				const auto tint = !available ? st.unavailable : !affordable ? st.noMagicka : st.white;
				DrawIcon(list, Icons::ForForm(a_form), ImVec2{ p0.x + inset, p0.y + inset }, ImVec2{ p1.x - inset, p1.y - inset }, tint);

				if (a_marked) {
					DrawIcon(list, st.barHighlight, p0, p1, st.equipped);
				}

				// cooldown
				const auto [remaining, total] = Actions::Cooldown(a_form);
				if (remaining > 0.0f && total > 0.0f) {
					const float frac = std::clamp(remaining / total, 0.0f, 1.0f);
					ImDrawListManager::AddRectFilled(list, ImVec2{ p0.x + inset, p1.y - inset - (size - 2 * inset) * frac }, ImVec2{ p1.x - inset, p1.y - inset },
						st.cooldown, 0.0f, 0);
					if (st.showCooldownText && remaining >= 0.95f) {
						const auto text = remaining < 10.0f ? std::format("{:.1f}", remaining) : std::format("{:.0f}", remaining);
						const auto ts = TextSize(st.labelSize, text);
						DrawText(list, st.labelSize, ImVec2{ p0.x + (size - ts.x) * 0.5f, p0.y + (size - ts.y) * 0.5f }, text, colors.text, alpha);
					}
				}

				// charge / channel progress
				if (a_cast) {
					if (a_cast->channeling) {
						DrawIcon(list, st.barHighlight, p0, p1, st.channel);
					} else {
						const float barH = std::max(3.0f, size * 0.08f);
						ImDrawListManager::AddRectFilled(list, ImVec2{ p0.x, p1.y + 2 }, ImVec2{ p1.x, p1.y + 2 + barH }, st.chargeBack, 0.0f, 0);
						ImDrawListManager::AddRectFilled(list, ImVec2{ p0.x, p1.y + 2 }, ImVec2{ p0.x + size * a_cast->progress, p1.y + 2 + barH },
							st.charge, 0.0f, 0);
					}
				}

				// hand marker
				if (a_hand != Hand::kAuto && (a_form->Is(RE::FormType::Spell) || a_form->Is(RE::FormType::Scroll) || a_form->Is(RE::FormType::Weapon))) {
					const char* hand = a_hand == Hand::kLeft ? "L" : a_hand == Hand::kRight ? "R" : "D";
					DrawText(list, st.smallSize, ImVec2{ p0.x + size * 0.08f, p0.y + size * 0.04f }, hand, colors.handMarker, alpha);
				}

				// item count
				if (st.showItemCount) {
					if (const auto items = Actions::ItemCount(a_form); items >= 0) {
						DrawText(list, st.smallSize, ImVec2{ p0.x + size * 0.08f, p1.y - st.smallSize - size * 0.04f }, std::to_string(items), colors.text, alpha);
					}
				}
			}

			DrawIcon(list, st.barOverlay, p0, p1, st.frame);

			if (!a_key.empty()) {
				const auto ts = TextSize(st.labelSize, a_key);
				DrawText(list, st.labelSize, ImVec2{ p1.x - ts.x - size * 0.07f, p1.y - ts.y - size * 0.03f }, a_key, colors.keyLabel, alpha);
			}
		}
	}

	void __stdcall RenderHud()
	{
		std::scoped_lock lock(Config::Lock());

		Input::Update();
		Config::SaveIfDirty();
		ItemIcons::Update();

		const auto ctx = GetContext();
		if (!ctx) {
			return;
		}

		// animate towards the wanted state; while binding or in the settings the bar is just there
		const auto now = std::chrono::steady_clock::now();
		const float dt = lastFrame == std::chrono::steady_clock::time_point{} ?
		                     0.0f :
		                     std::min(std::chrono::duration<float>(now - lastFrame).count(), 0.1f);
		lastFrame = now;
		const auto& settings = Config::Get();
		const auto  page = Input::CurrentPage();
		const bool  oblivion = settings.keyMode == KeyMode::kOblivion;
		// Oblivion style: the main bar only shows up while picking or binding (slot key, extra bar key held), always in the
		// settings preview
		const bool wantMain = !oblivion || !settings.readyHideMainBar || page != Page::kMain || Input::SinceSlotKey() < kPeekTime;
		if (ctx->preview) {
			mainShown = 1.0f;
		} else {
			const bool mainAppearing = wantMain && mainShown < 1.0f;
			mainShown = Approach(mainShown, wantMain ? 1.0f : 0.0f, dt / (mainAppearing ? kFadeInTime : kFadeOutTime));
		}
		if (ctx->preview || ctx->bindMenu) {
			shown = 1.0f;
			opacity = ctx->alpha;
		} else {
			if (shown <= 0.0f) {
				opacity = ctx->alpha;  // fully hidden: only the show animation fades it in
			}
			const bool appearing = ctx->visible && shown < 1.0f;
			shown = Approach(shown, ctx->visible ? 1.0f : 0.0f, dt / (appearing ? kFadeInTime : kFadeOutTime));
			opacity = Approach(opacity, ctx->alpha, dt / kOpacityTime);
		}
		const float eased = SmoothStep(shown);
		const float alpha = opacity * eased;
		if (alpha <= 0.01f) {
			return;
		}
		const float mainEased = SmoothStep(std::min(shown, mainShown));
		const float mainAlpha = opacity * mainEased;

		const auto& colors = settings.colors;
		const auto  io = GetIO();
		const auto  screen = io->DisplaySize;
		const float scale = screen.y / 1080.0f;
		const float size = settings.iconSize * scale;
		const float spacing = settings.spacing * scale;

		const int count = settings.slotCount;
		const int cols = std::clamp(settings.columns, 1, count);
		const int rows = (count + cols - 1) / cols;
		const float width = cols * size + (cols - 1) * spacing;
		const float height = rows * size + (rows - 1) * spacing;

		// while showing / hiding the bars sit a bit towards the screen edge they're anchored to (bottom bar: lower)
		const float slide = (1.0f - eased) * kSlideDistance * scale;
		const auto  origin = Origin(settings.anchor, settings.offsetX, settings.offsetY, { width, height }, screen, scale,
			(1.0f - mainEased) * kSlideDistance * scale);

		const auto castInfo = Actions::CurrentCast();
		const auto selection = ctx->bindMenu ? Input::MenuSelection() : nullptr;
		const auto style = MakeStyle(settings, size, alpha);
		const auto mainStyle = MakeStyle(settings, size, mainAlpha);
		const auto readySpell = Bindings::GetForm(Page::kMain, kReadySpellSlot);
		const auto readyPotion = Bindings::GetForm(Page::kMain, kReadyPotionSlot);

		for (int slot = 0; slot < count && mainAlpha > 0.01f; ++slot) {
			const auto form = Bindings::GetForm(page, slot);
			if (!form && !settings.showEmptySlots && !ctx->bindMenu && !ctx->preview) {
				continue;
			}
			const ImVec2 p0{ origin.x + (slot % cols) * (size + spacing), origin.y + (slot / cols) * (size + spacing) };
			// equipped (spell in hand, current power, worn item) or picked for the Oblivion style cast / potion key
			const bool marked = form && (Actions::IsEquipped(form) || (oblivion && (form == readySpell || form == readyPotion)));
			const bool casting = castInfo && castInfo->page == page && castInfo->slot == slot;
			DrawSlot(mainStyle, p0, form, Bindings::Get(page, slot).hand, marked, casting ? &*castInfo : nullptr,
				settings.showKeyLabels ? ShortKeyName(settings.slotKeys[slot]) : std::string{});
		}

		// Oblivion style ready slots: the picked spell, the picked potion and the current power, labelled with the
		// keys that use them
		const int  n = settings.readyShowPower ? 3 : 2;
		const auto extent = settings.readyVertical ? ImVec2{ size, n * size + (n - 1) * spacing } : ImVec2{ n * size + (n - 1) * spacing, size };
		const auto readyOrigin = Origin(settings.readyAnchor, settings.readyOffsetX, settings.readyOffsetY, extent, screen, scale, slide);
		if (oblivion) {
			const auto step = settings.readyVertical ? ImVec2{ 0.0f, size + spacing } : ImVec2{ size + spacing, 0.0f };
			for (int i = 0; i < n; ++i) {
				const ImVec2 p0{ readyOrigin.x + i * step.x, readyOrigin.y + i * step.y };
				if (i == 2) {
					const auto power = RE::PlayerCharacter::GetSingleton()->GetActorRuntimeData().selectedPower;
					DrawSlot(style, p0, power, Hand::kAuto, false, nullptr, ShortKeyName(ShoutKey()));
					continue;
				}
				const int  slot = i == 0 ? kReadySpellSlot : kReadyPotionSlot;
				const bool casting = castInfo && castInfo->slot == slot;
				DrawSlot(style, p0, i == 0 ? readySpell : readyPotion, Bindings::Get(Page::kMain, slot).hand, false, casting ? &*castInfo : nullptr,
					ShortKeyName(i == 0 ? settings.castKey : settings.potionKey));
			}
		}

		// header line above the bar: page name / binding help
		std::string header;
		if (ctx->bindMenu) {
			header = selection ? Lang::F("Press a slot key to bind {}", selection->GetName()) : Lang::T("Select a spell or item, then press a slot key");
			if (page != Page::kMain) {
				header += "  " + Lang::F("[holding {}]", Keys::Name(settings.modifierKeys[static_cast<int>(page) - static_cast<int>(Page::kModifier1)]));
			}
		} else if (settings.showPageName && page != Page::kMain) {
			header = Keys::Name(settings.modifierKeys[static_cast<int>(page) - static_cast<int>(Page::kModifier1)]);
		}
		// above the main bar, or while binding with the main bar hidden (Oblivion style) above the small bar
		const bool aboveMain = mainAlpha > 0.01f;
		if (!header.empty() && (aboveMain || (oblivion && ctx->bindMenu))) {
			const auto  boxOrigin = aboveMain ? origin : readyOrigin;
			const auto  box = aboveMain ? ImVec2{ width, height } : extent;
			const float hs = std::max(12.0f, size * 0.32f);
			const auto  ts = TextSize(hs, header);
			float       y = boxOrigin.y - ts.y - spacing - 2.0f;
			if (y < 0.0f) {
				y = boxOrigin.y + box.y + spacing + 4.0f;
			}
			const float x = std::clamp(boxOrigin.x + (box.x - ts.x) * 0.5f, 0.0f, std::max(0.0f, screen.x - ts.x));
			const float headerAlpha = aboveMain ? std::max(mainAlpha, 0.8f * mainEased) : alpha;
			DrawText(style.list, hs, ImVec2{ x, y }, header, colors.text, headerAlpha);
		}
	}
}
