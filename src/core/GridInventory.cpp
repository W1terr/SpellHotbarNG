#include "core/GridInventory.h"

#include "GridInventoryAPI.h"

namespace GridInventory
{
	namespace
	{
		constexpr std::string_view kMenuName = "GridInventoryMenu";
		constexpr RE::FormID       kPlayerRef = 0x14;
		constexpr auto             kHoverTimeout = 150ms;  // no overlay asked for this long: the mouse left the tiles

		// written on Grid Inventory's render thread, read by the hotbar's input / HUD
		std::atomic<RE::FormID>          hovered{ 0 };
		std::atomic<std::int64_t>        hoveredAt{ 0 };  // steady_clock ticks
		std::atomic<bool>                asked{ false };  // Grid Inventory called us at least once

		std::int64_t Now()
		{
			return std::chrono::steady_clock::now().time_since_epoch().count();
		}

		// Called for the hovered tile only, every frame: remember it, draw nothing
		bool GetOverlay(void*, const GridInvAPI::ItemKey* a_key, GridInvAPI::Overlay*)
		{
			if (a_key && a_key->owner == kPlayerRef) {
				hovered = a_key->base;
				hoveredAt = Now();
				if (!asked.exchange(true)) {
					logs::info("Grid Inventory: hovered items are reported, slot keys bind them");
				}
			}
			return false;
		}

		std::uint32_t GetTooltipLines(void*, const GridInvAPI::ItemKey*, GridInvAPI::TooltipLine*, std::uint32_t)
		{
			return 0;
		}

		std::uint32_t OfferDrop(void*, const GridInvAPI::DropQuery*)
		{
			return GridInvAPI::kDropReject;
		}

		const GridInvAPI::Provider kProvider{ sizeof(GridInvAPI::Provider), GridInvAPI::kABIVersion, "Spell Hotbar NG (hovered item for binding)",
			nullptr, GetOverlay, GetTooltipLines, OfferDrop };

		// ---- icon cache ---------------------------------------------------------------------------
		// GridInventory_icons.pak: records of magic 'FIC5'..'FIC9' (4), key (8), width, height, DXGI format (4 each),
		// for 'FIC6' / 'FIC7' / 'FIC9' a 36 byte rotation, pixel byte count (4), then width * height * 4 bytes. The key's
		// upper half is the picture slot (PictureSlot), the lower half Grid Inventory's capture angle. 'FIC8' / 'FIC9'
		// are captured with real transparency; the older ones over a magenta backdrop (only used when nothing newer).

		constexpr const char* kPakPath = "Data/SKSE/Plugins/GridInventory_icons.pak";

		struct Record
		{
			std::uint64_t offset{ 0 };  // of the pixels
			std::uint32_t width{ 0 };
			std::uint32_t height{ 0 };
			std::uint32_t format{ 0 };
			bool          modern{ false };
		};

		std::mutex                                  pakLock;
		std::unordered_map<std::uint32_t, Record>   pak;        // slot -> newest picture
		std::uintmax_t                              pakSize{ static_cast<std::uintmax_t>(-1) };

		constexpr std::uint32_t Magic(char a_version)
		{
			return 0x00434946u | (static_cast<std::uint32_t>(a_version) << 24);  // "FIC" + version
		}

		void ScanPak()
		{
			std::error_code ec;
			const auto      size = std::filesystem::file_size(kPakPath, ec);
			if (ec || size == pakSize) {
				return;
			}
			pakSize = size;
			pak.clear();
			std::ifstream in(kPakPath, std::ios::binary);
			std::uint64_t at = 0;
			while (in) {
				std::uint32_t magic = 0, w = 0, h = 0, format = 0, length = 0;
				std::uint64_t key = 0;
				if (!in.read(reinterpret_cast<char*>(&magic), 4)) {
					break;
				}
				const bool modern = magic == Magic('8') || magic == Magic('9');
				const bool rotation = magic == Magic('6') || magic == Magic('7') || magic == Magic('9');
				if (!modern && magic != Magic('5') && !rotation) {
					break;
				}
				in.read(reinterpret_cast<char*>(&key), 8);
				in.read(reinterpret_cast<char*>(&w), 4);
				in.read(reinterpret_cast<char*>(&h), 4);
				in.read(reinterpret_cast<char*>(&format), 4);
				if (rotation) {
					in.seekg(36, std::ios::cur);
				}
				in.read(reinterpret_cast<char*>(&length), 4);
				const std::uint64_t pixels = at + 28 + (rotation ? 36 : 0);
				if (!in || w == 0 || h == 0 || w > 2048 || h > 2048 || length != w * h * 4 || pixels + length > size) {
					break;
				}
				const auto slot = static_cast<std::uint32_t>(key >> 32);
				if (const auto it = pak.find(slot); it == pak.end() || modern || !it->second.modern) {
					pak[slot] = { pixels, w, h, format, modern };  // newer wins, a modern one over an old one
				}
				at = pixels + length;
				in.seekg(static_cast<std::streamoff>(at));
			}
			logs::info("Grid Inventory icon cache: {} pictures", pak.size());
		}

		// Fits the picture into a square, area-averaged with premultiplied alpha
		std::vector<std::uint8_t> Fit(const std::vector<std::uint8_t>& a_px, const Record& a_rec, int a_size)
		{
			const bool  bgra = a_rec.format == 87 || a_rec.format == 88;  // DXGI B8G8R8A8 (else R8G8B8A8)
			const int   w = static_cast<int>(a_rec.width), h = static_cast<int>(a_rec.height);
			const float scale = static_cast<float>(a_size) / static_cast<float>(std::max(w, h));
			const int   dw = std::max(1, static_cast<int>(std::lround(w * scale)));
			const int   dh = std::max(1, static_cast<int>(std::lround(h * scale)));
			const int   ox = (a_size - dw) / 2, oy = (a_size - dh) / 2;
			std::vector<std::uint8_t> out(static_cast<std::size_t>(a_size) * a_size * 4, 0);
			for (int y = 0; y < dh; ++y) {
				const int y0 = y * h / dh, y1 = std::max(y0 + 1, (y + 1) * h / dh);
				for (int x = 0; x < dw; ++x) {
					const int x0 = x * w / dw, x1 = std::max(x0 + 1, (x + 1) * w / dw);
					float r = 0, g = 0, b = 0, a = 0;
					int   n = 0;
					for (int sy = y0; sy < y1; ++sy) {
						for (int sx = x0; sx < x1; ++sx) {
							const auto* p = &a_px[(static_cast<std::size_t>(sy) * w + sx) * 4];
							const float pa = p[3] / 255.0f;
							r += (bgra ? p[2] : p[0]) * pa;
							g += p[1] * pa;
							b += (bgra ? p[0] : p[2]) * pa;
							a += pa;
							++n;
						}
					}
					auto* o = &out[(static_cast<std::size_t>(y + oy) * a_size + (x + ox)) * 4];
					if (a > 0.0f) {
						o[0] = static_cast<std::uint8_t>(std::clamp(b / a, 0.0f, 255.0f));
						o[1] = static_cast<std::uint8_t>(std::clamp(g / a, 0.0f, 255.0f));
						o[2] = static_cast<std::uint8_t>(std::clamp(r / a, 0.0f, 255.0f));
						o[3] = static_cast<std::uint8_t>(std::clamp(a / n * 255.0f + 0.5f, 0.0f, 255.0f));
					}
				}
			}
			return out;
		}

		std::uint32_t Fnv(std::uint32_t a_hash, const char* a_path)
		{
			const char* s = a_path;
			if (_strnicmp(s, "meshes", 6) == 0 && (s[6] == '\\' || s[6] == '/')) {
				s += 7;
			}
			for (; *s; ++s) {
				char c = *s;
				if (c >= 'A' && c <= 'Z') {
					c += 32;
				}
				if (c == '/') {
					c = '\\';
				}
				a_hash = (a_hash ^ static_cast<std::uint8_t>(c)) * 16777619u;
			}
			return a_hash;
		}
	}

	bool Installed()
	{
		static const bool installed = REX::W32::GetModuleHandleW(L"GridInventory.dll") != nullptr;
		return installed;
	}

	void OfferExtension()
	{
		const auto messaging = SKSE::GetMessagingInterface();
		// the host copies the struct; Dispatch returns false if Grid Inventory isn't installed
		if (messaging && messaging->Dispatch(GridInvAPI::kMsgRegisterProvider, const_cast<GridInvAPI::Provider*>(&kProvider),
							 sizeof(GridInvAPI::Provider), GridInvAPI::kHostPluginName)) {
			logs::info("Grid Inventory found: offered the hotbar as its extension (taken unless another mod's extension came first)");
		}
	}

	bool MenuOpen()
	{
		const auto ui = RE::UI::GetSingleton();
		return ui && ui->IsMenuOpen(kMenuName);
	}

	RE::TESForm* Hovered()
	{
		const auto id = hovered.load();
		const auto age = std::chrono::steady_clock::duration(Now() - hoveredAt.load());
		if (!id || age > kHoverTimeout || !MenuOpen()) {
			return nullptr;
		}
		return RE::TESForm::LookupByID(id);
	}

	// Grid Inventory's ModelSlot32 for items: FNV-1a of the ground model path (armor: both sexes', and which sex
	// wears it when they differ), the FormID for items without a model or with alternate textures
	std::uint32_t PictureSlot(RE::TESForm* a_form)
	{
		const char*   path = nullptr;
		const char*   path2 = nullptr;
		std::uint32_t alternates = 0;
		if (const auto armor = a_form->As<RE::TESObjectARMO>()) {
			const auto& male = armor->worldModels[RE::SEXES::kMale];
			const auto& female = armor->worldModels[RE::SEXES::kFemale];
			path = male.GetModel();
			path2 = female.GetModel();
			alternates = male.numAlternateTextures + female.numAlternateTextures;
		} else if (const auto model = skyrim_cast<RE::TESModel*>(a_form)) {
			path = model->GetModel();
			if (const auto swap = skyrim_cast<RE::TESModelTextureSwap*>(a_form)) {
				alternates = swap->numAlternateTextures;
			}
		}
		if (!path || !*path || alternates > 0) {
			return a_form->GetFormID();
		}
		auto hash = Fnv(2166136261u, path);
		if (path2 && *path2) {
			hash = Fnv((hash ^ 0x1Fu) * 16777619u, path2);
			if (_stricmp(path, path2) != 0) {
				const auto player = RE::PlayerCharacter::GetSingleton();
				const auto base = player ? player->GetActorBase() : nullptr;
				const bool female = base && base->GetSex() == RE::SEX::kFemale;
				hash = (hash ^ (female ? 0xF1u : 0x4Du)) * 16777619u;
			}
		}
		return hash;
	}

	bool Picture(RE::TESForm* a_form, int a_size, std::vector<std::uint8_t>& a_bgra)
	{
		if (!a_form) {
			return false;
		}
		std::scoped_lock lock(pakLock);
		ScanPak();
		auto it = pak.find(PictureSlot(a_form));
		if (it == pak.end()) {
			it = pak.find(a_form->GetFormID());  // captured before Grid Inventory keyed pictures by model
		}
		if (it == pak.end()) {
			return false;
		}
		const auto&               rec = it->second;
		std::vector<std::uint8_t> pixels(static_cast<std::size_t>(rec.width) * rec.height * 4);
		std::ifstream             in(kPakPath, std::ios::binary);
		in.seekg(static_cast<std::streamoff>(rec.offset));
		if (!in.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()))) {
			return false;
		}
		a_bgra = Fit(pixels, rec, a_size);
		return true;
	}
}
