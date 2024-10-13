group("garrysmod_common")
	project("detouring")
		kind("StaticLib")
		location("projects/" .. os.target() .. "/" .. _ACTION)
		targetdir("%{prj.location}/%{cfg.architecture}/%{cfg.buildcfg}")
		debugdir("%{prj.location}/%{cfg.architecture}/%{cfg.buildcfg}")
		objdir("!%{prj.location}/%{cfg.architecture}/%{cfg.buildcfg}/intermediate/%{prj.name}")
		includedirs({"include/detouring", "hde/include", "minhook/include"})
		files({
			"include/detouring/*.hpp",
			"include/detouring/*.h",
			"source/*.cpp"
		})
		vpaths({
			["Header files"] = {
				"include/detouring/*.hpp",
				"include/detouring/*.h"
			},
			["Source files"] = "source/*.cpp"
		})
		links({"hde", "minhook"})

	project("hde")
		language("C")
		kind("StaticLib")
		location("projects/" .. os.target() .. "/" .. _ACTION)
		targetdir("%{prj.location}/%{cfg.architecture}/%{cfg.buildcfg}")
		debugdir("%{prj.location}/%{cfg.architecture}/%{cfg.buildcfg}")
		objdir("!%{prj.location}/%{cfg.architecture}/%{cfg.buildcfg}/intermediate/%{prj.name}")
		includedirs({"include/detouring", "hde/include"})
		files({
			"hde/include/*.h",
			"hde/src/hde.c"
		})
		vpaths({
			["Header files"] = "hde/include/*.h",
			["Source files/hde"] = "hde/src/*.c"
		})

	project("minhook")
		language("C")
		kind("StaticLib")
		location("projects/" .. os.target() .. "/" .. _ACTION)
		targetdir("%{prj.location}/%{cfg.architecture}/%{cfg.buildcfg}")
		debugdir("%{prj.location}/%{cfg.architecture}/%{cfg.buildcfg}")
		objdir("!%{prj.location}/%{cfg.architecture}/%{cfg.buildcfg}/intermediate/%{prj.name}")
		includedirs({"hde/include", "minhook/include"})
		files({
			"minhook/include/*.h",
			"minhook/src/*.h",
			"minhook/src/*.c"
		})
		vpaths({
			["Header files"] = {
				"minhook/include/*.h",
				"minhook/src/*.h"
			},
			["Source files"] = "minhook/src/*.c"
		})
		links("hde")
