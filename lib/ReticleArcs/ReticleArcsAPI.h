#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chiktabba
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include <cstdint>

// Vendored from https://codeberg.org/chiktabba/ReticleArcs (api/ReticleArcsAPI.h, commit 355e6d5). Only change:
// REX::W32 GetModuleHandleW / GetProcAddress (CommonLibSSE-NG builds without windows.h).

// Reticle Arcs add-on interface. UNSUPPORTED FOR NOW: it may change without notice while it settles.
//
// an add-on is an SKSE plugin showing its own bar beside Reticle Arcs' own. Reticle Arcs draws it in the player's look and
// asks the add-on for its state every frame; the add-on only reads the game. an add-on can also colour the built-in bars and
// read what they show. no CommonLib types cross it, so add-ons don't need the same CommonLib build. a crash inside an
// add-on's callback stops that add-on, not the game
namespace ReticleArcsAPI {
    // 2: colours for the built-in bars (SetColorProvider), reading them (GetBarState)
    inline constexpr std::uint32_t kVersion = 2;

    // the built-in bars
    enum class Bar : std::uint32_t {
        kLeftSpell,
        kRightSpell,
        kShout,
        // bow and crossbow
        kBow,
    };

    // as Reticle Arcs detects it, from the spell's effects, keywords and casting light
    enum class Element : std::uint32_t { kNone, kFire, kFrost, kShock, kSun, kPoison, kDrain };

    // the spell's costliest effect's school. kOther: a skill a mod made a school
    enum class School : std::uint32_t { kNone, kAlteration, kConjuration, kDestruction, kIllusion, kRestoration, kOther };

    // with drain, what moves along the fill
    enum class Motion : std::uint8_t {
        kNone,
        // a soft spot running along it
        kFlow,
        // the fill breathing brighter
        kPulse,
        // shown as kFlow since 2.0
        kMotes,
        // whatever the player picked for concentration spells
        kPlayers,
    };

    // what the bar shows this frame. Reticle Arcs hands it over zeroed; later versions only add fields at the end
    struct BarState {
        // 0 to 1
        float percent = 0.0f;
        // shown. going from shown to not plays the bar's release effect
        bool active = false;
        // empties towards its start and follows the value without easing, like a concentration spell. rises still ease,
        // and reaching full that way plays the bar's charged effect
        bool drain = false;
        Motion motion = Motion::kNone;
        // split into this many pieces, like a shout's words. 0 or 1 for one piece, 12 at most
        std::uint32_t segments = 0;
        // in the warning colour
        bool low = false;
        // the track pulses in the warning colour
        bool alarm = false;
        // a rim in the warning colour, like the bow's for its last arrow
        bool rim = false;
        // this colour, 0xRRGGBB, instead of the player's for this bar
        bool useColor = false;
        std::uint32_t color = 0;
        // for one frame while not shown: the warning flash, as for a spell that can't be afforded
        bool refused = false;
        // for the frame it stops showing: the player's break effect instead of the release, as for a lost charge
        bool broken = false;
    };

    struct BarDefinition {
        // leave as is: tells Reticle Arcs which version of this struct it was given
        std::uint32_t size = sizeof(BarDefinition);
        // unique, letters, digits, _ and - only, 48 at most. the ini section is [AddOn.<id>]. start it with your mod's name
        const char* id = nullptr;
        // the menu's label and tooltip, the tooltip may be nullptr. Translation.ini's [AddOns] <id> and <id>Tip replace them
        const char* name = nullptr;
        const char* tooltip = nullptr;
        // defaults until the player changes them: fill as 0xRRGGBB and 0-255 opacity, a side
        std::uint32_t color = 0xFFFFFF;
        std::uint8_t alpha = 200;
        // its side of the ring outside the built-in bars : top, bottom, left or right
        const char* placement = "bottom";
        bool enabled = true;
        // main thread, every frame the game runs and no kill move or killcam is playing. seconds since the last call.
        // keep it cheap, the frame waits for it
        void (*update)(float a_delta, BarState& a_state) = nullptr;
        // after a save loads, may be nullptr
        void (*reset)() = nullptr;
        // the settings page's preview, seconds since it opened. nullptr shows a plain sweep
        void (*preview)(float a_time, BarState& a_state) = nullptr;
    };

    // what a built-in bar is showing. later versions only add fields at the end
    struct ColorQuery {
        std::uint32_t size = sizeof(ColorQuery);
        Bar bar = Bar::kLeftSpell;
        // the spell, scroll or staff enchantment being cast; the shout; the arrow or bolt nocked. never 0
        std::uint32_t formID = 0;
        // spells: both hands casting one spell, a dual cast or master ritual. asked once, for the hand casting it
        bool dual = false;
        // spells: a concentration spell being channelled, not charging
        bool channel = false;
        // spells, and elemental arrows and bolts. kNone for the rest
        Element element = Element::kNone;
        // spells only
        School school = School::kNone;
        // the colour Reticle Arcs shows when you return false, 0xRRGGBB
        std::uint32_t fallback = 0;
    };

    // main thread, every frame a built-in bar shows while the player picked one of the "Add-on" colours for it. true and
    // a_rgb (0xRRGGBB) colour it, false leaves it to the player's fallback. keep it cheap, the frame waits for it
    using ColorFn = bool (*)(const ColorQuery& a_query, std::uint32_t& a_rgb);

    // a built-in bar as of Reticle Arcs' last update, once a frame. later versions only add fields at the end
    struct BarInfo {
        // leave as is: tells Reticle Arcs which version of this struct it was given
        std::uint32_t size = sizeof(BarInfo);
        // charging, drawing, held or channelled. tracked whether or not the player shows the bar
        bool active = false;
        // the player shows this bar
        bool enabled = false;
        // 0 to 1. a channel's share of the magicka left
        float percent = 0.0f;
        // what's in it, as in ColorQuery. 0 while not active
        std::uint32_t formID = 0;
        bool dual = false;
        bool channel = false;
        // the bow bar showing a crossbow's reload
        bool crossbow = false;
        // in the warning colour, like a channel running out of magicka
        bool low = false;
        // the shout's words
        std::uint32_t segments = 0;
        // the fill colour shown, 0xRRGGBB
        std::uint32_t color = 0;
        // for one update each: stopped showing (cast, shot, shouted, or let go), the charge was lost (a stagger) rather than
        // used, a cast or shot pressed with too little magicka or no arrows
        bool ended = false;
        bool broken = false;
        bool refused = false;
    };

    class IReticleArcs {
    public:
        // the newest version this Reticle Arcs offers
        virtual std::uint32_t GetVersion() const = 0;
        // during SKSE's kPostLoad message. false when refused, the reason goes to ReticleArcs.log
        virtual bool RegisterBar(const BarDefinition& a_bar) = 0;

        // version 2. any time from SKSE's kPostLoad message on; one provider at a time, the last one set wins. nullptr
        // removes it
        virtual void SetColorProvider(ColorFn a_provider) = 0;
        // version 2. main thread. false for a bar that isn't one, or while the settings page shows its preview
        virtual bool GetBarState(Bar a_bar, BarInfo& a_info) const = 0;

    protected:
        ~IReticleArcs() = default;
    };

    // exported by ReticleArcs.dll
    using RequestFunction = IReticleArcs* (*)(std::uint32_t a_version);

    // nullptr when Reticle Arcs isn't installed or is older than the version asked for. an add-on needing only bars can
    // ask for 1 and keep working with older Reticle Arcs
    inline IReticleArcs* Request(std::uint32_t a_version = kVersion) {
        auto* module = REX::W32::GetModuleHandleW(L"ReticleArcs.dll");
        auto* request = module ? reinterpret_cast<RequestFunction>(REX::W32::GetProcAddress(module, "RequestReticleArcsAPI")) : nullptr;
        return request ? request(a_version) : nullptr;
    }
}
