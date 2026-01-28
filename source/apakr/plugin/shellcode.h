inline const char *APAKR_SHELLCODE = R"(
	apakr_current_gc = collectgarbage("count")

	local RunConsoleCommand = RunConsoleCommand
	local isfunction = isfunction
	local string_gsub = string.gsub
	local debug_getinfo = debug.getinfo
	local file_Exists = file.Exists
	local file_Open = file.Open
	local util_Decompress = util.Decompress
	local CompileString = CompileString
	local _CompileFile = CompileFile
	local util_SHA256 = util.SHA256
	local timer_Create = timer.Create
	local timer_Remove = timer.Remove
	local util_Base64Decode = util.Base64Decode
	local jit_flush = jit.flush
	local package_seeall = package.seeall
	local NoOperation = function() end
	local NoOperationReturn = function() return NoOperation end
	local ConVarFlags = {FCVAR_REPLICATED, FCVAR_DONTRECORD, FCVAR_PROTECTED, FCVAR_UNREGISTERED, FCVAR_UNLOGGED}
	local File = CreateConVar("apakr_file", "", ConVarFlags):GetString()
	local SHA256 = CreateConVar("apakr_sha256", "", ConVarFlags):GetString()
	local Key = CreateConVar("apakr_key", "", ConVarFlags):GetString()
	local DownloadFilter = GetConVar("cl_downloadfilter"):GetString()
	local APakr_AbsoluteVFS = {}
	local APakr_RelativeVFS = {}

	local function APakr_Log(String, ...)
		print(("[APakr]: " .. String):format(...))
	end

	local function APakr_Bail(Message, Disconnect)
		RunConsoleCommand("disconnect")

		if Disconnect then
			APakr = NoOperationReturn
			ErrorNoHalt(("----------------------!!!!-------------------\n[APakr]: Sorry! %s\nYou have been disconnected from the server because %s\n----------------------!!!!-------------------\n"):format(Message, Disconnect))
		else
			APakr = function()
				ErrorNoHalt(("----------------------!!!!-------------------\n[APakr]: Sorry! %s\n----------------------!!!!-------------------\n"):format(Message))
				APakr = NoOperationReturn
			end
		end

		module("gamemode", package_seeall)

		Register = NoOperation
		Get = NoOperation
	end

	{APAKR_DECRYPTION_FUNCTION}

	local function APakr_SaltedSHA256(Value)
		return util_SHA256(Key .. Value)
	end

	local function APakr_RemoveFromVFS(FullPath, RelativePath)
		local FullPathHash = APakr_SaltedSHA256(FullPath):sub(1, 15)
		local RelativePathHash = APakr_SaltedSHA256(RelativePath):sub(1, 15)

		if APakr_AbsoluteVFS[FullPathHash] or APakr_RelativeVFS[RelativePathHash] then
			APakr_Log("(Autorefresh) Removed %s from VFS.", FullPath)
		end

		APakr_AbsoluteVFS[FullPathHash] = nil
		APakr_RelativeVFS[RelativePathHash] = nil
	end

	local function ComputeVFSObject(VFSObject, Source)
		local Computed = VFSObject.Computed

		if Computed ~= nil then
			return Computed
		end
	
		Computed = CompileString(VFSObject.Contents, Source)

		if isfunction(Computed) then
			VFSObject.Computed = Computed
			VFSObject.Contents = nil
		end

		return Computed
	end

	if File == "" or SHA256 == "" then
		return APakr_Bail("A critical error caused APakr to not initialize properly...")
	end

	APakr_Log("Initializing...")
	APakr_Log("Made by Asriel.")

	local Start = SysTime()
	local Path = ("download/data/apakr/%s.bsp"):format(File)

	if not file_Exists(Path, "GAME") then
		if DownloadFilter == "none" then
			APakr_Bail("You have cl_downloadfilter set to none! You must have a minimum of mapsonly to join.", "pack file was not downloaded")
		else
			APakr_Bail("Pack file is missing! Please check your network connection and contact an admin if this persists.", "pack file missing")
		end

		return
	end

	local FileHandle = file_Open(Path, "rb", "GAME")
	local Contents = FileHandle:Read(FileHandle:Size())
	Contents = APakr_Decrypt(Contents)
	FileHandle:Close()

	if not Contents then
		return APakr_Bail(("Failed to decrypt pack file. Delete %s and try joining again."):format(Path), "Decryption failed")
	end

	Contents = util_Decompress(Contents)

	if not Contents then
		return APakr_Bail(("Failed to decompress pack file. Delete %s and try joining again."):format(Path), "Decompression failed")
	end

	local FileSHA256 = util_SHA256(Contents)

	if FileSHA256 ~= SHA256 then
		return APakr_Bail(("Pack mismatch! Delete %s and try joining again."):format(Path), "File mismatch")
	end

	APakr_Log("Pack file verified! Building VFS...")

	local Index = 1
	local Count = 0

	while Index < #Contents do
		local FullPathHash = Contents:sub(Index, Index + 14)
		Index = Index + 15
		local RelativePathHash = Contents:sub(Index, Index + 14)
		Index = Index + 15
		local Size = tonumber(Contents:sub(Index, Index + 5), 16)
		Index = Index + 6
		local File = Contents:sub(Index, Index + (Size - 1))
		Index = Index + Size

		local VFSObject = { Contents = File, Computed = nil }

		APakr_AbsoluteVFS[FullPathHash] = VFSObject
		APakr_RelativeVFS[RelativePathHash] = VFSObject
	
		Count = Count + 1
	end

	APakr_Log("VFS built in %0.2fs with %d files.", SysTime() - Start, Count)

	function APakr()
		local Info = debug_getinfo(2, "S")
		local Source = string_gsub(Info.source, "^@", "")
		local FullPathHash = APakr_SaltedSHA256(Source):sub(1, 15)
		local VFSObject = APakr_AbsoluteVFS[FullPathHash]

		if VFSObject then
			return ComputeVFSObject(VFSObject, Source)
		end

		ErrorNoHaltWithStack(("APakr: %s [%s] was missing from the VFS!"):format(Source, FullPathHash))
	end

	function CompileFile(FilePath, ShowError)
		local RelativePathHash = APakr_SaltedSHA256(FilePath):sub(1, 15)
		local VFSObject = APakr_RelativeVFS[RelativePathHash]

		if VFSObject then
			local Computed = ComputeVFSObject(VFSObject, FilePath)

			if not isfunction(Computed) and ShowError == false then
				return
			end

			return Computed
		end

		return _CompileFile(FilePath, ShowError)
	end

	timer_Create("gmsv_apakr::refresh", 0, 0, function()
		if not net or not net.Receive then return end

		timer_Remove("gmsv_apakr::refresh")

		net.Receive("gmsv_apakr::refresh", function()
			local FullPath = net.ReadString()
			local RelativePath = net.ReadString()

			APakr_RemoveFromVFS(FullPath, RelativePath)
		end)
	end)

	jit_flush()
	collectgarbage()
	collectgarbage()
)";