group("garrysmod_common")
	project("scanning")
		kind("StaticLib")
		location("projects/" .. os.target() .. "/" .. _ACTION)
		targetdir("%{prj.location}/%{cfg.architecture}/%{cfg.buildcfg}")
		debugdir("%{prj.location}/%{cfg.architecture}/%{cfg.buildcfg}")
		objdir("!%{prj.location}/%{cfg.architecture}/%{cfg.buildcfg}/intermediate/%{prj.name}")
		includedirs("include/scanning")
		files({
			"include/scanning/*.hpp",
			"source/*.cpp",
			"source/" .. os.target() .. "/*.cpp"
		})
		vpaths({
			["Header files/*"] = "include/scanning/*.hpp",
			["Source files/*"] = {"source/*.cpp", "source/" .. os.target() .. "/*.cpp"}
		})
