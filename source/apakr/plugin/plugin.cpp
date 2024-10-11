#include <apakr/plugin/plugin.h>

GModDataPackProxy GModDataPackProxy::Singleton;
IVEngineServerProxy IVEngineServerProxy::Singleton;
CApakrPlugin *CApakrPlugin::Singleton = new CApakrPlugin();
IServer *g_pServer = nullptr;
IVEngineServer *g_pVEngineServer = nullptr;
CNetworkStringTableContainer *g_pNetworkStringTableContainer = nullptr;
CNetworkStringTable *g_pClientLuaFiles = nullptr;
CNetworkStringTable *g_pDownloadables = nullptr;
GarrysMod::Lua::ILuaShared *g_pILuaShared = nullptr;
GarrysMod::Lua::ILuaInterface *g_pLUAServer = nullptr;
ConVar *apakr_file = nullptr;
ConVar *apakr_sha256 = nullptr;
ConVar *apakr_key = nullptr;
ConVar *apakr_clone_directory = nullptr;
ConVar *apakr_upload_url = nullptr;
ConVar *sv_downloadurl = nullptr;
bool DownloadURLChanged = false;
bool LuaValue = false;
std::string CurrentDownloadURL;
luaL_loadbufferx_t luaL_loadbufferx_Original = nullptr;
Detouring::Hook LoadBufferXHook;

#if defined APAKR_32_SERVER

IFileSystem *g_pFullFileSystem = nullptr;

#endif

void OnCloneDirectoryChanged_Callback(ConVar *_this, const char *OldString, float OldFloat)
{
    if (!CApakrPlugin::Singleton->Ready || CApakrPlugin::Singleton->CurrentPackName == "")
        return;

    Msg("\x1B[94m[Apakr]: \x1B[97mapakr_clone_directory was changed. Issuing repack.\n");

    CApakrPlugin::Singleton->NeedsRepack = true;
}

void OnUploadURLChanged_Callback(ConVar *_this, const char *OldString, float OldFloat)
{
    if (!CApakrPlugin::Singleton->Ready || CApakrPlugin::Singleton->CurrentPackName == "")
        return;

    Msg("\x1B[94m[Apakr]: \x1B[97mapakr_upload_url was changed. Issuing repack.\n");

    CApakrPlugin::Singleton->NeedsRepack = true;
}

void OnDownloadURLChanged_Callback(ConVar *_this, const char *OldString, float OldFloat)
{
    if (DownloadURLChanged || !CApakrPlugin::Singleton->Ready || CApakrPlugin::Singleton->CurrentPackName == "")
        return;

    Msg("\x1B[94m[Apakr]: \x1B[97msv_downloadurl was changed. Issuing repack.\n");

    CurrentDownloadURL = GetConvarString(sv_downloadurl);
    CApakrPlugin::Singleton->NeedsRepack = true;
}

DataPackEntry::DataPackEntry(const std::string &EntryFilePath, const std::string &EntryCode,
                             const std::string &EntryOriginalCode)
    : FilePath(EntryFilePath), Contents(EntryCode), OriginalContents(EntryOriginalCode)
{
    ReplaceAll(this->Contents, "\r", "");
    GModDataPackProxy::Singleton.ProcessFile(this->Contents);

    this->Size = (int)this->Contents.size() + 1;
    this->OriginalSize = (int)this->OriginalContents.size() + 1;
    this->SHA256 = GModDataPackProxy::Singleton.GetSHA256(this->Contents.data(), this->Size);
    this->CompressedContents = GModDataPackProxy::Singleton.Compress(this->Contents);
}

FileEntry::FileEntry(const std::string &EntryContents, int EntrySize) : Contents(EntryContents), Size(EntrySize)
{
}

int luaL_loadbufferx_Hook(lua_State *L, const char *Buffer, size_t Size, const char *Name, const char *Mode)
{
    static luaL_loadbufferx_t Trampoline = LoadBufferXHook.GetTrampoline<luaL_loadbufferx_t>();
    std::string Code(Buffer, Size);

    GModDataPackProxy::Singleton.ProcessFile(Code);

    return Trampoline(L, Code.data(), Code.size(), Name, Mode);
}

bool CApakrPlugin::Load(CreateInterfaceFn InterfaceFactory, CreateInterfaceFn GameServerFactory)
{
    Msg("\x1B[94m[Apakr]: \x1B[97mLoading...\n");

    g_pFullFileSystem = InterfacePointers::Internal::Server::FileSystem();

    if (!g_pFullFileSystem)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mIFileSystem\x1B[97m!\n");

        return true;
    }

    g_pNetworkStringTableContainer =
        (CNetworkStringTableContainer *)InterfacePointers::Internal::Server::NetworkStringTableContainer();

    if (!g_pNetworkStringTableContainer)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mINetworkStringTableContainer\x1B[97m!\n");

        return true;
    }

    g_pServer = InterfacePointers::Server();

    if (!g_pServer)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mIServer\x1B[97m!\n");

        return true;
    }

    g_pVEngineServer = InterfacePointers::VEngineServer();

    if (!g_pVEngineServer)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mIVEngineServer\x1B[97m!\n");

        return true;
    }

    g_pCVar = InterfacePointers::Cvar();

    if (!g_pCVar)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mICVar\x1B[97m!\n");

        return true;
    }

    SourceSDK::FactoryLoader LuaSharedFactoryLoader("lua_shared");

    if (!LuaSharedFactoryLoader.IsValid())
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mlua_shared\x1B[97m FactoryLoader!\n");

        return true;
    }

    g_pILuaShared = LuaSharedFactoryLoader.GetInterface<GarrysMod::Lua::ILuaShared>(GMOD_LUASHARED_INTERFACE);

    if (!g_pILuaShared)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mILuaShared\x1B[97m!\n");

        return true;
    }

    luaL_loadbufferx_Original = (luaL_loadbufferx_t)LuaSharedFactoryLoader.GetSymbol("luaL_loadbufferx");

    if (!luaL_loadbufferx_Original)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mluaL_loadbufferx\x1B[97m!\n");

        return true;
    }

    if (!LoadBufferXHook.Create(Detouring::Hook::Target((void *)luaL_loadbufferx_Original),
                                (void *)luaL_loadbufferx_Hook))
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to hook \x1B[91mluaL_loadbufferx\x1B[97m!\n");

        return true;
    }

    if (!LoadBufferXHook.Enable())
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to enable \x1B[91mluaL_loadbufferx\x1B[97m hook!\n");

        return true;
    }

    ConVar_Register();

    apakr_file = new ConVar("apakr_file", "", FCVAR_GAMEDLL | FCVAR_REPLICATED | FCVAR_LUA_SERVER);
    apakr_sha256 = new ConVar("apakr_sha256", "", FCVAR_GAMEDLL | FCVAR_REPLICATED | FCVAR_LUA_SERVER);
    apakr_key = new ConVar("apakr_key", "", FCVAR_GAMEDLL | FCVAR_REPLICATED | FCVAR_LUA_SERVER);

    apakr_clone_directory =
        new ConVar("apakr_clone_directory", "", FCVAR_GAMEDLL | FCVAR_LUA_SERVER,
                   "Where to clone the data packs for FastDL.", (FnChangeCallback_t)OnCloneDirectoryChanged_Callback);

    apakr_upload_url = new ConVar("apakr_upload_url", "https://apakr.asriel.dev/", FCVAR_GAMEDLL | FCVAR_LUA_SERVER,
                                  "Custom self-hosting url.", (FnChangeCallback_t)OnUploadURLChanged_Callback);

    sv_downloadurl = g_pCVar->FindVar("sv_downloadurl");

    InstallConvarChangeCallback(sv_downloadurl, (FnChangeCallback_t)OnDownloadURLChanged_Callback);

    CurrentDownloadURL = GetConvarString(sv_downloadurl);

    this->LoadPreprocessorTemplates();

    if (GModDataPackProxy::Singleton.Load() && IVEngineServerProxy::Singleton.Load())
        this->Loaded = true;

    return true;
}

void CApakrPlugin::Unload()
{
    Msg("\x1B[94m[Apakr]: \x1B[97mUnloading...\n");

    if (!this->Loaded)
        return;

    RemoveConvarChangeCallback(sv_downloadurl, (FnChangeCallback_t)OnDownloadURLChanged_Callback);
    RemoveConvarChangeCallback(apakr_clone_directory, (FnChangeCallback_t)OnCloneDirectoryChanged_Callback);
    RemoveConvarChangeCallback(apakr_upload_url, (FnChangeCallback_t)OnUploadURLChanged_Callback);

    if (g_pLUAServer)
    {
        g_pLUAServer->PushNil();
        g_pLUAServer->SetField(GarrysMod::Lua::INDEX_GLOBAL, "APakr");
    }

    for (auto &[_, PackEntry] : this->FileMap)
        if (PackEntry.SHA256)
            delete PackEntry.SHA256;

    ConVar_Unregister();
    GModDataPackProxy::Singleton.Unload();
    IVEngineServerProxy::Singleton.Unload();
}

void CApakrPlugin::ServerActivate(edict_t *EntityList, int EntityCount, int MaxClients)
{
    g_pClientLuaFiles = (CNetworkStringTable *)g_pNetworkStringTableContainer->FindTable("client_lua_files");

    if (!g_pClientLuaFiles)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get stringtable \x1B[91mclient_lua_files\x1B[97m.\n");

        return;
    }

    g_pDownloadables = (CNetworkStringTable *)g_pNetworkStringTableContainer->FindTable("downloadables");

    if (!g_pDownloadables)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get stringtable \x1B[91mdownloadables\x1B[97m.\n");

        return;
    }

    this->Players.resize(MaxClients);

    this->Ready = true;
    this->LastRepack = std::chrono::system_clock::now();
}

void CApakrPlugin::GameFrame(bool Simulating)
{
    if (!this->Loaded)
        return;

    this->CheckForRepack();

    if (this->CurrentPackName != GetConvarString(apakr_file))
        apakr_file->SetValue(this->CurrentPackName.c_str());

    if (this->CurrentPackSHA256 != GetConvarString(apakr_sha256))
        apakr_sha256->SetValue(this->CurrentPackSHA256.c_str());

    if (this->CurrentPackKey != GetConvarString(apakr_key))
        apakr_key->SetValue(this->CurrentPackKey.c_str());

    if (this->TemplatePath != "" &&
        TimeSince<std::chrono::seconds>(this->LastTemplateUpdate) >= std::chrono::seconds(1))
        this->LoadPreprocessorTemplates();

    if (!g_pLUAServer)
        g_pLUAServer = g_pILuaShared->GetLuaInterface(GarrysMod::Lua::State::SERVER);

    bool DesiredLuaValue = this->PackReady;

    if (LuaValue != DesiredLuaValue && g_pLUAServer)
    {
        g_pLUAServer->PushBool(DesiredLuaValue);
        g_pLUAServer->SetField(GarrysMod::Lua::INDEX_GLOBAL, "APakr");

        LuaValue = DesiredLuaValue;
    }

    if (this->Disabled)
        return;

    if (this->Packing)
        for (GmodPlayer *Player : this->Players)
            if (Player && Player->Client && Player->LoadingIn && Player->Client->IsConnected())
                Player->Client->Reconnect();
}

void CApakrPlugin::ClientActive(edict_t *Entity)
{
    if (!this->Loaded)
        return;

    int Index = Entity->m_EdictIndex - 1;

    if (!this->Players[Index])
        return;

    this->Players[Index]->LoadingIn = false;
}

void CApakrPlugin::ClientDisconnect(edict_t *Entity)
{
    if (!this->Loaded)
        return;

    int Index = Entity->m_EdictIndex - 1;

    if (!this->Players[Index])
        return;

    this->Players[Index]->Client = nullptr;
}

PLUGIN_RESULT CApakrPlugin::ClientConnect(bool *AllowConnection, edict_t *Entity, const char *Name, const char *Address,
                                          char *Rejection, int MaxRejectionLength)
{
    if (!this->Loaded)
        return PLUGIN_CONTINUE;

    int Index = Entity->m_EdictIndex - 1;
    IClient *Client = g_pServer->GetClient(Index);

    if (!this->Players[Index])
        this->Players[Index] = new GmodPlayer(Client, true);
    else
    {
        this->Players[Index]->Client = Client;
        this->Players[Index]->LoadingIn = true;
    }

    return PLUGIN_CONTINUE;
}

void CApakrPlugin::CheckForRepack()
{
    if (!this->Ready || !this->NeedsRepack ||
        TimeSince<std::chrono::seconds>(this->LastRepack) < std::chrono::seconds(1))
        return;

    this->NeedsRepack = false;
    this->LastRepack = std::chrono::system_clock::now();
    this->UnpackedSize = 0;
    this->PackedSize = 0;
    this->WasDisabled = this->Disabled;
    this->Disabled = false;

    Msg("\x1B[94m[Apakr]: \x1B[97mRebuilding data pack file...\n");
    this->SetupClientFiles();
    this->BuildAndWriteDataPack();
}

std::pair<std::string, int> CApakrPlugin::GetDataPackInfo()
{
    int NeededSize = 0;
    std::vector<std::pair<std::string, std::string>> FileList;
    std::string OutputBuffer;

    for (auto &[_, PackEntry] : this->DataPackMap)
    {
        NeededSize += 15;
        NeededSize += 6;
        NeededSize += PackEntry.OriginalSize;

        FileList.push_back({PackEntry.FilePath, GModDataPackProxy::Singleton.SHA256ToHex(PackEntry.SHA256)});
    }

    std::sort(FileList.begin(), FileList.end(),
              [](std::pair<std::string, std::string> &First, std::pair<std::string, std::string> &Second) {
                  return First.first < Second.first;
              });

    std::string SHABuffer;

    for (auto &[FilePath, FileSHA256] : FileList)
        SHABuffer += FilePath + ":" + FileSHA256;

    return {GModDataPackProxy::Singleton.GetHexSHA256(SHABuffer), NeededSize};
}

void CApakrPlugin::SetupClientFiles()
{
    if (this->Disabled && this->WasDisabled)
        return;

    for (int Index = 1; Index < g_pClientLuaFiles->GetNumStrings(); ++Index)
    {
        const char *FilePath = g_pClientLuaFiles->GetString(Index);
        FileEntry &FileMapEntry = this->FileMap[FilePath];
        DataPackEntry &DataPackMapEntry = this->DataPackMap[Index];

        if (FileMapEntry.Contents.empty())
            continue;

        if (this->Disabled)
            g_pClientLuaFiles->SetStringUserData(Index, 32, FileMapEntry.SHA256);
        else
        {
            if (this->WasDisabled)
                g_pClientLuaFiles->SetStringUserData(Index, 32, DataPackMapEntry.SHA256.data());

            this->UnpackedSize += FileMapEntry.Size;

            if (!FileMapEntry.SHA256)
            {
                FileMapEntry.SHA256 = new uint8_t[32];

                const uint8_t *Data = (const uint8_t *)g_pClientLuaFiles->GetStringUserData(Index, nullptr);

                for (int ByteIndex = 0; ByteIndex < 32; ++ByteIndex)
                    FileMapEntry.SHA256[ByteIndex] = Data[ByteIndex];
            }

            if (!DataPackMapEntry.CompressedContents.empty())
            {
                DataPackMapEntry.OriginalContents = FileMapEntry.Contents;
                DataPackMapEntry.OriginalSize = (int)DataPackMapEntry.OriginalContents.size() + 1;

                continue;
            }

            std::string DataPackContent = "return APakr()()";

            if (strstr(FilePath, "lua/includes/init.lua"))
            {
                std::string Shellcode = APAKR_SHELLCODE;

                DataPackContent = ReplaceAll(Shellcode, "{APAKR_DECRYPTION_FUNCTION}", APAKR_DECRYPTION_FUNCTION);

                DataPackContent.append("\n\n").append(FileMapEntry.Contents);
            }

            DataPackMapEntry = this->DataPackMap[Index] =
                DataPackEntry(FilePath, DataPackContent, FileMapEntry.Contents);

            g_pClientLuaFiles->SetStringUserData(Index, 32, DataPackMapEntry.SHA256.data());
        }
    }
}

std::string LastHTTPResponse;

int ProgressCallback(void *, curl_off_t, curl_off_t, curl_off_t TotalToUpload, curl_off_t CurrentUpload)
{
    if (TotalToUpload > 0)
        DisplayProgressBar(CurrentUpload / TotalToUpload);

    return 0;
}

size_t Write_Callback(void *Contents, size_t Size, size_t Bytes, void *)
{
    size_t TotalSize = Size * Bytes;

    LastHTTPResponse.append((char *)Contents, TotalSize);

    return TotalSize;
}

size_t WriteEmpty_Callback(void *, size_t Size, size_t Bytes, void *)
{
    return Size * Bytes;
}

size_t WriteHeader_Callback(char *Buffer, size_t Size, size_t Items, std::map<std::string, std::string> *Headers)
{
    std::string HeaderLine = Buffer;

    HeaderLine = HeaderLine.substr(0, HeaderLine.size() - 2);

    if (HeaderLine.empty())
        return Size * Items;

    size_t Separator = HeaderLine.find(':');

    if (Separator == std::string::npos)
        return Size * Items;

    (*Headers)[HeaderLine.substr(0, Separator)] = HeaderLine.substr(Separator + 2);

    return Size * Items;
}

void WaitForSteam_Thread()
{
    std::string ServerIP = g_pVEngineServer->GMOD_GetServerAddress();

    ServerIP = ServerIP.substr(0, ServerIP.find(":"));

    Msg("\x1B[94m[Apakr]: \x1b[97mNot connected to steam. Waiting...\n");

    while (ServerIP == "0.0.0.0")
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        ServerIP = g_pVEngineServer->GMOD_GetServerAddress();
        ServerIP = ServerIP.substr(0, ServerIP.find(":"));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

bool CApakrPlugin::UploadDataPack(const std::string &UploadURL, const std::string &Pack,
                                  const std::vector<std::string> &PreviousPacks)
{
    Msg("\x1B[94m[Apakr]: \x1b[97mUploading data pack...\n");
    Msg("\x1B[94m[Apakr]: \x1b[97mUpload URL: \x1B[93m%s\x1b[97m\n", UploadURL.c_str());

    this->LastUploadBegan = std::chrono::system_clock::now();

    char FullPath[MAX_PATH];

    if (!g_pFullFileSystem->RelativePathToFullPath_safe(Pack.c_str(), "GAME", FullPath))
    {
        Msg("\x1B[94m[Apakr]: \x1b[97mFailed to get full file path while uploading.\n");

        return false;
    }

    CURL *Handle = curl_easy_init();
    std::string ServerIP = g_pVEngineServer->GMOD_GetServerAddress();

    ServerIP = ServerIP.substr(0, ServerIP.find(":"));

    if (ServerIP == "0.0.0.0")
    {
        std::thread(WaitForSteam_Thread).join();

        ServerIP = g_pVEngineServer->GMOD_GetServerAddress();
        ServerIP = ServerIP.substr(0, ServerIP.find(":"));
    }

    if (Handle)
    {
        std::string AuthorizationHeader = "Authorization: ";
        std::string DownloadURLHeader = "X-Download-URL: ";
        curl_slist *Headers = nullptr;
        char ErrorBuffer[CURL_ERROR_SIZE];
        curl_mime *Form = curl_mime_init(Handle);
        curl_mimepart *Field = curl_mime_addpart(Form);
        long HTTPCode;
        CURLcode ResponseCode;
        std::map<std::string, std::string> ResponseHeaders;

        LastPercentage = -1;
        LastHTTPResponse = "";
        AuthorizationHeader += GModDataPackProxy::Singleton.GetHexSHA256(ServerIP);
        DownloadURLHeader += CurrentDownloadURL;
        Headers = curl_slist_append(Headers, AuthorizationHeader.c_str());
        Headers = curl_slist_append(Headers, DownloadURLHeader.c_str());
        Headers = curl_slist_append(Headers, "User-Agent: apakr_server");

        curl_mime_name(Field, "file");
        curl_mime_filename(Field, Pack.c_str());
        curl_mime_filedata(Field, FullPath);

        std::string DeletionRequest = "[";

        for (const std::string &PackName : PreviousPacks)
        {
            if (DeletionRequest != "[")
                DeletionRequest += ",";

            DeletionRequest += "\"" + PackName + "\"";
        }

        DeletionRequest += "]";
        Field = curl_mime_addpart(Form);

        curl_mime_name(Field, "delete");
        curl_mime_data(Field, DeletionRequest.c_str(), CURL_ZERO_TERMINATED);
        curl_easy_setopt(Handle, CURLOPT_URL, UploadURL.c_str());
        curl_easy_setopt(Handle, CURLOPT_HTTPHEADER, Headers);
        curl_easy_setopt(Handle, CURLOPT_MIMEPOST, Form);
        curl_easy_setopt(Handle, CURLOPT_NOPROGRESS, 0);
        curl_easy_setopt(Handle, CURLOPT_ERRORBUFFER, ErrorBuffer);
        curl_easy_setopt(Handle, CURLOPT_WRITEFUNCTION, Write_Callback);
        curl_easy_setopt(Handle, CURLOPT_WRITEDATA, nullptr);
        curl_easy_setopt(Handle, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
        curl_easy_setopt(Handle, CURLOPT_XFERINFODATA, nullptr);
        curl_easy_setopt(Handle, CURLOPT_HEADERFUNCTION, WriteHeader_Callback);
        curl_easy_setopt(Handle, CURLOPT_HEADERDATA, &ResponseHeaders);
        curl_easy_setopt(Handle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        if (IsBridgedInterface())
            curl_easy_setopt(Handle, CURLOPT_INTERFACE, ServerIP.c_str());

        ResponseCode = curl_easy_perform(Handle);

        curl_easy_getinfo(Handle, CURLINFO_HTTP_CODE, &HTTPCode);
        curl_mime_free(Form);
        curl_easy_cleanup(Handle);

        if (ResponseCode != CURLE_OK)
        {
            Msg("\x1B[94m[Apakr]: \x1b[97mReceived an invalid curl code [%d] while uploading.\n", ResponseCode);

            return false;
        }

        if (HTTPCode != 200)
        {
            Msg("\x1B[94m[Apakr]: \x1b[97mReceived an invalid response code [%ld] while uploading.\n", HTTPCode);
            Msg("\x1B[94m[Apakr]: \x1b[97mBody: %s\n", LastHTTPResponse.data());
            Msg("\x1B[94m[Apakr]: \x1b[97mIP: %s\n", ServerIP.data());

            return false;
        }

        std::string XDownloadURL = ResponseHeaders["x-download-url"];

        if (XDownloadURL.empty())
        {
            Msg("\x1B[94m[Apakr]: \x1b[97mX-Download-URL was not sent in response headers.\n");
            Msg("\x1B[94m[Apakr]: \x1b[97mBody: %s\n", LastHTTPResponse.data());

            return false;
        }

        Msg("\x1B[94m[Apakr]: \x1b[97mData pack uploaded successfully in \x1B[93m%0.2f \x1B[97mseconds! Checking if it "
            "is reachable...\n",
            TimeSince<std::chrono::milliseconds>(this->LastUploadBegan).count() / 1000.0f);

        DownloadURLChanged = true;

        sv_downloadurl->SetValue(XDownloadURL.c_str());

        DownloadURLChanged = false;

        if (!this->CanDownloadPack(XDownloadURL))
            return false;
    }

    return true;
}

bool CApakrPlugin::CanDownloadPack(const std::string &DownloadURL)
{
    CURL *Handle = curl_easy_init();

    if (Handle)
    {
        long HTTPCode;
        CURLcode ResponseCode;
        curl_slist *Headers = nullptr;
        char ErrorBuffer[CURL_ERROR_SIZE];
        std::string ServerIP = g_pVEngineServer->GMOD_GetServerAddress();
        std::string CompleteDownloadURL = DownloadURL;

        ServerIP = ServerIP.substr(0, ServerIP.find(":"));
        Headers = curl_slist_append(Headers, "User-Agent: Half-Life 2");
        CompleteDownloadURL += "data/apakr/" + this->CurrentPackName + ".bsp.bz2";
        LastHTTPResponse = "";

        curl_easy_setopt(Handle, CURLOPT_URL, CompleteDownloadURL.c_str());
        curl_easy_setopt(Handle, CURLOPT_HTTPHEADER, Headers);
        curl_easy_setopt(Handle, CURLOPT_NOPROGRESS, 1);
        curl_easy_setopt(Handle, CURLOPT_FOLLOWLOCATION, 1);
        curl_easy_setopt(Handle, CURLOPT_WRITEFUNCTION, WriteEmpty_Callback);
        curl_easy_setopt(Handle, CURLOPT_ERRORBUFFER, ErrorBuffer);
        curl_easy_setopt(Handle, CURLOPT_WRITEFUNCTION, Write_Callback);
        curl_easy_setopt(Handle, CURLOPT_WRITEDATA, nullptr);
        curl_easy_setopt(Handle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        if (IsBridgedInterface())
            curl_easy_setopt(Handle, CURLOPT_INTERFACE, ServerIP.c_str());

        ResponseCode = curl_easy_perform(Handle);

        curl_easy_getinfo(Handle, CURLINFO_HTTP_CODE, &HTTPCode);
        curl_easy_cleanup(Handle);

        if (ResponseCode != CURLE_OK)
        {
            Msg("\x1B[94m[Apakr]: \x1b[97mReceived an invalid curl code [%d] while checking if data pack is "
                "downloadable.\n",
                ResponseCode);

            return false;
        }
        if (HTTPCode != 200)
        {
            Msg("\x1B[94m[Apakr]: \x1b[97mReceived an invalid response code [%ld] while checking if data pack is "
                "downloadable.\n",
                HTTPCode);

            Msg("\x1B[94m[Apakr]: \x1b[97mBody: %s\n", LastHTTPResponse.data());

            return false;
        }

        Msg("\x1B[94m[Apakr]: \x1b[97mData pack is reachable! Finished in \x1B[93m%0.2f \x1B[97mseconds.\n",
            TimeSince<std::chrono::milliseconds>(this->LastRepack).count() / 1000.0f);

        return true;
    }

    return false;
}

void FailedDataPackUpload_Thread(const std::string &UploadURL, const std::string &CurrentFile,
                                 const std::string &FilePath, const std::vector<std::string> &PreviousPacks,
                                 int Attempt)
{
    if (Attempt == 3)
    {
        CApakrPlugin::Singleton->Packing = false;
        CApakrPlugin::Singleton->Disabled = true;

        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to upload data pack after 3 tries. Falling back to regular networking.\n");
        g_pFullFileSystem->RemoveFile(FilePath.c_str(), "GAME");
        CApakrPlugin::Singleton->SetupClientFiles();

        return;
    }

    Msg("\x1B[94m[Apakr]: \x1B[97mData pack encountered an error while uploading! Retrying in 5 seconds.\n");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    if (!CApakrPlugin::Singleton->UploadDataPack(UploadURL, FilePath, PreviousPacks))
        return FailedDataPackUpload_Thread(UploadURL, CurrentFile, FilePath, PreviousPacks, Attempt + 1);

    CApakrPlugin::Singleton->SetupDL(FilePath, CurrentFile);

    CApakrPlugin::Singleton->Packing = false;
}

void BuildAndWriteDataPack_Thread(const std::string &ClonePath, const std::string &UploadURL)
{
    if (!CApakrPlugin::Singleton->Packing)
        return;

    std::string FilePath = "data/apakr/";
    FileFindHandle_t FindHandle = NULL;
    auto [PackKey, NeededSize] = CApakrPlugin::Singleton->GetDataPackInfo();
    std::string CurrentFile = FilePath;
    CUtlBuffer *FileContents = new CUtlBuffer();
    std::vector<std::string> PreviousPacks;
    std::string OutputBuffer;
    Bootil::_AutoBuffer EncryptedDataPack;
    Bootil::_AutoBuffer DataPack(NeededSize);

    CApakrPlugin::Singleton->CurrentPackKey = PackKey;
    CApakrPlugin::Singleton->PackedFiles = 0;

    for (auto &[_, PackEntry] : CApakrPlugin::Singleton->DataPackMap)
    {
        std::string SaltedPath =
            GModDataPackProxy::Singleton.GetHexSHA256(CApakrPlugin::Singleton->CurrentPackKey + PackEntry.FilePath)
                .substr(0, 15);

        std::string HexSize = PaddedHex(PackEntry.OriginalSize, 6);

        DataPack.Write(SaltedPath.data(), 15);
        DataPack.Write(HexSize.data(), 6);
        DataPack.Write(PackEntry.OriginalContents.data(), PackEntry.OriginalSize);

        OutputBuffer += SaltedPath + HexSize + PackEntry.OriginalContents;
        CApakrPlugin::Singleton->PackedFiles++;
    }

    CurrentFile.append(CApakrPlugin::Singleton->CurrentPackName).append(".bsp.bz2");

    CApakrPlugin::Singleton->CurrentPackSHA256 =
        GModDataPackProxy::Singleton.GetHexSHA256((char *)DataPack.GetBase(), DataPack.GetSize());

    CApakrPlugin::Singleton->CurrentPackName =
        GModDataPackProxy::Singleton
            .GetHexSHA256(CApakrPlugin::Singleton->CurrentPackSHA256 + CApakrPlugin::Singleton->CurrentPackKey)
            .substr(0, 32);

    std::string CurrentPath = FilePath + CApakrPlugin::Singleton->CurrentPackName + ".bsp.bz2";

    if (g_pFullFileSystem->FileExists(CurrentPath.c_str(), "GAME"))
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mData pack is the same, skipping repack!\n");

        if (!ClonePath.empty())
            CloneFile(CurrentPath, ClonePath + CurrentPath);
        else
        {
            if (!UploadURL.empty())
                CApakrPlugin::Singleton->FailedUpload =
                    !CApakrPlugin::Singleton->UploadDataPack(UploadURL, CurrentPath, PreviousPacks);
            else
                CApakrPlugin::Singleton->FailedUpload = false;

            if (CApakrPlugin::Singleton->FailedUpload)
                return std::thread(FailedDataPackUpload_Thread, UploadURL, CurrentFile, CurrentPath, PreviousPacks, 1)
                    .detach();
        }

        CApakrPlugin::Singleton->SetupDL(CurrentPath, CurrentFile);

        CApakrPlugin::Singleton->Packing = false;
        CApakrPlugin::Singleton->PackReady = true;

        return;
    }

    std::vector<uint8_t> CompressedData =
        GModDataPackProxy::Singleton.Compress((uint8_t *)DataPack.GetBase(), DataPack.GetSize());

    Bootil::_AutoBuffer CompressedDataPack((int)CompressedData.size());

    CompressedDataPack.Write(CompressedData.data(), (uint)CompressedData.size());
    Apakr_Encrypt(CompressedDataPack, EncryptedDataPack, CApakrPlugin::Singleton->CurrentPackKey);

    std::vector<uint8_t> BZ2Data =
        GModDataPackProxy::Singleton.BZ2((uint8_t *)EncryptedDataPack.GetBase(), EncryptedDataPack.GetSize());

    FileContents->Put(BZ2Data.data(), (int)BZ2Data.size());

    const char *FileName = g_pFullFileSystem->FindFirst("data/apakr/*", &FindHandle);

    while (FileName)
    {
        std::string ExistingPath = FilePath + FileName;

        PreviousPacks.push_back(FileName);
        g_pFullFileSystem->RemoveFile(ExistingPath.c_str(), "GAME");

        if (!ClonePath.empty())
            try
            {
                std::filesystem::remove(ClonePath + ExistingPath);
            }
            catch (const std::filesystem::filesystem_error &_)
            {
            }

        FileName = g_pFullFileSystem->FindNext(FindHandle);
    }

    g_pFullFileSystem->FindClose(FindHandle);
    g_pFullFileSystem->CreateDirHierarchy(FilePath.c_str(), "GAME");
    FilePath.append(CApakrPlugin::Singleton->CurrentPackName).append(".bsp.bz2");
    g_pFullFileSystem->WriteFile(FilePath.c_str(), "GAME", *FileContents);

    FileHandle_t Handle = g_pFullFileSystem->Open(FilePath.c_str(), "rb", "GAME");

    CApakrPlugin::Singleton->PackedSize = g_pFullFileSystem->Size(Handle);

    g_pFullFileSystem->Close(Handle);

    Msg("\x1B[94m[Apakr]: \x1B[97mData pack ready! We've packed \x1B[93m%d \x1B[97mfiles (\x1B[93m%s \x1B[97m-> "
        "\x1B[93m%s \x1B[97m[\x1B[95m%0.2f%%\x1B[97m]) in \x1B[93m%0.2f \x1B[97mseconds.\n",
        CApakrPlugin::Singleton->PackedFiles, HumanSize(CApakrPlugin::Singleton->UnpackedSize).c_str(),
        HumanSize(CApakrPlugin::Singleton->PackedSize).c_str(),
        PercentageDifference(CApakrPlugin::Singleton->UnpackedSize, CApakrPlugin::Singleton->PackedSize),
        TimeSince<std::chrono::milliseconds>(CApakrPlugin::Singleton->LastRepack).count() / 1000.0f);

    if (!ClonePath.empty())
        CloneFile(FilePath, ClonePath + FilePath);
    else
    {
        if (!UploadURL.empty())
            CApakrPlugin::Singleton->FailedUpload =
                !CApakrPlugin::Singleton->UploadDataPack(UploadURL, FilePath, PreviousPacks);
        else
            CApakrPlugin::Singleton->FailedUpload = false;

        if (CApakrPlugin::Singleton->FailedUpload)
            return std::thread(FailedDataPackUpload_Thread, UploadURL, CurrentFile, FilePath, PreviousPacks, 1)
                .detach();
    }

    CApakrPlugin::Singleton->SetupDL(FilePath, CurrentFile);

    CApakrPlugin::Singleton->Packing = false;
    CApakrPlugin::Singleton->PackReady = true;

    delete FileContents;
}

void CApakrPlugin::BuildAndWriteDataPack()
{
    if (this->Packing)
        return;

    std::string ClonePath = GetConvarString(apakr_clone_directory);
    std::string UploadURL = GetConvarString(apakr_upload_url);

    this->Packing = true;
    this->PackReady = false;

    std::thread(BuildAndWriteDataPack_Thread, ClonePath, UploadURL).detach();
}

void CApakrPlugin::SetupDL(const std::string &FilePath, const std::string &PreviousPath)
{
    std::string TrimmedPreviousPath = PreviousPath;
    std::string TrimmedFilePath = FilePath;

    if (PreviousPath.find("bz2") != std::string::npos)
        TrimmedPreviousPath = PreviousPath.substr(0, PreviousPath.size() - 4);

    if (FilePath.find("bz2") != std::string::npos)
        TrimmedFilePath = FilePath.substr(0, FilePath.size() - 4);

    if (g_pDownloadables->FindStringIndex(TrimmedPreviousPath.c_str()) != INVALID_STRING_INDEX)
    {
        g_pDownloadables->m_pItems->m_Items.Remove(TrimmedPreviousPath.c_str());

        Msg("\x1B[94m[Apakr]: \x1B[97mRemoved \x1B[96mprevious \x1B[97mdata pack \x1B[96m%s \x1B[97mfrom "
            "\x1B[93mFastDL\x1B[97m.\n",
            TrimmedPreviousPath.c_str());
    }

    if (g_pDownloadables->FindStringIndex(TrimmedFilePath.c_str()) == INVALID_STRING_INDEX)
    {
        g_pDownloadables->AddString(true, TrimmedFilePath.c_str(), (int)TrimmedFilePath.size());

        Msg("\x1B[94m[Apakr]: \x1B[97mServing data pack \x1B[96m%s \x1B[97mvia \x1B[93mFastDL\x1B[97m.\n",
            TrimmedFilePath.c_str());
    }
}

void CApakrPlugin::LoadPreprocessorTemplates()
{
    this->LastTemplateUpdate = std::chrono::system_clock::now();

    char FullPath[MAX_PATH];

    if (!g_pFullFileSystem->RelativePathToFullPath_safe("apakr.templates", nullptr, FullPath))
        return;

    if (!std::filesystem::exists(FullPath))
        return;

    if (this->TemplatePath != "" && this->LastTemplateEdit == std::filesystem::last_write_time(this->TemplatePath))
        return;

    Msg("\x1B[94m[Apakr]: \x1B[97mLoading preprocessor templates.\n");
    this->PreprocessorTemplates.clear();

    std::ifstream File(FullPath);
    nlohmann::json Object;

    try
    {
        File >> Object;

        for (const nlohmann::json &Rule : Object)
            this->PreprocessorTemplates.push_back({Rule["Pattern"], Rule["Replacement"]});
    }
    catch (const nlohmann::json::parse_error &_)
    {
        Msg("\x1B[94m[Apakr]: \x1b[97mapakr_templates.cfg is invalid.\n");
    }

    this->TemplatePath = FullPath;
    this->LastTemplateEdit = std::filesystem::last_write_time(FullPath);
}

bool GModDataPackProxy::Load()
{
    this->AddOrUpdateFile_Original = FunctionPointers::GModDataPack_AddOrUpdateFile();

    if (!this->AddOrUpdateFile_Original)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to allocate hook for \x1B[91mGModDataPack::AddOrUpdateFile\x1B[97m.\n");

        return false;
    }

    if (!Hook(this->AddOrUpdateFile_Original, &GModDataPackProxy::AddOrUpdateFile))
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to hook for \x1B[91mGModDataPack::AddOrUpdateFile\x1B[97m.\n");

        return false;
    }

    this->SendFileToClient_Original = FunctionPointers::GModDataPack_SendFileToClient();

    if (!this->SendFileToClient_Original)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to allocate hook for \x1B[91mGModDataPack::SendFileToClient\x1B[97m.\n");

        return false;
    }

    if (!Hook(this->SendFileToClient_Original, &GModDataPackProxy::SendFileToClient))
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to hook for \x1B[91mGModDataPack::SendFileToClient\x1B[97m.\n");

        return false;
    }

    return true;
}

void GModDataPackProxy::Unload()
{
    UnHook(this->AddOrUpdateFile_Original);
    UnHook(this->SendFileToClient_Original);
}

void GModDataPackProxy::AddOrUpdateFile(const GmodDataPackFile *FileContents, bool Refresh)
{
    GModDataPackProxy &Self = this->Singleton;
    std::string FileName = FileContents->name;

    CApakrPlugin::Singleton->LastRepack = std::chrono::system_clock::now();
    CApakrPlugin::Singleton->NeedsRepack = true;

    FileHandle_t Handle = g_pFullFileSystem->Open(FileName.c_str(), "rb", "GAME");
    int FileSize = g_pFullFileSystem->Size(Handle);

    g_pFullFileSystem->Close(Handle);

    CApakrPlugin::Singleton->FileMap[FileContents->name] = FileEntry(FileContents->contents, FileSize);

    ReplaceAll(CApakrPlugin::Singleton->FileMap[FileContents->name].Contents, "\r", "");
    GModDataPackProxy::Singleton.ProcessFile(CApakrPlugin::Singleton->FileMap[FileContents->name].Contents);

    if (Refresh)
        Msg("\x1B[94m[Apakr]: \x1B[97mAutorefresh: \x1B[93m%s\x1B[97m. Rebuilding data pack...\n", FileName.c_str());

    Call(Self.AddOrUpdateFile_Original, (LuaFile *)FileContents, Refresh);
}

_32CharArray GModDataPackProxy::GetSHA256(const char *Data, size_t Length)
{
    CSha256 Context;
    _32CharArray SHA256;

    Sha256_Init(&Context);
    Sha256_Update(&Context, (const uint8_t *)Data, Length);
    Sha256_Final(&Context, (uint8_t *)SHA256.data());

    return SHA256;
}

std::string GModDataPackProxy::GetHexSHA256(const char *Data, size_t Length)
{
    return this->SHA256ToHex(this->GetSHA256(Data, Length));
}

std::string GModDataPackProxy::GetHexSHA256(const std::string &Data)
{
    return this->SHA256ToHex(this->GetSHA256(Data.data(), Data.size()));
}

std::string GModDataPackProxy::SHA256ToHex(const _32CharArray &SHA256)
{
    std::ostringstream Stream;

    Stream << std::hex << std::setfill('0');

    for (const char &Char : SHA256)
        Stream << std::setw(2) << ((unsigned)Char & 0xFF);

    return Stream.str();
}

std::vector<uint8_t> GModDataPackProxy::Compress(uint8_t *Input, int Size)
{
    size_t PropsSize = LZMA_PROPS_SIZE;
    size_t DestinationSize = Size + Size / 3 + 128;
    std::vector<uint8_t> Output(DestinationSize + PropsSize + 8, 0);
    uint8_t *PropStart = Output.data();
    uint8_t *SizeStart = PropStart + PropsSize;
    uint8_t *BodyStart = SizeStart + 8;

    if (LzmaCompress(BodyStart, &DestinationSize, Input, Size, PropStart, &PropsSize, 5, 65536, 3, 0, 2, 32, 2) !=
            SZ_OK ||
        PropsSize != LZMA_PROPS_SIZE)
        return {};

    SizeStart[0] = Size & 0xFF;
    SizeStart[1] = (Size >> 8) & 0xFF;
    SizeStart[2] = (Size >> 16) & 0xFF;
    SizeStart[3] = (Size >> 24) & 0xFF;
    SizeStart[4] = 0;
    SizeStart[5] = 0;
    SizeStart[6] = 0;
    SizeStart[7] = 0;

    Output.resize(DestinationSize + PropsSize + 8);

    return Output;
}

std::string GModDataPackProxy::Decompress(const uint8_t *Input, int Size)
{
    if (Size <= LZMA_PROPS_SIZE + 8)
        return "";

    const uint8_t *PropsBuffer = (uint8_t *)Input;
    const uint8_t *SizeBuffer = PropsBuffer + LZMA_PROPS_SIZE;
    const uint8_t *DataBuffer = SizeBuffer + 8;

    if (PropsBuffer[0] >= (9 * 5 * 5))
        return "";

    unsigned int DictionarySize = PropsBuffer[1] | ((unsigned int)PropsBuffer[2] << 8) |
                                  ((unsigned int)PropsBuffer[3] << 16) | ((unsigned int)PropsBuffer[4] << 24);

    if (DictionarySize < (1 << 12))
        return "";

    size_t DestinationLength = SizeBuffer[0] | (SizeBuffer[1] << 8) | (SizeBuffer[2] << 16) | (SizeBuffer[3] << 24);
    size_t RealDestinationLength = DestinationLength;
    size_t SourceLength = Size - LZMA_PROPS_SIZE - 8;
    std::vector<uint8_t> Output(RealDestinationLength, 0);

    if (LzmaUncompress((uint8_t *)Output.data(), &DestinationLength, DataBuffer, &SourceLength, PropsBuffer,
                       LZMA_PROPS_SIZE) != SZ_OK)
        return "";

    return std::string((char *)Output.data());
}

std::vector<uint8_t> GModDataPackProxy::Compress(const std::string &Input)
{
    return this->Compress((uint8_t *)Input.data(), (int)Input.size() + 1);
}

std::vector<uint8_t> GModDataPackProxy::BZ2(const uint8_t *Input, int Size)
{
    unsigned int CompressedSize = Size + (Size / 100) + 600;
    std::vector<uint8_t> Output(CompressedSize);

    if (BZ2_bzBuffToBuffCompress((char *)Output.data(), &CompressedSize, (char *)Input, Size, BZ2_DEFAULT_BLOCKSIZE100k,
                                 0, BZ2_DEFAULT_WORKFACTOR) != BZ_OK)
        return {};

    Output.resize(CompressedSize);

    return Output;
}

void GModDataPackProxy::ProcessFile(std::string &Code)
{
    for (size_t Index = 0; Index < CApakrPlugin::Singleton->PreprocessorTemplates.size(); ++Index)
    {
        Template &Rule = CApakrPlugin::Singleton->PreprocessorTemplates[Index];

        try
        {
            std::regex Regex(Rule.Pattern);
            std::smatch Matches;

            while (std::regex_search(Code, Matches, Regex))
                Code = std::regex_replace(Code, Regex, Rule.Replacement);
        }
        catch (const std::regex_error &_)
        {
        }
    }
}

void GModDataPackProxy::SendDataPackFile(int Client, int FileID)
{
    if (FileID < 1 || FileID > g_pClientLuaFiles->GetNumStrings())
        return;

    DataPackEntry &Entry = CApakrPlugin::Singleton->DataPackMap[FileID];
    const std::vector<uint8_t> &CompressedBuffer = Entry.CompressedContents;

    if (CompressedBuffer.empty())
        return;

    int BufferSize = 1 + 32 + (int)CompressedBuffer.size() + 4 + 2;

    std::vector<uint8_t> Buffer(BufferSize, 0);
    bf_write Writer("Apakr SendDataPackFile Buffer", Buffer.data(), (int)Buffer.size());

    Writer.WriteByte(4);
    Writer.WriteWord(FileID);
    Writer.WriteBytes(Entry.SHA256.data(), (int)Entry.SHA256.size());
    Writer.WriteBytes(CompressedBuffer.data(), (int)CompressedBuffer.size());
    g_pVEngineServer->GMOD_SendToClient(Client, Writer.GetData(), Writer.GetNumBitsWritten());
}

void GModDataPackProxy::SendFileToClient(int Client, int FileID)
{
    if (CApakrPlugin::Singleton->Disabled)
        return Call(this->Singleton.SendFileToClient_Original, Client, FileID);

    this->SendDataPackFile(Client, FileID);
}

bool IVEngineServerProxy::Load()
{
    SourceSDK::FactoryLoader EngineFactoryLoader("engine");

    if (!EngineFactoryLoader.IsValid())
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mengine\x1B[97m FactoryLoader!\n");

        return false;
    }

    this->GMOD_SendFileToClient_Original =
        ResolveSymbols<IVEngineServer_GMOD_SendFileToClient_t>(EngineFactoryLoader, IVEngineServer_GMOD_SendToClient);

    if (!this->GMOD_SendFileToClient_Original)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to allocate hook for "
            "\x1B[91mIVEngineServer::GMOD_SendFileToClient\x1B[97m.\n");

        return false;
    }

    if (!Hook(this->GMOD_SendFileToClient_Original, &IVEngineServerProxy::GMOD_SendFileToClient))
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to hook for "
            "\x1B[91mIVEngineServerProxy::GMOD_SendFileToClient\x1B[97m.\n");

        return false;
    }

    return true;
}

void IVEngineServerProxy::Unload()
{
    UnHook(this->GMOD_SendFileToClient_Original);
}

void IVEngineServerProxy::GMOD_SendFileToClient(IRecipientFilter *Filter, void *BF_Data, int BF_Size)
{
    IVEngineServerProxy &Self = this->Singleton;
    bf_read Buffer(BF_Data, BF_Size);
    int Type = Buffer.ReadByte();

    if (Type != 1)
        return Call(Self.GMOD_SendFileToClient_Original, Filter, BF_Data, BF_Size);

    std::string FilePath = Buffer.ReadAndAllocateString();
    unsigned int Length = Buffer.ReadLong();

    if (Length <= 32)
        return Call(Self.GMOD_SendFileToClient_Original, Filter, BF_Data, BF_Size);

    Length = Length - 32;

    std::vector<uint8_t> CompressedContents(Length, 0);

    Buffer.SeekRelative(32 << 3);
    Buffer.ReadBytes((void *)CompressedContents.data(), Length);

    std::string Contents = GModDataPackProxy::Singleton.Decompress(CompressedContents.data(), Length);

    if (Contents == "")
        return Call(Self.GMOD_SendFileToClient_Original, Filter, BF_Data, BF_Size);

    ReplaceAll(Contents, "\r", "");
    GModDataPackProxy::Singleton.ProcessFile(Contents);

    std::vector<uint8_t> FileBuffer = GModDataPackProxy::Singleton.Compress(Contents);
    _32CharArray SHA256 = GModDataPackProxy::Singleton.GetSHA256(Contents.data(), Contents.length() + 1);
    void *BF_Data_New = malloc(65536);
    bf_write Writer(BF_Data_New, 65536);

    Writer.WriteByte(1);
    Writer.WriteString(FilePath.data());
    Writer.WriteLong((int)SHA256.size() + (int)FileBuffer.size());
    Writer.WriteBytes(SHA256.data(), (int)SHA256.size());
    Writer.WriteBytes(FileBuffer.data(), (int)FileBuffer.size());

    if (!Writer.IsOverflowed())
        Call(Self.GMOD_SendFileToClient_Original, Filter, BF_Data_New, Writer.GetNumBitsWritten());

    free(BF_Data_New);
}

extern "C"
{
    void *CreateInterface(const char *Name, int *ReturnCode)
    {
        return CApakrPlugin::Singleton;
    }
}