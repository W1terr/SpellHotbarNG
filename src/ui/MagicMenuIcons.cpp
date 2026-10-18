#include "ui/MagicMenuIcons.h"

#include "core/Config.h"
#include "ui/Icons.h"

namespace MagicMenuIcons
{
	namespace
	{
		constexpr std::uint32_t kSize = 64;      // pixels of the picture in its movie
		constexpr std::uint32_t kStage = 128;    // movie size in pixels, the same as SkyUI's icon movies
		constexpr int           kVersion = 1;    // part of the file names: new files when the movie changes
		constexpr auto          kRetry = 250ms;  // how often an open Magic menu is checked for the hook

		// SkyUI's icon movie, for entries going back from one of our pictures when I4 isn't installed
		constexpr const char* kSkyUIIcons = "skyui/icons_item_psychosteve.swf";

		std::mutex lock;  // the caches below: filled on the UI thread, cleared when the menu closes

		// ---- atlas pixels ------------------------------------------------------------------------

		enum class Format
		{
			kNone,
			kBC1,
			kBC2,
			kBC3,
			kRGBA,
			kBGRA
		};

		struct Atlas
		{
			std::vector<std::uint8_t> data;
			std::size_t               offset{ 0 };  // first byte of the top mip level
			std::uint32_t             width{ 0 };
			std::uint32_t             height{ 0 };
			Format                    format{ Format::kNone };
			bool                      alpha{ true };  // uncompressed: has an alpha channel
		};

		std::unordered_map<std::string, Atlas>       atlases;   // read once per Magic menu
		std::unordered_map<std::string, std::string> pictures;  // picture key -> movie path ("" = can't be made)

		std::uint32_t U32(const std::vector<std::uint8_t>& a_data, std::size_t a_offset)
		{
			std::uint32_t value;
			std::memcpy(&value, a_data.data() + a_offset, sizeof(value));
			return value;
		}

		constexpr std::uint32_t FourCC(const char (&a_code)[5])
		{
			return static_cast<std::uint32_t>(a_code[0]) | static_cast<std::uint32_t>(a_code[1]) << 8 |
			       static_cast<std::uint32_t>(a_code[2]) << 16 | static_cast<std::uint32_t>(a_code[3]) << 24;
		}

		bool Compressed(Format a_format)
		{
			return a_format == Format::kBC1 || a_format == Format::kBC2 || a_format == Format::kBC3;
		}

		// DDS atlases in BC1/BC2/BC3 or 32 bit RGBA/BGRA (ours, SpellHotbar2 icon packs). PNG and BC7 aren't read.
		Atlas ReadAtlas(const std::string& a_path)
		{
			Atlas         atlas;
			std::ifstream file(a_path, std::ios::binary);
			if (!file) {
				return atlas;
			}
			atlas.data.assign(std::istreambuf_iterator<char>(file), {});
			const auto& d = atlas.data;
			if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) {
				return atlas;
			}
			atlas.height = U32(d, 12);
			atlas.width = U32(d, 16);
			atlas.offset = 128;
			const auto flags = U32(d, 80);
			const auto fourCC = U32(d, 84);
			if (flags & 0x4) {  // four CC
				if (fourCC == FourCC("DXT1")) {
					atlas.format = Format::kBC1;
				} else if (fourCC == FourCC("DXT2") || fourCC == FourCC("DXT3")) {
					atlas.format = Format::kBC2;
				} else if (fourCC == FourCC("DXT4") || fourCC == FourCC("DXT5")) {
					atlas.format = Format::kBC3;
				} else if (fourCC == FourCC("DX10") && d.size() >= 148) {
					atlas.offset = 148;
					switch (U32(d, 128)) {  // DXGI format
					case 70:
					case 71:
					case 72:
						atlas.format = Format::kBC1;
						break;
					case 73:
					case 74:
					case 75:
						atlas.format = Format::kBC2;
						break;
					case 76:
					case 77:
					case 78:
						atlas.format = Format::kBC3;
						break;
					case 27:
					case 28:
					case 29:
						atlas.format = Format::kRGBA;
						break;
					case 87:
					case 90:
					case 91:
						atlas.format = Format::kBGRA;
						break;
					default:
						break;
					}
				}
			} else if ((flags & 0x40) && U32(d, 88) == 32) {  // uncompressed 32 bit
				atlas.format = U32(d, 92) == 0x00FF0000 ? Format::kBGRA : Format::kRGBA;
				atlas.alpha = (flags & 0x1) && U32(d, 104) != 0;
			}

			const std::size_t blocks = static_cast<std::size_t>((atlas.width + 3) / 4) * ((atlas.height + 3) / 4);
			const std::size_t size = atlas.format == Format::kBC1 ? blocks * 8 :
			                         Compressed(atlas.format)     ? blocks * 16 :
			                                                        static_cast<std::size_t>(atlas.width) * atlas.height * 4;
			if (atlas.width == 0 || atlas.height == 0 || d.size() < atlas.offset + size) {
				atlas.format = Format::kNone;
			}
			return atlas;
		}

		using Texel = std::array<std::uint8_t, 4>;  // RGBA

		// One 4x4 block: 565 color end points + 2 bit indices (BC1 with 1 bit alpha when c0 <= c1)
		void DecodeColor(const std::uint8_t* a_block, bool a_bc1, std::array<Texel, 16>& a_out)
		{
			const auto c0 = static_cast<std::uint16_t>(a_block[0] | a_block[1] << 8);
			const auto c1 = static_cast<std::uint16_t>(a_block[2] | a_block[3] << 8);
			const auto expand = [](std::uint16_t c) {
				const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
				return std::array<int, 3>{ (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2) };
			};
			const auto e0 = expand(c0);
			const auto e1 = expand(c1);
			std::array<Texel, 4> palette{};
			for (int i = 0; i < 3; ++i) {
				palette[0][i] = static_cast<std::uint8_t>(e0[i]);
				palette[1][i] = static_cast<std::uint8_t>(e1[i]);
				if (c0 > c1 || !a_bc1) {
					palette[2][i] = static_cast<std::uint8_t>((2 * e0[i] + e1[i]) / 3);
					palette[3][i] = static_cast<std::uint8_t>((e0[i] + 2 * e1[i]) / 3);
				} else {
					palette[2][i] = static_cast<std::uint8_t>((e0[i] + e1[i]) / 2);
					palette[3][i] = 0;
				}
			}
			palette[0][3] = palette[1][3] = palette[2][3] = 255;
			palette[3][3] = (c0 > c1 || !a_bc1) ? 255 : 0;

			const auto indices = static_cast<std::uint32_t>(a_block[4] | a_block[5] << 8 | a_block[6] << 16 | a_block[7] << 24);
			for (int i = 0; i < 16; ++i) {
				a_out[i] = palette[(indices >> (2 * i)) & 3];
			}
		}

		void DecodeBlock(const std::uint8_t* a_block, Format a_format, std::array<Texel, 16>& a_out)
		{
			if (a_format == Format::kBC1) {
				DecodeColor(a_block, true, a_out);
				return;
			}
			DecodeColor(a_block + 8, false, a_out);
			if (a_format == Format::kBC2) {  // 4 bit alpha per texel
				for (int i = 0; i < 16; ++i) {
					const auto nibble = (a_block[i / 2] >> (4 * (i % 2))) & 15;
					a_out[i][3] = static_cast<std::uint8_t>(nibble * 17);
				}
				return;
			}
			// BC3: two alpha end points + 3 bit indices
			const int    a0 = a_block[0];
			const int    a1 = a_block[1];
			std::array<int, 8> alphas{ a0, a1 };
			if (a0 > a1) {
				for (int i = 1; i < 7; ++i) {
					alphas[i + 1] = ((7 - i) * a0 + i * a1) / 7;
				}
			} else {
				for (int i = 1; i < 5; ++i) {
					alphas[i + 1] = ((5 - i) * a0 + i * a1) / 5;
				}
				alphas[6] = 0;
				alphas[7] = 255;
			}
			std::uint64_t bits = 0;
			for (int i = 0; i < 6; ++i) {
				bits |= static_cast<std::uint64_t>(a_block[2 + i]) << (8 * i);
			}
			for (int i = 0; i < 16; ++i) {
				a_out[i][3] = static_cast<std::uint8_t>(alphas[(bits >> (3 * i)) & 7]);
			}
		}

		// RGBA pixels of a rectangle of the atlas, row by row
		std::vector<Texel> Region(const Atlas& a_atlas, std::uint32_t a_x, std::uint32_t a_y, std::uint32_t a_w, std::uint32_t a_h)
		{
			std::vector<Texel> out(static_cast<std::size_t>(a_w) * a_h);
			const auto*        data = a_atlas.data.data() + a_atlas.offset;
			if (Compressed(a_atlas.format)) {
				const std::size_t        blockSize = a_atlas.format == Format::kBC1 ? 8 : 16;
				const std::uint32_t      blocksWide = (a_atlas.width + 3) / 4;
				std::array<Texel, 16>    block{};
				for (std::uint32_t by = a_y / 4; by <= (a_y + a_h - 1) / 4; ++by) {
					for (std::uint32_t bx = a_x / 4; bx <= (a_x + a_w - 1) / 4; ++bx) {
						DecodeBlock(data + (static_cast<std::size_t>(by) * blocksWide + bx) * blockSize, a_atlas.format, block);
						for (std::uint32_t i = 0; i < 16; ++i) {
							const auto x = bx * 4 + i % 4;
							const auto y = by * 4 + i / 4;
							if (x >= a_x && x < a_x + a_w && y >= a_y && y < a_y + a_h) {
								out[static_cast<std::size_t>(y - a_y) * a_w + (x - a_x)] = block[i];
							}
						}
					}
				}
				return out;
			}
			for (std::uint32_t y = 0; y < a_h; ++y) {
				for (std::uint32_t x = 0; x < a_w; ++x) {
					const auto* p = data + (static_cast<std::size_t>(a_y + y) * a_atlas.width + a_x + x) * 4;
					auto&       t = out[static_cast<std::size_t>(y) * a_w + x];
					t = a_atlas.format == Format::kBGRA ? Texel{ p[2], p[1], p[0], p[3] } : Texel{ p[0], p[1], p[2], p[3] };
					if (!a_atlas.alpha) {
						t[3] = 255;
					}
				}
			}
			return out;
		}

		// kSize x kSize premultiplied ARGB (the byte order of SWF lossless bitmaps), area averaged
		std::vector<std::uint8_t> Scale(const std::vector<Texel>& a_pixels, std::uint32_t a_w, std::uint32_t a_h)
		{
			std::vector<std::uint8_t> out(static_cast<std::size_t>(kSize) * kSize * 4);
			const double              sx = static_cast<double>(a_w) / kSize;
			const double              sy = static_cast<double>(a_h) / kSize;
			for (std::uint32_t oy = 0; oy < kSize; ++oy) {
				const double y0 = oy * sy, y1 = y0 + sy;
				for (std::uint32_t ox = 0; ox < kSize; ++ox) {
					const double x0 = ox * sx, x1 = x0 + sx;
					double       sum[4]{};
					double       total = 0.0;
					for (auto y = static_cast<std::uint32_t>(y0); y < a_h && y < y1; ++y) {
						const double wy = std::min<double>(y + 1, y1) - std::max<double>(y, y0);
						for (auto x = static_cast<std::uint32_t>(x0); x < a_w && x < x1; ++x) {
							const double wx = std::min<double>(x + 1, x1) - std::max<double>(x, x0);
							const double w = wx * wy;
							const auto&  t = a_pixels[static_cast<std::size_t>(y) * a_w + x];
							const double a = t[3] / 255.0;
							sum[0] += t[0] * a * w;
							sum[1] += t[1] * a * w;
							sum[2] += t[2] * a * w;
							sum[3] += t[3] * w;
							total += w;
						}
					}
					auto* p = out.data() + (static_cast<std::size_t>(oy) * kSize + ox) * 4;
					if (total > 0.0) {
						p[0] = static_cast<std::uint8_t>(std::clamp(sum[3] / total + 0.5, 0.0, 255.0));
						for (int c = 0; c < 3; ++c) {
							p[c + 1] = static_cast<std::uint8_t>(std::clamp(sum[c] / total + 0.5, 0.0, 255.0));
						}
					}
				}
			}
			return out;
		}

		// ---- the movie -----------------------------------------------------------------------------

		class Writer
		{
		public:
			std::vector<std::uint8_t> bytes;

			void U8(std::uint32_t a_value) { Flush(), bytes.push_back(static_cast<std::uint8_t>(a_value)); }
			void U16(std::uint32_t a_value) { U8(a_value & 0xFF), U8((a_value >> 8) & 0xFF); }
			void U32(std::uint32_t a_value) { U16(a_value & 0xFFFF), U16(a_value >> 16); }
			void Bytes(const std::vector<std::uint8_t>& a_bytes) { Flush(), bytes.insert(bytes.end(), a_bytes.begin(), a_bytes.end()); }

			// most significant bit first; signed values in two's complement
			void Bits(std::int64_t a_value, int a_count)
			{
				for (int i = a_count - 1; i >= 0; --i) {
					acc = static_cast<std::uint8_t>(acc << 1 | ((static_cast<std::uint64_t>(a_value) >> i) & 1));
					if (++used == 8) {
						bytes.push_back(acc);
						acc = 0;
						used = 0;
					}
				}
			}

			void Flush()
			{
				if (used > 0) {
					bytes.push_back(static_cast<std::uint8_t>(acc << (8 - used)));
					acc = 0;
					used = 0;
				}
			}

			void Rect(std::int32_t a_size)
			{
				constexpr int bits = 15;
				Bits(bits, 5);
				Bits(0, bits);
				Bits(a_size, bits);
				Bits(0, bits);
				Bits(a_size, bits);
				Flush();
			}

			void Tag(std::uint32_t a_code, const Writer& a_body)
			{
				U16(a_code << 6 | 0x3F);  // long form: fine for every tag
				U32(static_cast<std::uint32_t>(a_body.bytes.size()));
				Bytes(a_body.bytes);
			}

		private:
			std::uint8_t acc{ 0 };
			int          used{ 0 };
		};

		// zlib stream with stored (uncompressed) deflate blocks
		std::vector<std::uint8_t> Zlib(const std::vector<std::uint8_t>& a_data)
		{
			std::vector<std::uint8_t> out{ 0x78, 0x01 };
			std::size_t               pos = 0;
			do {
				const auto len = static_cast<std::uint16_t>(std::min<std::size_t>(a_data.size() - pos, 0xFFFF));
				const bool last = pos + len >= a_data.size();
				out.push_back(last ? 1 : 0);
				out.push_back(static_cast<std::uint8_t>(len & 0xFF));
				out.push_back(static_cast<std::uint8_t>(len >> 8));
				out.push_back(static_cast<std::uint8_t>(~len & 0xFF));
				out.push_back(static_cast<std::uint8_t>((~len >> 8) & 0xFF));
				out.insert(out.end(), a_data.begin() + pos, a_data.begin() + pos + len);
				pos += len;
			} while (pos < a_data.size());
			std::uint32_t a = 1, b = 0;
			for (const auto byte : a_data) {
				a = (a + byte) % 65521;
				b = (b + a) % 65521;
			}
			const auto adler = b << 16 | a;
			for (int shift = 24; shift >= 0; shift -= 8) {
				out.push_back(static_cast<std::uint8_t>(adler >> shift));
			}
			return out;
		}

		// A one frame AS2 movie of kStage x kStage pixels filled with the picture (bitmap 1, shape 2)
		std::vector<std::uint8_t> Movie(const std::vector<std::uint8_t>& a_argb)
		{
			constexpr std::int32_t stage = kStage * 20;  // twips

			Writer body;
			body.Rect(stage);
			body.U16(24 << 8);  // frame rate
			body.U16(1);        // frame count

			Writer attributes;
			attributes.U32(0);
			body.Tag(69, attributes);  // FileAttributes

			Writer bitmap;
			bitmap.U16(1);
			bitmap.U8(5);  // 32 bit ARGB
			bitmap.U16(kSize);
			bitmap.U16(kSize);
			bitmap.Bytes(Zlib(a_argb));
			body.Tag(36, bitmap);  // DefineBitsLossless2

			Writer shape;
			shape.U16(2);
			shape.Rect(stage);
			shape.U8(1);     // one fill style
			shape.U8(0x41);  // clipped bitmap, smoothed
			shape.U16(1);
			shape.Bits(1, 1);  // matrix: scale bitmap pixels to the stage
			constexpr std::int64_t scale = static_cast<std::int64_t>(stage / kSize) << 16;
			shape.Bits(25, 5);
			shape.Bits(scale, 25);
			shape.Bits(scale, 25);
			shape.Bits(0, 1);  // no rotation
			shape.Bits(0, 5);  // no translation
			shape.Flush();
			shape.U8(0);  // no line styles
			shape.Bits(1, 4);  // fill style index bits
			shape.Bits(0, 4);  // line style index bits
			// move to 0,0 with fill style 1 on the right of the edges
			shape.Bits(0b000101, 6);
			shape.Bits(1, 5);
			shape.Bits(0, 1);
			shape.Bits(0, 1);
			shape.Bits(1, 1);
			// clockwise square: right, down, left, up
			constexpr int edgeBits = 15;
			const auto    edge = [&](bool a_vertical, std::int32_t a_delta) {
                shape.Bits(0b11, 2);
                shape.Bits(edgeBits - 2, 4);
                shape.Bits(0, 1);  // not a general line
                shape.Bits(a_vertical ? 1 : 0, 1);
                shape.Bits(a_delta, edgeBits);
			};
			edge(false, stage);
			edge(true, stage);
			edge(false, -stage);
			edge(true, -stage);
			shape.Bits(0, 6);  // end of shape
			shape.Flush();
			body.Tag(2, shape);  // DefineShape

			Writer place;
			place.U8(0x02);  // has character
			place.U16(1);    // depth
			place.U16(2);
			body.Tag(26, place);  // PlaceObject2

			body.Tag(1, Writer{});  // ShowFrame
			body.Tag(0, Writer{});  // End

			Writer file;
			file.U8('F');
			file.U8('W');
			file.U8('S');
			file.U8(8);
			file.U32(static_cast<std::uint32_t>(8 + body.bytes.size()));
			file.Bytes(body.bytes);
			return file.bytes;
		}

		// ---- pictures --------------------------------------------------------------------------

		std::uint64_t Hash(std::string_view a_text)
		{
			std::uint64_t hash = 14695981039346656037ull;
			for (const auto c : a_text) {
				hash = (hash ^ static_cast<std::uint8_t>(c)) * 1099511628211ull;
			}
			return hash;
		}

		// The movie (path for loadClip, relative to Data\Interface) of an atlas picture; made on first use
		std::string MovieFor(const Icons::Icon& a_icon)
		{
			const auto& atlasPath = Icons::AtlasPath(a_icon.atlas);
			if (atlasPath.empty()) {
				return {};
			}
			const auto key = std::format("{}|{}|{}|{}|{}|{}", atlasPath, a_icon.u0, a_icon.v0, a_icon.u1, a_icon.v1, kVersion);

			std::scoped_lock guard(lock);
			if (const auto it = pictures.find(key); it != pictures.end()) {
				return it->second;
			}
			auto&      result = pictures[key];
			const auto name = std::format("{:016x}.swf", Hash(key));
			const auto file = std::filesystem::path("Data/Interface/SpellHotbarNG/magic_icons") / name;
			const auto movie = "SpellHotbarNG/magic_icons/" + name;

			std::error_code ec;
			if (std::filesystem::exists(file, ec)) {
				return result = movie;
			}

			auto& atlas = atlases[atlasPath];
			if (atlas.data.empty()) {
				atlas = ReadAtlas(atlasPath);
				if (atlas.format == Format::kNone) {
					logs::warn("Magic menu icons: can't read the pictures of {} (DDS in DXT1/3/5 or 32 bit color needed)", atlasPath);
				}
			}
			if (atlas.format == Format::kNone) {
				return result;
			}
			const auto px = [](float a_uv, std::uint32_t a_size) {
				return std::min(a_size, static_cast<std::uint32_t>(std::lround(std::clamp(a_uv, 0.0f, 1.0f) * a_size)));
			};
			const auto x0 = px(a_icon.u0, atlas.width), x1 = px(a_icon.u1, atlas.width);
			const auto y0 = px(a_icon.v0, atlas.height), y1 = px(a_icon.v1, atlas.height);
			if (x1 <= x0 || y1 <= y0) {
				return result;
			}

			const auto bytes = Movie(Scale(Region(atlas, x0, y0, x1 - x0, y1 - y0), x1 - x0, y1 - y0));
			std::filesystem::create_directories(file.parent_path(), ec);
			std::ofstream out(file, std::ios::binary);
			if (!out || !out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
				logs::warn("Magic menu icons: could not write {}", file.string());
				return result;
			}
			return result = movie;
		}

		// The movie for a list entry (SkyUI entry object with the form id), empty if it keeps SkyUI's icon
		std::string MovieForEntry(const RE::GFxValue& a_entry)
		{
			if (!a_entry.IsObject()) {
				return {};
			}
			RE::GFxValue formId;
			a_entry.GetMember("formId", &formId);
			if (!formId.IsNumber()) {
				return {};
			}
			const auto form = RE::TESForm::LookupByID(static_cast<RE::FormID>(formId.GetNumber()));
			if (!form || !(form->Is(RE::FormType::Spell) || form->Is(RE::FormType::Shout))) {
				return {};
			}
			std::scoped_lock guard(Config::Lock());
			if (!Config::Get().magicMenuIcons) {
				return {};
			}
			const auto icon = Icons::MenuPicture(form);
			return icon.atlas >= 0 ? MovieFor(icon) : std::string{};
		}

		// ---- the hook --------------------------------------------------------------------------

		void LoadClip(RE::GFxMovie* a_movie, RE::GFxValue& a_listener, RE::GFxValue& a_target, const RE::GFxValue& a_source)
		{
			RE::GFxValue loader;
			a_movie->CreateObject(&loader, "MovieClipLoader");
			if (!loader.IsObject()) {
				return;
			}
			loader.Invoke("addListener", nullptr, &a_listener, 1);
			const std::array<RE::GFxValue, 2> args{ a_source, a_target };
			loader.Invoke("loadClip", args);
		}

		// InventoryListEntry.formatItemIcon(a_entryField, a_entryObject, a_state): a_entryField is the itemIcon clip
		class FormatItemIcon : public RE::GFxFunctionHandler
		{
		public:
			explicit FormatItemIcon(const RE::GFxValue& a_previous) :
				previous(a_previous)
			{}

			void Call(Params& a_params) override
			{
				if (a_params.argCount < 3 || !a_params.thisPtr || !a_params.thisPtr->IsObject()) {
					CallPrevious(a_params);
					return;
				}
				auto&      self = *a_params.thisPtr;
				auto&      field = a_params.args[0];
				const auto movie = MovieForEntry(a_params.args[1]);

				RE::GFxValue shown;  // the movie of ours the entry shows, if any
				self.GetMember("_shngIcon", &shown);
				const std::string shownMovie = shown.IsString() ? shown.GetString() : "";

				if (!movie.empty()) {
					if (shownMovie != movie) {
						const RE::GFxValue source(movie.c_str());
						self.SetMember("_shngIcon", source);
						self.SetMember("_iconSource", source);  // I4 loads its own icon movie again when it differs
						LoadClip(a_params.movie, self, field, source);
					}
					return;
				}

				if (shownMovie.empty()) {
					CallPrevious(a_params);
					return;
				}
				// back to the previous icon (this entry clip is reused for another spell)
				self.DeleteMember("_shngIcon");
				CallPrevious(a_params);
				RE::GFxValue source;
				self.GetMember("_iconSource", &source);
				if (source.IsString() && shownMovie != source.GetString()) {
					return;  // I4 is loading its icon movie
				}
				// SkyUI alone: its icon movie, onLoadInit shows the label the previous function picked
				self.DeleteMember("_iconSource");
				RE::GFxValue stateSource;
				if (a_params.args[2].IsObject()) {
					a_params.args[2].GetMember("iconSource", &stateSource);
				}
				LoadClip(a_params.movie, self, field, stateSource.IsString() ? stateSource : RE::GFxValue(kSkyUIIcons));
			}

		private:
			void CallPrevious(Params& a_params)
			{
				if (previous.IsObject()) {
					previous.Invoke("call", a_params.retVal, a_params.argsWithThisRef, static_cast<std::size_t>(a_params.argCount) + 1);
				}
			}

			RE::GFxValue previous;
		};

		// UI thread: wraps formatItemIcon of the open Magic menu's movie once, then redraws the list
		void Install()
		{
			const auto ui = RE::UI::GetSingleton();
			const auto menu = ui ? ui->GetMenu<RE::MagicMenu>() : nullptr;
			const auto movie = menu ? menu->uiMovie : nullptr;
			if (!movie) {
				return;
			}
			RE::GFxValue entry;
			movie->GetVariable(&entry, "_global.InventoryListEntry.prototype");
			if (!entry.IsObject() || entry.HasMember("_shngMagicIcons")) {
				return;  // not SkyUI's menu (yet), or done
			}
			RE::GFxValue previous;
			entry.GetMember("formatItemIcon", &previous);
			if (!previous.IsObject()) {
				return;
			}
			RE::GFxValue function;
			movie->CreateFunction(&function, RE::make_gptr<FormatItemIcon>(previous).get());
			entry.SetMember("formatItemIcon", function);
			entry.SetMember("_shngMagicIcons", RE::GFxValue(true));

			RE::GFxValue list;
			movie->GetVariable(&list, "_root.Menu_mc.inventoryLists.itemList");
			if (list.IsObject()) {
				list.Invoke("UpdateList");
			}
			logs::info("Magic menu icons: SkyUI's magic list shows the bar's pictures");
		}

		void ClearCaches()
		{
			std::scoped_lock guard(lock);
			atlases.clear();
			pictures.clear();
		}
	}

	void Update()
	{
		static auto last = std::chrono::steady_clock::time_point{};
		static bool open = false;

		const auto ui = RE::UI::GetSingleton();
		const bool nowOpen = ui && ui->IsMenuOpen(RE::MagicMenu::MENU_NAME);
		if (open && !nowOpen) {
			SKSE::GetTaskInterface()->AddUITask(ClearCaches);  // the atlases are big: only kept while the menu is open
		}
		open = nowOpen;
		if (!nowOpen || !Config::Get().magicMenuIcons) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now - last < kRetry) {
			return;
		}
		last = now;
		SKSE::GetTaskInterface()->AddUITask(Install);
	}
}
