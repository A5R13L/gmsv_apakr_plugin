#include <apakr/plugin/plugin.h>

static CApakrPlugin *INSTANCE = new CApakrPlugin();
GModDataPackProxy GModDataPackProxy::Singleton;

ConVar *apakr_file = nullptr;
ConVar *apakr_sha256 = nullptr;
ConVar *apakr_key = nullptr;
ConVar *apakr_clone_directory = nullptr;
ConVar *apakr_upload_url = nullptr;
ConVar *sv_downloadurl = nullptr;
bool DownloadURLChanged = false;
bool LuaValue = false;
std::string CurrentDownloadURL;

void OnCloneDirectoryChanged(ConVar *_this, const char *OldString, float OldFloat)
{
    if (!INSTANCE->Ready || INSTANCE->CurrentPackName == "")
        return;

    Msg("\x1B[94m[Apakr]: \x1B[97aapakr_clone_directory \x1B[97mwas changed. Issuing repack.\n");

    INSTANCE->NeedsRepack = true;
}

void OnUploadURLChanged(ConVar *_this, const char *OldString, float OldFloat)
{
    if (!INSTANCE->Ready || INSTANCE->CurrentPackName == "")
        return;

    Msg("\x1B[94m[Apakr]: \x1B[97aapakr_upload_url \x1B[97mwas changed. Issuing repack.\n");

    INSTANCE->NeedsRepack = true;
}

void OnDownloadURLChanged(ConVar *_this, const char *OldString, float OldFloat)
{
    if (DownloadURLChanged || !INSTANCE->Ready || INSTANCE->CurrentPackName == "")
        return;

    Msg("\x1B[94m[Apakr]: \x1B[97asv_downloadurl \x1B[97mwas changed. Issuing repack.\n");

#if defined(APAKR_32_SERVER)
    CurrentDownloadURL = sv_downloadurl->GetString();
#else
    CurrentDownloadURL = sv_downloadurl->Get<const char *>();
#endif

    INSTANCE->NeedsRepack = true;
}

DataPackEntry::DataPackEntry(std::string _Path, std::string _String, std::string _Original)
{
    this->Path = _Path;
    this->String = ReplaceAll(_String, "\r", "");
    this->Size = this->String.size() + 1;
    this->OriginalString = _Original;
    this->OriginalSize = this->OriginalString.size() + 1;
    this->SHA256 = GModDataPackProxy::Singleton.GetSHA256(this->String.data(), this->Size);
    this->Compressed = GModDataPackProxy::Singleton.Compress(this->String);
}

FileEntry::FileEntry(std::string _Contents, int _Size)
{
    this->Contents = _Contents;
    this->Size = _Size;
}

bool CApakrPlugin::Load(CreateInterfaceFn InterfaceFactory, CreateInterfaceFn GameServerFactory)
{
    Msg("\x1B[94m[Apakr]: \x1B[97mLoading...\n");

    g_pFullFileSystem = InterfacePointers::Internal::Server::FileSystem();

    if (!g_pFullFileSystem)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mIFileSystem\x1B[97m!\n");

        return false;
    }

    g_pNetworkStringTableContainer =
        (CNetworkStringTableContainer *)InterfacePointers::Internal::Server::NetworkStringTableContainer();

    if (!g_pNetworkStringTableContainer)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mINetworkStringTableContainer\x1B[97m!\n");

        return false;
    }

    g_pServer = InterfacePointers::Server();

    if (!g_pServer)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mIServer\x1B[97m!\n");

        return false;
    }

    g_pVEngineServer = InterfacePointers::VEngineServer();

    if (!g_pVEngineServer)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mIVEngineServer\x1B[97m!\n");

        return false;
    }

    g_pCVar = InterfacePointers::Cvar();

    if (!g_pCVar)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mICVar\x1B[97m!\n");

        return false;
    }

    SourceSDK::FactoryLoader LuaSharedFactoryLoader("lua_shared");

    if (!LuaSharedFactoryLoader.IsValid())
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mlua_shared\x1B[97m FactoryLoader!\n");

        return false;
    }

    g_pILuaShared = LuaSharedFactoryLoader.GetInterface<GarrysMod::Lua::ILuaShared>(GMOD_LUASHARED_INTERFACE);

    if (!g_pILuaShared)
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to get \x1B[91mILuaShared\x1B[97m!\n");

        return false;
    }

    ConVar_Register();

    apakr_file = new ConVar("apakr_file", "", FCVAR_GAMEDLL | FCVAR_REPLICATED | FCVAR_LUA_SERVER);
    apakr_sha256 = new ConVar("apakr_sha256", "", FCVAR_GAMEDLL | FCVAR_REPLICATED | FCVAR_LUA_SERVER);
    apakr_key = new ConVar("apakr_key", "", FCVAR_GAMEDLL | FCVAR_REPLICATED | FCVAR_LUA_SERVER);

    apakr_clone_directory =
        new ConVar("apakr_clone_directory", "", FCVAR_GAMEDLL | FCVAR_LUA_SERVER,
                   "Where to clone the data packs for FastDL.", (FnChangeCallback_t)OnCloneDirectoryChanged);

    apakr_upload_url = new ConVar("apakr_upload_url", "https://apakr.asriel.dev/", FCVAR_GAMEDLL | FCVAR_LUA_SERVER,
                                  "Custom self-hosting url.", (FnChangeCallback_t)OnUploadURLChanged);

    sv_downloadurl = g_pCVar->FindVar("sv_downloadurl");

#if defined(APAKR_32_SERVER)
    sv_downloadurl->InstallChangeCallback((FnChangeCallback_t)OnDownloadURLChanged);

    CurrentDownloadURL = sv_downloadurl->GetString();
#else
    sv_downloadurl->InstallChangeCallback((FnChangeCallback_t)OnDownloadURLChanged, false);

    CurrentDownloadURL = sv_downloadurl->Get<const char *>();
#endif

    return GModDataPackProxy::Singleton.Load();
}

void CApakrPlugin::Unload()
{
    Msg("\x1B[94m[Apakr]: \x1B[97mUnloading...\n");

#if defined(APAKR_32_SERVER)
    ((HackedConVar *)sv_downloadurl)->m_fnChangeCallback = nullptr;
    ((HackedConVar *)apakr_clone_directory)->m_fnChangeCallback = nullptr;
    ((HackedConVar *)apakr_upload_url)->m_fnChangeCallback = nullptr;
#else
    sv_downloadurl->RemoveChangeCallback((FnChangeCallback_t)OnDownloadURLChanged);
    apakr_clone_directory->RemoveChangeCallback((FnChangeCallback_t)OnCloneDirectoryChanged);
    apakr_upload_url->RemoveChangeCallback((FnChangeCallback_t)OnUploadURLChanged);
#endif

    if (g_pLUAServer)
    {
        g_pLUAServer->PushNil();
        g_pLUAServer->SetField(GarrysMod::Lua::INDEX_GLOBAL, "APakr");
    }

    ConVar_Unregister();
    GModDataPackProxy::Singleton.Unload();
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
    this->CheckForRepack();

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
            if (Player && Player->Client && Player->LoadingIn)
                Player->Client->Reconnect();
}

void CApakrPlugin::ClientActive(edict_t *Entity)
{
    int Index = Entity->m_EdictIndex - 1;

    if (!this->Players[Index])
        return;

    this->Players[Index]->LoadingIn = false;
}

void CApakrPlugin::ClientDisconnect(edict_t *Entity)
{
    int Index = Entity->m_EdictIndex - 1;

    if (!this->Players[Index])
        return;

    this->Players[Index]->Client = nullptr;
}

PLUGIN_RESULT CApakrPlugin::ClientConnect(bool *AllowConnection, edict_t *Entity, const char *Name, const char *Address,
                                          char *Rejection, int MaxRejectionLength)
{
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

Bootil::AutoBuffer CApakrPlugin::GetDataPackBuffer()
{
    int NeededSize = 0;
    std::vector<std::pair<std::string, std::string>> FileList;

    for (auto &Pair : this->DataPackMap)
    {
        NeededSize += 15;
        NeededSize += 6;
        NeededSize += Pair.second.OriginalSize;

        FileList.push_back({Pair.second.Path, GModDataPackProxy::Singleton.SHA256ToHex(Pair.second.SHA256)});
    }

    std::sort(FileList.begin(), FileList.end(),
              [](std::pair<std::string, std::string> &First, std::pair<std::string, std::string> &Second) {
                  return First.first < Second.first;
              });

    std::string SHABuffer = "";

    for (auto &Pair : FileList)
        SHABuffer += Pair.first + ":" + Pair.second;

    Bootil::AutoBuffer DataPack(NeededSize);

    this->CurrentPackKey = GModDataPackProxy::Singleton.GetHexSHA256(SHABuffer);
    this->PackedFiles = 0;

    for (auto &Pair : this->DataPackMap)
    {
        std::string SaltedPath =
            GModDataPackProxy::Singleton.GetHexSHA256(this->CurrentPackKey + Pair.second.Path).substr(0, 15);

        std::string HexSize = PaddedHex(Pair.second.OriginalSize, 6);

        DataPack.Write(SaltedPath.data(), 15);
        DataPack.Write(HexSize.data(), 6);
        DataPack.Write(Pair.second.OriginalString.data(), Pair.second.OriginalSize);

        this->PackedFiles++;
    }

    return DataPack;
}

void CApakrPlugin::SetupClientFiles()
{
    if (this->Disabled && this->WasDisabled)
        return;

    for (int Index = 1; Index < g_pClientLuaFiles->GetNumStrings(); ++Index)
    {
        const char *Path = g_pClientLuaFiles->GetString(Index);
        FileEntry &FileMapEntry = this->FileMap[Path];
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
                FileMapEntry.SHA256 = new char[32];
                const char *Data = (const char *)g_pClientLuaFiles->GetStringUserData(Index, nullptr);

                for (int ByteIndex = 0; ByteIndex < 32; ++ByteIndex)
                    FileMapEntry.SHA256[ByteIndex] = Data[ByteIndex];
            }

            if (!DataPackMapEntry.Compressed.empty())
            {

                DataPackMapEntry.OriginalString = FileMapEntry.Contents;
                DataPackMapEntry.OriginalSize = DataPackMapEntry.OriginalString.size() + 1;

                continue;
            }

            std::string DataPackContent;

            if (strstr(Path, "lua/includes/init.lua"))
            {
                std::string Shellcode = APAKR_SHELLCODE;

                DataPackContent = ReplaceAll(Shellcode, "{APAKR_DECRYPTION_FUNCTION}", APAKR_DECRYPTION_FUNCTION);

                DataPackContent.append("\n\n").append(FileMapEntry.Contents);
            }
            else
                DataPackContent = "return APakr()()";

            DataPackMapEntry = this->DataPackMap[Index] = DataPackEntry(Path, DataPackContent, FileMapEntry.Contents);

            g_pClientLuaFiles->SetStringUserData(Index, 32, DataPackMapEntry.SHA256.data());
        }
    }
}

int BarSize = 40;
double LastPercentage = -1;

void DisplayProgressBar(double Percentage)
{
    int Progress = BarSize * Percentage;

    if (LastPercentage == Progress)
        return;

    LastPercentage = Progress;

    Msg("\x1B[94m[Apakr]: \x1b[97m[\x1B[91m");

    for (int Index = 0; Index < BarSize; ++Index)
        if (Index < Progress)
            Msg("=");
        else
            Msg(" ");

    Msg("\x1b[97m] %.2f%%\n", Percentage * 100);
}

int ProgressCallback(void *, curl_off_t, curl_off_t, curl_off_t TotalToUpload, curl_off_t CurrentUpload)
{
    if (TotalToUpload > 0)
        DisplayProgressBar(CurrentUpload / TotalToUpload);

    return 0;
}

size_t WriteCallback(void *Pointer, size_t Size, size_t Bytes, void *Stream)
{
    return fwrite(Pointer, Size, Bytes, (FILE *)Stream);
}

size_t WriteEmptyCallback(void *Pointer, size_t Size, size_t Bytes, void *Stream)
{
    return Size * Bytes;
}

size_t HeaderCallback(char *Buffer, size_t Size, size_t Items, std::map<std::string, std::string> *Headers)
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

bool CApakrPlugin::UploadDataPack(std::string &UploadURL, std::string &Pack, std::vector<std::string> &PreviousPacks)
{
    Msg("\x1B[94m[Apakr]: \x1b[97mUploading data pack...\n");
    Msg("\x1B[94m[Apakr]: \x1b[97mURL: %s\n", UploadURL.c_str());

    char FullPath[MAX_PATH];

    if (!g_pFullFileSystem->RelativePathToFullPath_safe(Pack.c_str(), "GAME", FullPath))
    {
        Msg("\x1B[94m[Apakr]: \x1b[97Failed to get full file path while uploading.\n");

        return false;
    }

    CURL *Handle = curl_easy_init();
    std::string ServerIP = g_pVEngineServer->GMOD_GetServerAddress();

    ServerIP = ServerIP.substr(0, ServerIP.find(":"));

    if (Handle)
    {
        std::string AuthorizationHeader = "Authorization: ";
        std::string DownloadURLHeader = "X-Download-URL: ";
        curl_slist *Headers = nullptr;
        FILE *HTTPUploadLog = nullptr;
        char ErrorBuffer[CURL_ERROR_SIZE];
        curl_mime *Form = curl_mime_init(Handle);
        curl_mimepart *Field = curl_mime_addpart(Form);
        long HTTPCode;
        CURLcode ResponseCode;
        std::map<std::string, std::string> ResponseHeaders;

        LastPercentage = -1;
        AuthorizationHeader += GModDataPackProxy::Singleton.GetHexSHA256(ServerIP);
        DownloadURLHeader += CurrentDownloadURL;
        Headers = curl_slist_append(Headers, AuthorizationHeader.c_str());
        Headers = curl_slist_append(Headers, DownloadURLHeader.c_str());
        Headers = curl_slist_append(Headers, "User-Agent: apakr_server");

        curl_mime_name(Field, "file");
        curl_mime_filename(Field, Pack.c_str());
        curl_mime_filedata(Field, FullPath);

        std::string DeletionRequest = "[";

        for (std::string &File : PreviousPacks)
        {
            if (DeletionRequest != "[")
                DeletionRequest += ",";

            DeletionRequest += "\"" + File + "\"";
        }

        DeletionRequest += "]";
        Field = curl_mime_addpart(Form);

        curl_mime_name(Field, "delete");
        curl_mime_data(Field, DeletionRequest.c_str(), CURL_ZERO_TERMINATED);

        HTTPUploadLog = fopen("apakr_http_upload.log", "wb");

        curl_easy_setopt(Handle, CURLOPT_URL, UploadURL.c_str());
        curl_easy_setopt(Handle, CURLOPT_HTTPHEADER, Headers);
        curl_easy_setopt(Handle, CURLOPT_MIMEPOST, Form);
        curl_easy_setopt(Handle, CURLOPT_NOPROGRESS, 0);
        curl_easy_setopt(Handle, CURLOPT_ERRORBUFFER, ErrorBuffer);
        curl_easy_setopt(Handle, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(Handle, CURLOPT_WRITEDATA, HTTPUploadLog);
        curl_easy_setopt(Handle, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
        curl_easy_setopt(Handle, CURLOPT_XFERINFODATA, nullptr);
        curl_easy_setopt(Handle, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(Handle, CURLOPT_HEADERDATA, &ResponseHeaders);

        ResponseCode = curl_easy_perform(Handle);

        curl_easy_getinfo(Handle, CURLINFO_HTTP_CODE, &HTTPCode);
        curl_mime_free(Form);
        curl_easy_cleanup(Handle);
        fclose(HTTPUploadLog);

        if (ResponseCode != CURLE_OK)
        {
            Msg("\x1B[94m[Apakr]: \x1b[97mReceived an invalid curl code [%d] while uploading.\n", ResponseCode);

            return false;
        }

        if (HTTPCode != 200)
        {
            Msg("\x1B[94m[Apakr]: \x1b[97mReceived an invalid response code [%ld] while uploading.\n", HTTPCode);

            return false;
        }

        std::string XDownloadURL = ResponseHeaders["x-download-url"];

        if (XDownloadURL.empty())
        {
            Msg("\x1B[94m[Apakr]: \x1b[97mX-Download-URL was not sent in response headers.\n");

            return false;
        }

        Msg("\x1B[94m[Apakr]: \x1b[97mData pack uploaded successfully! Checking if it is reachable...\n");

        DownloadURLChanged = true;

        sv_downloadurl->SetValue(XDownloadURL.c_str());

        DownloadURLChanged = false;

        if (!this->CanDownloadPack(XDownloadURL))
            return false;
    }

    return true;
}

bool CApakrPlugin::CanDownloadPack(std::string DownloadURL)
{
    CURL *Handle = curl_easy_init();

    if (Handle)
    {
        curl_slist *Headers = nullptr;
        char ErrorBuffer[CURL_ERROR_SIZE];
        long HTTPCode;
        CURLcode ResponseCode;

        Headers = curl_slist_append(Headers, "User-Agent: Half-Life 2");
        DownloadURL += "data/apakr/" + this->CurrentPackName + ".bsp.bz2";

        curl_easy_setopt(Handle, CURLOPT_URL, DownloadURL.c_str());
        curl_easy_setopt(Handle, CURLOPT_HTTPHEADER, Headers);
        curl_easy_setopt(Handle, CURLOPT_NOPROGRESS, 1);
        curl_easy_setopt(Handle, CURLOPT_FOLLOWLOCATION, 1);
        curl_easy_setopt(Handle, CURLOPT_WRITEFUNCTION, WriteEmptyCallback);
        curl_easy_setopt(Handle, CURLOPT_ERRORBUFFER, ErrorBuffer);

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

            return false;
        }

        Msg("\x1B[94m[Apakr]: \x1b[97mData pack is reachable!\n");

        return true;
    }

    return false;
}

void AttemptDataPackUpload_Thread(std::string UploadURL, std::string CurrentFile, std::string Path,
                                  std::vector<std::string> PreviousPacks, int Attempt)
{
    if (Attempt == 3)
    {
        INSTANCE->Packing = false;
        INSTANCE->Disabled = true;

        Msg("\x1B[94m[Apakr]: \x1B[97mFailed to upload data pack after 3 tries. Falling back to regular networking.\n");
        g_pFullFileSystem->RemoveFile(Path.c_str(), "GAME");
        INSTANCE->SetupClientFiles();

        return;
    }

    Msg("\x1B[94m[Apakr]: \x1B[97mData pack encountered an error while uploading! Retrying in 5 seconds.\n");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    if (!INSTANCE->UploadDataPack(UploadURL, Path, PreviousPacks))
        return AttemptDataPackUpload_Thread(UploadURL, CurrentFile, Path, PreviousPacks, Attempt + 1);

    Msg("\x1B[94m[Apakr]: \x1B[97mData pack is ready! We've packed \x1B[93m%d \x1B[97mfiles (\x1B[93m%s \x1B[97m-> "
        "\x1B[93m%s \x1B[97m[\x1B[95m%0.2f%%\x1B[97m]) in \x1B[93m%0.2f \x1B[97mseconds.\n",
        INSTANCE->PackedFiles, HumanSize(INSTANCE->UnpackedSize).c_str(), HumanSize(INSTANCE->PackedSize).c_str(),
        PercentageDifference(INSTANCE->UnpackedSize, INSTANCE->PackedSize),
        TimeSince<std::chrono::milliseconds>(INSTANCE->LastRepack).count() / 1000.0f);

    INSTANCE->SetupDL(Path, CurrentFile);

    INSTANCE->Packing = false;
}

void BuildAndWriteDataPack_Thread(std::string ClonePath, std::string UploadURL)
{
    if (!INSTANCE->Packing)
        return;

    std::string Path = "data/apakr/";
    FileFindHandle_t FindHandle = NULL;
    Bootil::AutoBuffer DataPack = INSTANCE->GetDataPackBuffer();
    std::string CurrentFile = Path;
    Bootil::AutoBuffer EncryptedDataPack;
    CUtlBuffer FileContents;
    std::vector<std::string> PreviousPacks;

    CurrentFile.append(INSTANCE->CurrentPackName).append(".bsp.bz2");

    INSTANCE->CurrentPackSHA256 =
        GModDataPackProxy::Singleton.GetHexSHA256((char *)DataPack.GetBase(), DataPack.GetSize());

    INSTANCE->CurrentPackName =
        GModDataPackProxy::Singleton.GetHexSHA256(INSTANCE->CurrentPackSHA256 + INSTANCE->CurrentPackKey).substr(0, 32);

    std::string CurrentPath = Path + INSTANCE->CurrentPackName + ".bsp.bz2";

    if (g_pFullFileSystem->FileExists(CurrentPath.c_str(), "GAME"))
    {
        apakr_file->SetValue(INSTANCE->CurrentPackName.c_str());
        apakr_sha256->SetValue(INSTANCE->CurrentPackSHA256.c_str());
        apakr_key->SetValue(INSTANCE->CurrentPackKey.c_str());
        Msg("\x1B[94m[Apakr]: \x1B[97mData pack is the same, skipping repack!\n");

        if (!ClonePath.empty())
        {
            std::filesystem::path FullPath = ClonePath + CurrentPath;

            try
            {
                std::filesystem::create_directories(FullPath.parent_path());
            }
            catch (std::filesystem::filesystem_error &Error)
            {
            }

            char FullPathBuffer[512];

            if (g_pFullFileSystem->RelativePathToFullPath(CurrentPath.c_str(), "GAME", FullPathBuffer,
                                                          sizeof(FullPathBuffer)))
                try
                {
                    Msg("\x1B[94m[Apakr]: \x1B[97mCloning \x1B[93m%s \x1B[97mto \x1B[93m%s\x1B[97m.\n", FullPathBuffer,
                        FullPath.c_str());

                    std::filesystem::copy_file(FullPathBuffer, FullPath);
                }
                catch (std::filesystem::filesystem_error &Error)
                {
                }
        }
        else
        {
            if (!UploadURL.empty())
                INSTANCE->FailedUpload = !INSTANCE->UploadDataPack(UploadURL, CurrentPath, PreviousPacks);
            else
                INSTANCE->FailedUpload = false;

            if (INSTANCE->FailedUpload)
                return std::thread(AttemptDataPackUpload_Thread, UploadURL, CurrentFile, CurrentPath, PreviousPacks, 1)
                    .detach();
        }

        INSTANCE->SetupDL(CurrentPath, CurrentFile);

        INSTANCE->Packing = false;
        INSTANCE->PackReady = true;

        return;
    }

    std::vector<char> CompressedData =
        GModDataPackProxy::Singleton.Compress((char *)DataPack.GetBase(), DataPack.GetSize());

    Bootil::AutoBuffer CompressedDataPack(CompressedData.size());

    CompressedDataPack.Write(CompressedData.data(), CompressedData.size());
    Apakr_Encrypt(CompressedDataPack, EncryptedDataPack, INSTANCE->CurrentPackKey);

    std::vector<char> BZ2Data =
        GModDataPackProxy::Singleton.BZ2((char *)EncryptedDataPack.GetBase(), EncryptedDataPack.GetSize());

    FileContents.Put(BZ2Data.data(), BZ2Data.size());

    const char *FileName = g_pFullFileSystem->FindFirst("data/apakr/*", &FindHandle);

    while (FileName)
    {
        std::string ExistingPath = Path + FileName;

        PreviousPacks.push_back(FileName);
        g_pFullFileSystem->RemoveFile(ExistingPath.c_str(), "GAME");

        if (!ClonePath.empty())
            try
            {
                std::filesystem::remove(ClonePath + ExistingPath);
            }
            catch (std::filesystem::filesystem_error &Error)
            {
            }

        FileName = g_pFullFileSystem->FindNext(FindHandle);
    }

    g_pFullFileSystem->FindClose(FindHandle);
    g_pFullFileSystem->CreateDirHierarchy(Path.c_str(), "GAME");
    Path.append(INSTANCE->CurrentPackName).append(".bsp.bz2");
    apakr_file->SetValue(INSTANCE->CurrentPackName.c_str());
    apakr_sha256->SetValue(INSTANCE->CurrentPackSHA256.c_str());
    apakr_key->SetValue(INSTANCE->CurrentPackKey.c_str());
    g_pFullFileSystem->WriteFile(Path.c_str(), "GAME", FileContents);

    if (!ClonePath.empty())
    {
        std::filesystem::path FullPath = ClonePath + Path;

        try
        {
            std::filesystem::create_directories(FullPath.parent_path());
        }
        catch (std::filesystem::filesystem_error &Error)
        {
        }

        char FullPathBuffer[512];

        if (g_pFullFileSystem->RelativePathToFullPath(Path.c_str(), "GAME", FullPathBuffer, sizeof(FullPathBuffer)))
            try
            {
                Msg("\x1B[94m[Apakr]: \x1B[97mCloning \x1B[93m%s \x1B[97mto \x1B[93m%s\x1B[97m.\n", FullPathBuffer,
                    FullPath.c_str());

                std::filesystem::copy_file(FullPathBuffer, FullPath);
            }
            catch (std::filesystem::filesystem_error &Error)
            {
            }
    }
    else
    {
        if (!UploadURL.empty())
            INSTANCE->FailedUpload = !INSTANCE->UploadDataPack(UploadURL, Path, PreviousPacks);
        else
            INSTANCE->FailedUpload = false;

        if (INSTANCE->FailedUpload)
            return std::thread(AttemptDataPackUpload_Thread, UploadURL, CurrentFile, Path, PreviousPacks, 1).detach();
    }

    FileHandle_t Handle = g_pFullFileSystem->Open(Path.c_str(), "rb", "GAME");

    INSTANCE->PackedSize = g_pFullFileSystem->Size(Handle);

    g_pFullFileSystem->Close(Handle);

    Msg("\x1B[94m[Apakr]: \x1B[97mData pack is ready! We've packed \x1B[93m%d \x1B[97mfiles (\x1B[93m%s \x1B[97m-> "
        "\x1B[93m%s \x1B[97m[\x1B[95m%0.2f%%\x1B[97m]) in \x1B[93m%0.2f \x1B[97mseconds.\n",
        INSTANCE->PackedFiles, HumanSize(INSTANCE->UnpackedSize).c_str(), HumanSize(INSTANCE->PackedSize).c_str(),
        PercentageDifference(INSTANCE->UnpackedSize, INSTANCE->PackedSize),
        TimeSince<std::chrono::milliseconds>(INSTANCE->LastRepack).count() / 1000.0f);

    INSTANCE->SetupDL(Path, CurrentFile);

    INSTANCE->Packing = false;
    INSTANCE->PackReady = true;
}

void CApakrPlugin::BuildAndWriteDataPack()
{
    if (this->Packing)
        return;

#if defined(APAKR_32_SERVER)
    std::string ClonePath = apakr_clone_directory->GetString();
    std::string UploadURL = apakr_upload_url->GetString();
#else
    std::string ClonePath = apakr_clone_directory->Get<const char *>();
    std::string UploadURL = apakr_upload_url->Get<const char *>();
#endif

    this->Packing = true;
    this->PackReady = false;

    std::thread(BuildAndWriteDataPack_Thread, ClonePath, UploadURL).detach();
}

void CApakrPlugin::SetupDL(std::string Path, std::string PreviousPath)
{
    if (PreviousPath.find("bz2") != std::string::npos)
        PreviousPath = PreviousPath.substr(0, PreviousPath.size() - 4);

    if (Path.find("bz2") != std::string::npos)
        Path = Path.substr(0, Path.size() - 4);

    if (g_pDownloadables->FindStringIndex(PreviousPath.c_str()) != INVALID_STRING_INDEX)
    {
        g_pDownloadables->m_pItems->m_Items.Remove(PreviousPath.c_str());

        Msg("\x1B[94m[Apakr]: \x1B[97mRemoved \x1B[96mprevious \x1B[97mdata pack \x1B[96m%s \x1B[97mfrom "
            "\x1B[93mFastDL\x1B[97m.\n",
            PreviousPath.c_str());
    }

    if (g_pDownloadables->FindStringIndex(Path.c_str()) == INVALID_STRING_INDEX)
    {
        g_pDownloadables->AddString(true, Path.c_str(), Path.size());

        Msg("\x1B[94m[Apakr]: \x1B[97mServing data pack \x1B[96m%s \x1B[97mvia \x1B[93mFastDL\x1B[97m.\n",
            Path.c_str());
    }
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

void GModDataPackProxy::AddOrUpdateFile(GmodDataPackFile *FileContents, bool Refresh)
{
    auto &Self = this->Singleton;

    INSTANCE->LastRepack = std::chrono::system_clock::now();
    INSTANCE->NeedsRepack = true;

    FileHandle_t Handle = g_pFullFileSystem->Open(FileContents->name, "rb", "GAME");
    int FileSize = g_pFullFileSystem->Size(Handle);

    g_pFullFileSystem->Close(Handle);

    INSTANCE->FileMap[FileContents->name] = FileEntry(FileContents->contents, FileSize);

    ReplaceAll(INSTANCE->FileMap[FileContents->name].Contents, "\r", "");

    if (Refresh)
        Msg("\x1B[94m[Apakr]: \x1B[97mAutorefresh: \x1B[93m%s\x1B[97m. Rebuilding data pack...\n", FileContents->name);

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

std::string GModDataPackProxy::GetHexSHA256(std::string Data)
{
    return this->SHA256ToHex(this->GetSHA256(Data.data(), Data.size()));
}

std::string GModDataPackProxy::SHA256ToHex(_32CharArray SHA256)
{
    std::ostringstream Stream;

    Stream << std::hex << std::setfill('0');

    for (auto &Char : SHA256)
        Stream << std::setw(2) << ((unsigned)Char & 0xFF);

    return Stream.str();
}

std::vector<char> GModDataPackProxy::Compress(char *Input, int Size)
{
    size_t InputLength = Size;
    size_t PropsSize = LZMA_PROPS_SIZE;
    size_t DestinationSize = InputLength + InputLength / 3 + 128;
    std::vector<char> Output(DestinationSize + PropsSize + 8, 0);
    uint8_t *InputData = (uint8_t *)Input;
    uint8_t *PropStart = (uint8_t *)Output.data();
    uint8_t *SizeStart = PropStart + PropsSize;
    uint8_t *BodyStart = SizeStart + 8;

    if (LzmaCompress(BodyStart, &DestinationSize, InputData, InputLength, PropStart, &PropsSize, 5, 65536, 3, 0, 2, 32,
                     2) != SZ_OK ||
        PropsSize != LZMA_PROPS_SIZE)
        return {};

    SizeStart[0] = InputLength & 0xFF;
    SizeStart[1] = (InputLength >> 8) & 0xFF;
    SizeStart[2] = (InputLength >> 16) & 0xFF;
    SizeStart[3] = (InputLength >> 24) & 0xFF;
    SizeStart[4] = 0;
    SizeStart[5] = 0;
    SizeStart[6] = 0;
    SizeStart[7] = 0;

    Output.resize(DestinationSize + PropsSize + 8);

    return Output;
}

std::vector<char> GModDataPackProxy::Compress(std::string &Input)
{
    return this->Compress((char *)Input.data(), Input.size() + 1);
}

std::vector<char> GModDataPackProxy::BZ2(char *Input, int Size)
{
    unsigned int CompressedSize = Size + (Size / 100) + 600;
    std::vector<char> Output(CompressedSize);

    if (BZ2_bzBuffToBuffCompress(Output.data(), &CompressedSize, Input, Size, BZ2_DEFAULT_BLOCKSIZE100k, 0,
                                 BZ2_DEFAULT_WORKFACTOR) != BZ_OK)
        return {};

    Output.resize(CompressedSize);

    return Output;
}

void GModDataPackProxy::SendDataPackFile(int Client, int FileID)
{
    if (FileID < 1 || FileID > g_pClientLuaFiles->GetNumStrings())
        return;

    DataPackEntry &Entry = INSTANCE->DataPackMap[FileID];
    const auto &CompressedBuffer = Entry.Compressed;

    if (CompressedBuffer.empty())
        return;

    int BufferSize = 1 + 32 + CompressedBuffer.size() + 4 + 2;

    std::vector<char> Buffer(BufferSize, 0);
    bf_write Writer("Apakr SendDataPackFile Buffer", Buffer.data(), Buffer.size());

    Writer.WriteByte(4);
    Writer.WriteWord(FileID);
    Writer.WriteBytes(Entry.SHA256.data(), Entry.SHA256.size());
    Writer.WriteBytes(CompressedBuffer.data(), CompressedBuffer.size());
    g_pVEngineServer->GMOD_SendToClient(Client, Writer.GetData(), Writer.GetNumBitsWritten());
}

void GModDataPackProxy::SendFileToClient(int Client, int FileID)
{
    if (INSTANCE->Disabled)
        return Call(this->Singleton.SendFileToClient_Original, Client, FileID);

    this->SendDataPackFile(Client, FileID);
}

extern "C"
{
    void *CreateInterface(const char *Name, int *ReturnCode)
    {
        return INSTANCE;
    }
}