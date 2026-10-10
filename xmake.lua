-- include subprojects
includes("lib/commonlibsse-ng")

-- set project constants
set_project("SpellHotbarNG")
set_version("1.9.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")

add_requires("nlohmann_json")

-- define targets
target("SpellHotbarNG")
    add_rules("commonlibsse-ng.plugin", {
        name = "SpellHotbarNG",
        author = "Iuko",
        description = "Spell hotbar configured through SKSE Menu Framework"
    })

    add_packages("nlohmann_json")

    -- plugin sources
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    -- third party: SKSE Menu Framework API (header only) and the Open Animation Replacer condition API
    add_headerfiles("lib/SKSEMenuFramework/*.h")
    add_includedirs("lib/SKSEMenuFramework")
    add_files("lib/OpenAnimationReplacer/*.cpp")
    add_headerfiles("lib/OpenAnimationReplacer/*.h")
    add_includedirs("lib/OpenAnimationReplacer")
