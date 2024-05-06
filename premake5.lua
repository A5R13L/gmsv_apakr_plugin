PROJECT_GENERATOR_VERSION = 3

local gmcommon = "./garrysmod_common_64"
include(gmcommon)

CreateWorkspace({name = "apakr_64", abi_compatible = false, path = "projects/x64/" .. os.target() .. "/" .. _ACTION})
	CreateProject({serverside = true, source_path = "source", manual_files = false})
		IncludeHelpersExtended()
		IncludeSDKCommon()
		IncludeSDKTier0()
		IncludeSDKTier1()
		IncludeDetouring()
		files({
			"source/**/*.*",
			"source/**/*.*",
		})

gmcommon = "./garrysmod_common_32"
include(gmcommon)

CreateWorkspace({name = "apakr_32", abi_compatible = false, path = "projects/x32/" .. os.target() .. "/" .. _ACTION})
	CreateProject({serverside = true, source_path = "source", manual_files = false})
		IncludeHelpersExtended()
		IncludeSDKCommon()
		IncludeSDKTier0()
		IncludeSDKTier1()
		IncludeDetouring()
		files({
			"source/**/*.*",
			"source/**/*.*",
		})