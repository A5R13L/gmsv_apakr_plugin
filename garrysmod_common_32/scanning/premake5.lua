local current_dir = _SCRIPT_DIR

function IncludeScanning()
	local refcount = IncludePackage("scanning")

	local _project = project()

	externalincludedirs(current_dir .. "/include")
	links("scanning")

	filter("system:linux or macosx")
		links("dl")

	filter("system:macosx")
		links("CoreServices.framework")

	if refcount == 1 then
		dofile(current_dir .. "/premake5_create_project.lua")
	end

	project(_project.name)
end
