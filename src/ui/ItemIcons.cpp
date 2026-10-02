#include "ui/ItemIcons.h"

#include "core/Config.h"
#include "ui/Framework.h"

namespace ItemIcons
{
	namespace
	{
		namespace W = REX::W32;

		constexpr int   kIconSize = 160;      // pixels of a saved icon
		constexpr float kMargin = 1.08f;      // room around the model inside the icon
		constexpr float kAlphaCut = 0.04f;    // more transparent than this doesn't count as model (crop)
		constexpr int   kLoadTimeout = 300;   // frames a model may take to load
		constexpr int   kSettleFrames = 2;    // frames after loading before the picture (transforms, textures)
		constexpr int   kEmptyRetries = 5;    // captures without any model pixels before giving up

		constexpr const char* kMenuName = "SpellHotbarNG_IconCapture";

		enum class State
		{
			kUnknown,
			kQueued,
			kReady,   // file exists
			kFailed   // this session
		};

		struct Entry
		{
			State       state{ State::kUnknown };
			std::string path;
			void*       texture{ nullptr };
		};

		struct Job
		{
			std::string key;
			RE::FormID  form{ 0 };
			bool        ownScene{ false };  // we started the 3D view (no game menu showing items)
			int         frames{ 0 };
			int         settled{ 0 };
			int         empty{ 0 };
		};

		std::recursive_mutex                         lock;
		std::unordered_map<std::string, Entry>       byModel;   // lower case model path -> icon
		std::unordered_map<RE::FormID, std::string>  modelOf;   // form -> model key ("" = no model)
		std::deque<std::pair<std::string, RE::FormID>> queue;
		std::optional<Job>                           job;
		std::chrono::steady_clock::time_point        menuRequestTime{};  // last show / hide message

		std::filesystem::path Dir()
		{
			return Config::DataDir() / "item_icons";
		}

		// The model the inventory shows: the ground model for armor, the object's model otherwise
		std::string ModelKey(RE::TESForm* a_form)
		{
			const char* path = nullptr;
			if (const auto armor = a_form->As<RE::TESObjectARMO>()) {
				path = armor->worldModels[RE::SEXES::kMale].GetModel();
				if (!path || !*path) {
					path = armor->worldModels[RE::SEXES::kFemale].GetModel();
				}
			} else if (const auto model = a_form->As<RE::TESModel>()) {
				path = model->GetModel();
			}
			std::string key = path ? path : "";
			std::ranges::transform(key, key.begin(), [](unsigned char c) { return static_cast<char>(c == '/' ? '\\' : std::tolower(c)); });
			return key;
		}

		std::string FileFor(const std::string& a_key)
		{
			std::uint64_t hash = 1469598103934665603ull;  // FNV-1a
			for (const unsigned char c : a_key) {
				hash = (hash ^ c) * 1099511628211ull;
			}
			return (Dir() / std::format("{:016x}.dds", hash)).string();
		}

		const std::string& KeyOf(RE::TESForm* a_form)
		{
			const auto id = a_form->GetFormID();
			auto       it = modelOf.find(id);
			if (it == modelOf.end()) {
				it = modelOf.emplace(id, ModelKey(a_form)).first;
			}
			return it->second;
		}

		RE::UI* UI() { return RE::UI::GetSingleton(); }

		bool MenuOpen(std::string_view a_name)
		{
			const auto ui = UI();
			return ui && ui->IsMenuOpen(a_name);
		}

		// A game menu that shows items in the 3D view owns it
		bool GameMenuOwns3D()
		{
			return MenuOpen(RE::InventoryMenu::MENU_NAME) || MenuOpen(RE::ContainerMenu::MENU_NAME) ||
			       MenuOpen(RE::BarterMenu::MENU_NAME) || MenuOpen(RE::GiftMenu::MENU_NAME) || MenuOpen(RE::MagicMenu::MENU_NAME) ||
			       MenuOpen(RE::CraftingMenu::MENU_NAME);
		}

		bool CanUseOwnScene()
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			return player && player->Is3DLoaded() && !GameMenuOwns3D() && !MenuOpen(RE::LoadingMenu::MENU_NAME) &&
			       !MenuOpen(RE::MainMenu::MENU_NAME) && !MenuOpen(RE::RaceSexMenu::MENU_NAME);
		}

		// The loaded model of a form in the 3D view, once it has finished loading
		RE::NiAVObject* LoadedModel(RE::Inventory3DManager* a_inv, RE::FormID a_form)
		{
			auto& runtime = a_inv->GetRuntimeData();
			for (const auto& loaded : runtime.loadedModels) {
				const bool ours = (loaded.itemBase && loaded.itemBase->GetFormID() == a_form) ||
				                  (loaded.modelObj && loaded.modelObj->GetFormID() == a_form);
				if (ours && loaded.spModel && loaded.spModel->worldBound.radius > 0.0f) {
					return loaded.spModel.get();
				}
			}
			return nullptr;
		}

		// ---- pixels ------------------------------------------------------------------------------

		float Half(std::uint16_t a_half)
		{
			const std::uint32_t sign = (a_half & 0x8000u) << 16;
			const std::uint32_t exponent = (a_half >> 10) & 0x1F;
			const std::uint32_t mantissa = a_half & 0x3FF;
			std::uint32_t       bits;
			if (exponent == 0) {
				return (sign ? -1.0f : 1.0f) * std::ldexp(static_cast<float>(mantissa), -24);
			} else if (exponent == 31) {
				bits = sign | 0x7F800000u | (mantissa << 13);
			} else {
				bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
			}
			return std::bit_cast<float>(bits);
		}

		// Reads RGB (0..1) from a mapped render target row, nullopt for formats we can't read
		struct Reader
		{
			W::DXGI_FORMAT format;

			[[nodiscard]] bool Supported() const
			{
				switch (format) {
				case W::DXGI_FORMAT_R8G8B8A8_TYPELESS:
				case W::DXGI_FORMAT_R8G8B8A8_UNORM:
				case W::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
				case W::DXGI_FORMAT_B8G8R8A8_TYPELESS:
				case W::DXGI_FORMAT_B8G8R8A8_UNORM:
				case W::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
				case W::DXGI_FORMAT_B8G8R8X8_UNORM:
				case W::DXGI_FORMAT_R10G10B10A2_TYPELESS:
				case W::DXGI_FORMAT_R10G10B10A2_UNORM:
				case W::DXGI_FORMAT_R16G16B16A16_FLOAT:
					return true;
				default:
					return false;
				}
			}

			void Read(const std::uint8_t* a_row, int a_x, float a_rgb[3]) const
			{
				switch (format) {
				case W::DXGI_FORMAT_R8G8B8A8_TYPELESS:
				case W::DXGI_FORMAT_R8G8B8A8_UNORM:
				case W::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
					{
						const auto p = a_row + a_x * 4;
						a_rgb[0] = p[0] / 255.0f;
						a_rgb[1] = p[1] / 255.0f;
						a_rgb[2] = p[2] / 255.0f;
						return;
					}
				case W::DXGI_FORMAT_B8G8R8A8_TYPELESS:
				case W::DXGI_FORMAT_B8G8R8A8_UNORM:
				case W::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
				case W::DXGI_FORMAT_B8G8R8X8_UNORM:
					{
						const auto p = a_row + a_x * 4;
						a_rgb[0] = p[2] / 255.0f;
						a_rgb[1] = p[1] / 255.0f;
						a_rgb[2] = p[0] / 255.0f;
						return;
					}
				case W::DXGI_FORMAT_R10G10B10A2_TYPELESS:
				case W::DXGI_FORMAT_R10G10B10A2_UNORM:
					{
						std::uint32_t v;
						std::memcpy(&v, a_row + a_x * 4, 4);
						a_rgb[0] = (v & 0x3FF) / 1023.0f;
						a_rgb[1] = ((v >> 10) & 0x3FF) / 1023.0f;
						a_rgb[2] = ((v >> 20) & 0x3FF) / 1023.0f;
						return;
					}
				case W::DXGI_FORMAT_R16G16B16A16_FLOAT:
					{
						std::uint16_t h[3];
						std::memcpy(h, a_row + a_x * 8, 6);
						for (int i = 0; i < 3; ++i) {
							a_rgb[i] = std::clamp(Half(h[i]), 0.0f, 1.0f);
						}
						return;
					}
				default:
					a_rgb[0] = a_rgb[1] = a_rgb[2] = 0.0f;
					return;
				}
			}
		};

		struct Mapped
		{
			const std::uint8_t* data{ nullptr };
			std::uint32_t       pitch{ 0 };

			[[nodiscard]] const std::uint8_t* Row(int a_y) const { return data + static_cast<std::size_t>(a_y) * pitch; }
		};

		// Uncompressed 32 bit DDS (B8G8R8A8), the format every DDS loader reads
		bool WriteDDS(const std::string& a_path, const std::vector<std::uint8_t>& a_bgra, int a_size)
		{
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(a_path).parent_path(), ec);
			std::ofstream file(a_path, std::ios::binary | std::ios::trunc);
			if (!file) {
				return false;
			}
			std::uint32_t header[32]{};
			header[0] = 0x20534444;  // "DDS "
			header[1] = 124;
			header[2] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x8;  // caps, height, width, pixel format, pitch
			header[3] = a_size;
			header[4] = a_size;
			header[5] = a_size * 4;
			header[19] = 32;            // pixel format size
			header[20] = 0x1 | 0x40;    // alpha pixels | rgb
			header[22] = 32;
			header[23] = 0x00FF0000;    // r
			header[24] = 0x0000FF00;    // g
			header[25] = 0x000000FF;    // b
			header[26] = 0xFF000000;    // a
			header[27] = 0x1000;        // texture
			file.write(reinterpret_cast<const char*>(header), sizeof(header));
			file.write(reinterpret_cast<const char*>(a_bgra.data()), static_cast<std::streamsize>(a_bgra.size()));
			return static_cast<bool>(file);
		}

		enum class CaptureResult
		{
			kSaved,
			kEmpty,   // nothing of the model in the picture (yet)
			kFailed   // can't capture at all here
		};

		// Black pass: model over black = colour * alpha. White pass: model over white = colour * alpha + (1 - alpha).
		// So alpha = 1 - (white - black) and colour = black / alpha.
		CaptureResult Process(const Mapped& a_black, const Mapped& a_white, const Reader& a_reader, int a_width, int a_height,
			const std::string& a_path)
		{
			const auto alphaAt = [&](int x, int y, float* a_black3 = nullptr) {
				float b[3], w[3];
				a_reader.Read(a_black.Row(y), x, b);
				a_reader.Read(a_white.Row(y), x, w);
				if (a_black3) {
					std::copy_n(b, 3, a_black3);
				}
				const float diff = ((w[0] - b[0]) + (w[1] - b[1]) + (w[2] - b[2])) / 3.0f;
				return std::clamp(1.0f - diff, 0.0f, 1.0f);
			};

			int minX = a_width, minY = a_height, maxX = -1, maxY = -1;
			for (int y = 0; y < a_height; y += 2) {
				for (int x = 0; x < a_width; x += 2) {
					if (alphaAt(x, y) > kAlphaCut) {
						minX = std::min(minX, x);
						maxX = std::max(maxX, x);
						minY = std::min(minY, y);
						maxY = std::max(maxY, y);
					}
				}
			}
			if (maxX < 0) {
				return CaptureResult::kEmpty;
			}
			minX = std::max(0, minX - 2);
			minY = std::max(0, minY - 2);
			maxX = std::min(a_width - 1, maxX + 2);
			maxY = std::min(a_height - 1, maxY + 2);

			// square crop around the model, centered
			const float side = std::max(maxX - minX + 1, maxY - minY + 1) * kMargin;
			const float originX = (minX + maxX + 1) * 0.5f - side * 0.5f;
			const float originY = (minY + maxY + 1) * 0.5f - side * 0.5f;
			const float step = side / kIconSize;
			const int   samples = std::clamp(static_cast<int>(std::ceil(step)), 1, 8);  // per axis, area average

			std::vector<std::uint8_t> bgra(static_cast<std::size_t>(kIconSize) * kIconSize * 4, 0);
			for (int y = 0; y < kIconSize; ++y) {
				for (int x = 0; x < kIconSize; ++x) {
					float sum[3]{}, alpha = 0.0f;
					int   count = 0;
					for (int sy = 0; sy < samples; ++sy) {
						for (int sx = 0; sx < samples; ++sx) {
							const int px = static_cast<int>(originX + (x + (sx + 0.5f) / samples) * step);
							const int py = static_cast<int>(originY + (y + (sy + 0.5f) / samples) * step);
							++count;
							if (px < 0 || py < 0 || px >= a_width || py >= a_height) {
								continue;
							}
							float black[3];
							alpha += alphaAt(px, py, black);
							for (int i = 0; i < 3; ++i) {
								sum[i] += black[i];  // premultiplied colour
							}
						}
					}
					alpha /= count;
					auto* out = &bgra[(static_cast<std::size_t>(y) * kIconSize + x) * 4];
					if (alpha <= 0.002f) {
						continue;
					}
					const auto channel = [&](float a_premultiplied) {
						return static_cast<std::uint8_t>(std::clamp(a_premultiplied / count / alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
					};
					out[0] = channel(sum[2]);
					out[1] = channel(sum[1]);
					out[2] = channel(sum[0]);
					out[3] = static_cast<std::uint8_t>(alpha * 255.0f + 0.5f);
				}
			}
			return WriteDDS(a_path, bgra, kIconSize) ? CaptureResult::kSaved : CaptureResult::kFailed;
		}

		template <class T>
		struct Ref
		{
			T* p{ nullptr };
			~Ref()
			{
				if (p) {
					p->Release();
				}
			}
		};

		// Draws the 3D view twice into the bound render target (over black, over white), reads both back and puts the
		// frame back the way it was. Runs inside the UI render pass.
		CaptureResult Capture(RE::Inventory3DManager* a_inv, const std::string& a_path)
		{
			const auto renderer = RE::BSGraphics::Renderer::GetRendererDataSingleton();
			const auto context = renderer ? renderer->context : nullptr;
			const auto device = renderer ? renderer->forwarder : nullptr;
			if (!context || !device) {
				return CaptureResult::kFailed;
			}

			// whatever is bound now is the surface being drawn (with upscalers it's not always the swap chain)
			Ref<W::ID3D11RenderTargetView> rtv;
			context->OMGetRenderTargets(1, &rtv.p, nullptr);
			if (!rtv.p) {
				return CaptureResult::kFailed;
			}
			Ref<W::ID3D11Resource> resource;
			rtv.p->GetResource(&resource.p);
			Ref<W::ID3D11Texture2D> target;
			if (!resource.p || resource.p->QueryInterface(W::IID_ID3D11Texture2D, reinterpret_cast<void**>(&target.p)) < 0 || !target.p) {
				return CaptureResult::kFailed;
			}

			W::D3D11_TEXTURE2D_DESC desc{};
			target.p->GetDesc(&desc);
			const Reader reader{ desc.format };
			if (desc.sampleDesc.count != 1 || !reader.Supported()) {
				logs::warn("Item icons: render target format {} / {} samples can't be read", static_cast<int>(desc.format), desc.sampleDesc.count);
				return CaptureResult::kFailed;
			}

			static bool logged = false;
			if (!logged) {
				logged = true;
				logs::info("Item icons: capturing from a {}x{} render target, format {}", desc.width, desc.height, static_cast<int>(desc.format));
			}

			W::D3D11_TEXTURE2D_DESC copyDesc = desc;  // same size / format / mips: CopyResource needs identical layouts
			copyDesc.bindFlags = 0;
			copyDesc.miscFlags = 0;
			copyDesc.usage = W::D3D11_USAGE_DEFAULT;
			copyDesc.cpuAccessFlags = 0;
			W::D3D11_TEXTURE2D_DESC stagingDesc = copyDesc;
			stagingDesc.usage = W::D3D11_USAGE_STAGING;
			stagingDesc.cpuAccessFlags = W::D3D11_CPU_ACCESS_READ;

			Ref<W::ID3D11Texture2D> saved, black, white;
			if (device->CreateTexture2D(&copyDesc, nullptr, &saved.p) < 0 || device->CreateTexture2D(&stagingDesc, nullptr, &black.p) < 0 ||
				device->CreateTexture2D(&stagingDesc, nullptr, &white.p) < 0) {
				return CaptureResult::kFailed;
			}

			constexpr float kBlack[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
			constexpr float kWhite[4]{ 1.0f, 1.0f, 1.0f, 0.0f };
			context->CopyResource(saved.p, target.p);
			context->ClearRenderTargetView(rtv.p, kBlack);
			a_inv->Render();
			context->CopyResource(black.p, target.p);
			context->ClearRenderTargetView(rtv.p, kWhite);
			a_inv->Render();
			context->CopyResource(white.p, target.p);
			context->CopyResource(target.p, saved.p);  // nothing of this ever reaches the screen

			W::D3D11_MAPPED_SUBRESOURCE mappedBlack{}, mappedWhite{};
			if (context->Map(black.p, 0, W::D3D11_MAP_READ, 0, &mappedBlack) < 0) {
				return CaptureResult::kFailed;
			}
			if (context->Map(white.p, 0, W::D3D11_MAP_READ, 0, &mappedWhite) < 0) {
				context->Unmap(black.p, 0);
				return CaptureResult::kFailed;
			}
			const auto result = Process({ static_cast<const std::uint8_t*>(mappedBlack.data), mappedBlack.rowPitch },
				{ static_cast<const std::uint8_t*>(mappedWhite.data), mappedWhite.rowPitch }, reader, static_cast<int>(desc.width),
				static_cast<int>(desc.height), a_path);
			context->Unmap(black.p, 0);
			context->Unmap(white.p, 0);
			return result;
		}

		void EndOwnScene(RE::Inventory3DManager* a_inv)
		{
			a_inv->UnloadInventoryItem();
			a_inv->End3D();
		}

		void Finish(State a_state)
		{
			if (!job) {
				return;
			}
			auto& entry = byModel[job->key];
			entry.state = a_state;
			if (a_state == State::kFailed) {
				logs::warn("Item icon for {} could not be made", job->key);
			} else {
				logs::info("Item icon made for {}", job->key);
			}
			job.reset();
		}

		// Runs in the capture menu's PostDisplay: the UI render pass, where the game draws the inventory's 3D item
		void OnPostDisplay()
		{
			std::scoped_lock guard(lock);
			const auto inv = RE::Inventory3DManager::GetSingleton();
			if (!inv) {
				return;
			}
			const bool gameMenu = GameMenuOwns3D();

			if (!job) {
				if (queue.empty()) {
					return;
				}
				if (gameMenu) {
					// a game menu owns the 3D view: photograph what it is showing when it's one of ours
					for (auto it = queue.begin(); it != queue.end(); ++it) {
						if (LoadedModel(inv, it->second)) {
							job = Job{ it->first, it->second, false };
							queue.erase(it);
							break;
						}
					}
					return;
				}
				if (!CanUseOwnScene()) {
					return;
				}
				const auto [key, formID] = queue.front();
				queue.pop_front();
				const auto object = RE::TESForm::LookupByID<RE::TESBoundObject>(formID);
				if (!object) {
					byModel[key].state = State::kFailed;
					return;
				}
				job = Job{ key, formID, true };
				inv->Begin3D(RE::INTERFACE_LIGHT_SCHEME::kInventory);
				inv->LoadInventoryItem(object, nullptr);
				return;
			}

			++job->frames;
			if (job->ownScene && gameMenu) {
				// a menu with a 3D item view opened meanwhile: it owns the view now, try again later
				queue.emplace_back(job->key, job->form);
				byModel[job->key].state = State::kQueued;
				job.reset();
				return;
			}
			if (!job->ownScene && !gameMenu) {
				queue.emplace_back(job->key, job->form);  // the menu closed before the picture
				job.reset();
				return;
			}

			const auto model = LoadedModel(inv, job->form);
			if (!model) {
				if (job->frames > kLoadTimeout) {
					if (job->ownScene) {
						EndOwnScene(inv);
					}
					Finish(State::kFailed);
				} else if (!job->ownScene) {
					queue.emplace_back(job->key, job->form);  // the menu shows another item now
					job.reset();
				}
				return;
			}
			if (++job->settled < kSettleFrames) {
				if (job->ownScene) {
					model->SetAppCulled(true);  // ours: never visible in the game's own frames
				}
				return;
			}

			auto& entry = byModel[job->key];
			if (entry.path.empty()) {
				entry.path = FileFor(job->key);
			}
			model->SetAppCulled(false);
			const auto result = Capture(inv, entry.path);
			if (job->ownScene) {
				model->SetAppCulled(true);
			}
			switch (result) {
			case CaptureResult::kSaved:
				if (job->ownScene) {
					EndOwnScene(inv);
				}
				Finish(State::kReady);
				break;
			case CaptureResult::kEmpty:
				if (++job->empty >= kEmptyRetries) {
					if (job->ownScene) {
						EndOwnScene(inv);
					}
					Finish(State::kFailed);
				}
				break;
			default:
				if (job->ownScene) {
					EndOwnScene(inv);
				}
				Finish(State::kFailed);
				break;
			}
		}

		// Invisible menu, only open while icons are waiting: its PostDisplay is called in the UI render pass
		class CaptureMenu : public RE::IMenu
		{
		public:
			CaptureMenu()
			{
				menuFlags.set(RE::UI_MENU_FLAGS::kCustomRendering, RE::UI_MENU_FLAGS::kAllowSaving);
				depthPriority = 0;
			}

			void PostDisplay() override { OnPostDisplay(); }

			static RE::IMenu* Create() { return new CaptureMenu(); }
		};

		void SendMenuMessage(RE::UI_MESSAGE_TYPE a_type)
		{
			SKSE::GetTaskInterface()->AddUITask([a_type] {
				if (const auto messages = RE::UIMessageQueue::GetSingleton()) {
					messages->AddMessage(kMenuName, a_type, nullptr);
				}
			});
		}
	}

	void Register()
	{
		if (const auto ui = UI()) {
			ui->Register(kMenuName, CaptureMenu::Create);
			logs::info("Item icons: capture menu registered, cache in {}", Dir().string());
		}
	}

	bool Supports(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return false;
		}
		switch (a_form->GetFormType()) {
		case RE::FormType::Weapon:
		case RE::FormType::Armor:
		case RE::FormType::Ammo:
		case RE::FormType::Light:
			return true;
		default:
			return false;
		}
	}

	void* Get(RE::TESForm* a_form)
	{
		if (!Supports(a_form)) {
			return nullptr;
		}
		std::scoped_lock guard(lock);
		const auto& key = KeyOf(a_form);
		if (key.empty()) {
			return nullptr;
		}
		auto& entry = byModel[key];
		if (entry.texture) {
			return entry.texture;
		}
		switch (entry.state) {
		case State::kQueued:
		case State::kFailed:
			return nullptr;
		default:
			break;
		}
		if (entry.path.empty()) {
			entry.path = FileFor(key);
		}
		std::error_code ec;
		if (entry.state == State::kReady || std::filesystem::exists(entry.path, ec)) {
			entry.texture = SKSEMenuFramework::LoadTexture(entry.path);
			entry.state = entry.texture ? State::kReady : State::kFailed;
			if (!entry.texture) {
				logs::warn("Item icons: could not load {}", entry.path);
			}
			return entry.texture;
		}
		entry.state = State::kQueued;
		queue.emplace_back(key, a_form->GetFormID());
		return nullptr;
	}

	void Update()
	{
		std::scoped_lock guard(lock);
		const bool wanted = !queue.empty() || job.has_value();
		const bool open = MenuOpen(kMenuName);
		if (open == wanted || MenuOpen(RE::LoadingMenu::MENU_NAME) || MenuOpen(RE::MainMenu::MENU_NAME)) {
			return;
		}
		// one message, then wait a moment for it to arrive before asking again
		const auto now = std::chrono::steady_clock::now();
		if (now - menuRequestTime > 500ms) {
			menuRequestTime = now;
			SendMenuMessage(wanted ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide);
		}
	}
}
