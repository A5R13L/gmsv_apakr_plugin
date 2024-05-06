inline const char *APAKR_SHELLCODE = R"(
	apakr_current_gc = collectgarbage("count")

	local RunConsoleCommand = RunConsoleCommand
	local string_gsub = string.gsub
	local debug_getinfo = debug.getinfo
	local file_Exists = file.Exists
	local file_Open = file.Open
	local util_Decompress = util.Decompress
	local CompileString = CompileString
	local _CompileFile = CompileFile
	local util_SHA256 = util.SHA256
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
	local APakr_VFS = {}

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
		local Hash = Contents:sub(Index, Index + 14)
		Index = Index + 15
		local Size = tonumber(Contents:sub(Index, Index + 5), 16)
		Index = Index + 6
		local File = Contents:sub(Index, Index + Size - 1)
		Index = Index + Size

		APakr_VFS[Hash] = File
		Count = Count + 1
	end

	APakr_Log("VFS built in %0.2fs with %d files.", SysTime() - Start, Count)

	function APakr()
		local Info = debug_getinfo(2, "S")
		local Source = string_gsub(Info.source, "^@", "")
		local SourceHash = APakr_SaltedSHA256(Source):sub(1, 15)

		if APakr_VFS[SourceHash] then
			return CompileString(APakr_VFS[SourceHash], Source)
		end

		ErrorNoHaltWithStack(("APakr: %s [%s] was missing from the VFS!"):format(Source, SourceHash))
	end

	function CompileFile(FilePath, Source)
		local PathHash = APakr_SaltedSHA256(FilePath):sub(1, 15)

		if APakr_VFS[PathHash] then
			return CompileString(APakr_VFS[PathHash], Source or Path)()
		end

		return _CompileFile(FilePath, Source)
	end

	jit_flush()
	collectgarbage()
	collectgarbage()
)";