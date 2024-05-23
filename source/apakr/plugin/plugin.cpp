#include <apakr/plugin/plugin.h>

static CApakrPlugin *INSTANCE = new CApakrPlugin();
GModDataPackProxy GModDataPackProxy::Singleton;

ConVar *apakr_file = nullptr;
ConVar *apakr_sha256 = nullptr;
ConVar *apakr_key = nullptr;
ConVar *apakr_clone_directory = nullptr;
ConVar *apakr_upload_url = nullptr;
ConVar *sv_downloadurl = nullptr;
std::string CurrentDownloadURL;

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

    ConVar_Register();

    apakr_file = new ConVar("apakr_file", "", FCVAR_GAMEDLL | FCVAR_REPLICATED | FCVAR_LUA_SERVER);
    apakr_sha256 = new ConVar("apakr_sha256", "", FCVAR_GAMEDLL | FCVAR_REPLICATED | FCVAR_LUA_SERVER);
    apakr_key = new ConVar("apakr_key", "", FCVAR_GAMEDLL | FCVAR_REPLICATED | FCVAR_LUA_SERVER);

    apakr_clone_directory = new ConVar("apakr_clone_directory", "", FCVAR_GAMEDLL | FCVAR_LUA_SERVER,
                                       "Where to clone the data packs for FastDL.");

    apakr_upload_url = new ConVar("apakr_upload_url", "https://apakr.asriel.dev/", FCVAR_GAMEDLL | FCVAR_LUA_SERVER,
                                  "Custom self-hosting url.");

    sv_downloadurl = g_pCVar->FindVar("sv_downloadurl");

#if defined(APAKR_32_SERVER)
    CurrentDownloadURL = sv_downloadurl->GetString();
#else
    CurrentDownloadURL = sv_downloadurl->Get<const char *>();
#endif

    return GModDataPackProxy::Singleton.Load();
}

void CApakrPlugin::Unload()
{
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

    if (this->Packing)
        for (GmodPlayer *Player : this->Players)
            if (Player && Player->Client && Player->LoadingIn)
                Player->Client->Reconnect();
}

void CApakrPlugin::ClientActive(edict_t *Entity)
{
    int Index = Entity->m_EdictIndex - 1;

    this->Players[Index]->LoadingIn = false;
}

void CApakrPlugin::ClientDisconnect(edict_t *Entity)
{
    int Index = Entity->m_EdictIndex - 1;

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
    for (int Index = 1; Index < g_pClientLuaFiles->GetNumStrings(); Index++)
    {
        const char *Path = g_pClientLuaFiles->GetString(Index);

        if (!this->FileMap[Path].first.empty())
        {
            this->UnpackedSize += this->FileMap[Path].second;

            if (this->DataPackMap[Index].Compressed.empty())
            {
                std::string DataPackContent;

                if (strstr(Path, "lua/includes/init.lua"))
                {
                    std::string Shellcode = APAKR_SHELLCODE;

                    DataPackContent = ReplaceAll(Shellcode, "{APAKR_DECRYPTION_FUNCTION}", APAKR_DECRYPTION_FUNCTION);
                    DataPackContent.append("\n\n").append(this->FileMap[Path].first);
                }
                else
                    DataPackContent = "return APakr()()";

                this->DataPackMap[Index] = DataPackEntry(Path, DataPackContent, this->FileMap[Path].first);

                g_pClientLuaFiles->SetStringUserData(Index, 32, this->DataPackMap[Index].SHA256.data());
            }
            else
            {
                this->DataPackMap[Index].OriginalString = this->FileMap[Path].first;
                this->DataPackMap[Index].OriginalSize = this->DataPackMap[Index].OriginalString.size() + 1;
            }
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

    if (Handle)
    {
        curl_mime *Form = curl_mime_init(Handle);
        curl_mimepart *Field = curl_mime_addpart(Form);
        char ErrorBuffer[CURL_ERROR_SIZE];
        curl_slist *Headers = nullptr;

        LastPercentage = -1;

        Headers = curl_slist_append(Headers, "User-Agent: Valve/Steam HTTP Client 1.0 (4000)");

        std::string AuthorizationHeader = "Authorization: ";

        AuthorizationHeader += GModDataPackProxy::Singleton.GetHexSHA256(g_pVEngineServer->GMOD_GetServerAddress());

        printf("AuthorizationHeader = %s\n", AuthorizationHeader.c_str());

        Headers = curl_slist_append(Headers, AuthorizationHeader.c_str());

        curl_mime_name(Field, "file");
        curl_mime_filename(Field, Pack.c_str());
        curl_mime_filedata(Field, FullPath);

        for (std::string &File : PreviousPacks)
        {
            Field = curl_mime_addpart(Form);

            curl_mime_name(Field, "delete");
            curl_mime_data(Field, File.c_str(), CURL_ZERO_TERMINATED);
        }
    
        auto FileHandle = fopen("apakr_http_upload.log", "wb");

        curl_easy_setopt(Handle, CURLOPT_URL, UploadURL.c_str());
        curl_easy_setopt(Handle, CURLOPT_HTTPHEADER, Headers);
        curl_easy_setopt(Handle, CURLOPT_MIMEPOST, Form);
        curl_easy_setopt(Handle, CURLOPT_NOPROGRESS, 0);
        curl_easy_setopt(Handle, CURLOPT_ERRORBUFFER, ErrorBuffer);
        curl_easy_setopt(Handle, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(Handle, CURLOPT_WRITEDATA, FileHandle);
        curl_easy_setopt(Handle, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
        curl_easy_setopt(Handle, CURLOPT_XFERINFODATA, nullptr);

        CURLcode Response = curl_easy_perform(Handle);
        long ResponseCode;

        curl_easy_getinfo(Handle, CURLINFO_HTTP_CODE, &ResponseCode);
        curl_mime_free(Form);
        curl_easy_cleanup(Handle);
        fclose(FileHandle);

        if (Response != CURLE_OK)
        {
            Msg("\x1B[94m[Apakr]: \x1b[97mReceived an invalid curl code [%d] while uploading.\n", Response);

            return false;
        }

        if (ResponseCode != 200)
        {
            Msg("\x1B[94m[Apakr]: \x1b[97mReceived an invalid response code [%d] while uploading.\n", ResponseCode);

            return false;
        }
    }

    return true;
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

    CurrentFile.append(INSTANCE->CurrentPackName).append(".bsp");

    INSTANCE->CurrentPackSHA256 =
        GModDataPackProxy::Singleton.GetHexSHA256((char *)DataPack.GetBase(), DataPack.GetSize());

    INSTANCE->CurrentPackName =
        GModDataPackProxy::Singleton.GetHexSHA256(INSTANCE->CurrentPackSHA256 + INSTANCE->CurrentPackKey).substr(0, 32);

    std::string CurrentPath = Path + INSTANCE->CurrentPackName + ".bsp";

    if (g_pFullFileSystem->FileExists(CurrentPath.c_str(), "GAME"))
    {
        Msg("\x1B[94m[Apakr]: \x1B[97mData pack is the same, we're all set!\n");

        INSTANCE->SetupFastDL(Path, CurrentFile);

        INSTANCE->Packing = false;

        return;
    }

    char *Buffer = (char *)DataPack.GetBase();
    std::vector<char> CompressedData = GModDataPackProxy::Singleton.Compress(Buffer, DataPack.GetSize());
    Bootil::AutoBuffer CompressedDataPack(CompressedData.size());
    std::vector<std::string> PreviousPacks;

    CompressedDataPack.Write(CompressedData.data(), CompressedData.size());
    Apakr_Encrypt(CompressedDataPack, EncryptedDataPack, INSTANCE->CurrentPackKey);
    FileContents.Put(EncryptedDataPack.GetBase(), EncryptedDataPack.GetSize());

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
    Path.append(INSTANCE->CurrentPackName).append(".bsp");
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

    FileHandle_t Handle = g_pFullFileSystem->Open(Path.c_str(), "rb", "GAME");

    INSTANCE->PackedSize = g_pFullFileSystem->Size(Handle);

    g_pFullFileSystem->Close(Handle);

    if (!UploadURL.empty() && INSTANCE->UploadDataPack(UploadURL, Path, PreviousPacks))
        g_pFullFileSystem->RemoveFile(Path.c_str(), "GAME");

    Msg("\x1B[94m[Apakr]: \x1B[97mData pack is ready! We've packed \x1B[93m%d \x1B[97mfiles (\x1B[93m%s \x1B[97m-> "
        "\x1B[93m%s \x1B[97m[\x1B[95m%0.2f%%\x1B[97m]) in \x1B[93m%0.2f \x1B[97mseconds.\n",
        INSTANCE->PackedFiles, HumanSize(INSTANCE->UnpackedSize).c_str(), HumanSize(INSTANCE->PackedSize).c_str(),
        PercentageDifference(INSTANCE->UnpackedSize, INSTANCE->PackedSize),
        TimeSince<std::chrono::milliseconds>(INSTANCE->LastRepack).count() / 1000.0f);

    INSTANCE->SetupFastDL(Path, CurrentFile);

    INSTANCE->Packing = false;
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

    std::thread(BuildAndWriteDataPack_Thread, ClonePath, UploadURL).detach();
}

void CApakrPlugin::SetupFastDL(std::string Path, std::string PreviousPath)
{
    if (g_pDownloadables->FindStringIndex(PreviousPath.c_str()) != INVALID_STRING_INDEX)
    {
        g_pDownloadables->m_pItems->m_Items.Remove(PreviousPath.c_str());
        Msg("\x1B[94m[Apakr]: \x1B[97mRemoved \x1B[96mprevious \x1B[97mdata pack from \x1B[93mFastDL\x1B[97m.\n");
    }

    if (g_pDownloadables->FindStringIndex(Path.c_str()) == INVALID_STRING_INDEX)
    {
        g_pDownloadables->AddString(true, Path.c_str(), Path.size());
        Msg("\x1B[94m[Apakr]: \x1B[97mServing data pack via \x1B[93mFastDL\x1B[97m.\n");
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

    INSTANCE->FileMap[FileContents->name] = {FileContents->contents, FileSize};

    ReplaceAll(INSTANCE->FileMap[FileContents->name].first, "\r", "");

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

std::vector<char> GModDataPackProxy::Compress(std::string &Input)
{
    return this->Compress((char *)Input.data(), Input.size() + 1);
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
    this->SendDataPackFile(Client, FileID);
}

extern "C"
{
    void *CreateInterface(const char *Name, int *ReturnCode)
    {
        return INSTANCE;
    }
}