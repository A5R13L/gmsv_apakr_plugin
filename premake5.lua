PROJECT_GENERATOR_VERSION = 3

local gmcommon = "./garrysmod_common_64"
include(gmcommon)

newoption({
	trigger = "tag_version",
	value = "string",
	description = "The current version"
})

local function common_helper()
	filter("system:windows")
		links({"crypt32", "secur32", "iphlpapi"})

	filter("system:linux")
		links({"pthread", "curl"})

	filter {}

	defines({"APAKR_VERSION=\"" .. (_OPTIONS["tag_version"] or "unknown") .. "\""})
end

CreateWorkspace({name = "apakr_64", abi_compatible = false, path = "projects/apakr_64/" .. os.target() .. "/" .. _ACTION})
	CreateProject({serverside = true, source_path = "source", manual_files = false})
		common_helper()
		IncludeHelpersExtended()
		IncludeSDKCommon()
		IncludeSDKTier0()
		IncludeSDKTier1()
		IncludeDetouring()
		IncludeScanning()
		files({
			"source/**/*.*",
			"source/**/*.*",
		})

CreateWorkspace({name = "apakr_extension_example_64", abi_compatible = false, path = "projects/apakr_extension_example_64/" .. os.target() .. "/" .. _ACTION})
	CreateProject({serverside = true, source_path = "apakr_extension_example/source", manual_files = false})
		common_helper()
		IncludeHelpersExtended()
		IncludeSDKCommon()
		IncludeSDKTier0()
		IncludeSDKTier1()
		IncludeDetouring()
		IncludeScanning()
		files({
			"apakr_extension_example/source/**/*.*",
			"apakr_extension_example/source/**/*.*",
		})

gmcommon = "./garrysmod_common_32"
include(gmcommon)

CreateWorkspace({name = "apakr_32", abi_compatible = false, path = "projects/apakr_32/" .. os.target() .. "/" .. _ACTION})
	CreateProject({serverside = true, source_path = "source", manual_files = false})
		common_helper()
		IncludeHelpersExtended()
		IncludeSDKCommon()
		IncludeSDKTier0()
		IncludeSDKTier1()
		IncludeDetouring()
		IncludeScanning()
		files({
			"source/**/*.*",
			"source/**/*.*",
		})

CreateWorkspace({name = "apakr_extension_example_32", abi_compatible = false, path = "projects/apakr_extension_example_32/" .. os.target() .. "/" .. _ACTION})
	CreateProject({serverside = true, source_path = "apakr_extension_example/source", manual_files = false})
		common_helper()
		IncludeHelpersExtended()
		IncludeSDKCommon()
		IncludeSDKTier0()
		IncludeSDKTier1()
		IncludeDetouring()
		IncludeScanning()
		files({
			"apakr_extension_example/source/**/*.*",
			"apakr_extension_example/source/**/*.*",
		})