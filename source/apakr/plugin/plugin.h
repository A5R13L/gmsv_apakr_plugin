#include <GarrysMod/InterfacePointers.hpp>
#include <GarrysMod/FunctionPointers.hpp>
#include <GarrysMod/Lua/LuaInterface.h>
#include <GarrysMod/FactoryLoader.hpp>
#include <apakr/cnetworkstringtable.h>
#include <apakr/plugin/encryption.h>
#include <../utils/lzma/C/LzmaLib.h>
#include <scanning/symbolfinder.hpp>
#include <detouring/classproxy.hpp>
#include <../utils/lzma/C/Sha256.h>
#include <apakr/plugin/shellcode.h>
#include <engine/iserverplugin.h>
#include <GarrysMod/Symbol.hpp>
#include <filesystem_base.h>
#include <recipientfilter.h>
#include <nlohmann/json.hpp>
#include <Bootil/Bootil.h>
#include <GarrysMod/Lua/LuaShared.h>
#include <apakr/convar.h>
#include <bzip2/bzlib.h>
#include <inetchannel.h>
#include <curl/curl.h>
#include <filesystem>
#include <lauxlib.h>
#include <iserver.h>
#include <iclient.h>
#include <thread>
#include <regex>

typedef bool (*apakr_filter)(const char *Path, const char *Contents);
typedef void (*apakr_mutate)(bool AutoRefresh, const char *Path, const char *Contents, void (*MutatePath)(const char *),
                             void (*MutateContents)(const char *));
                             
#ifndef FCVAR_LUA_SERVER
#define FCVAR_LUA_SERVER (1 << 19)
#endif

#define BZ2_DEFAULT_BLOCKSIZE100k 9
#define BZ2_DEFAULT_WORKFACTOR 0

using Time = std::chrono::system_clock::time_point;
using _32CharArray = std::array<uint8_t, 32>;
using luaL_loadbufferx_t = decltype(&luaL_loadbufferx);

std::vector<Symbol> IVEngineServer_GMOD_SendToClient = {
#if defined SYSTEM_WINDOWS
#if defined ARCHITECTURE_X86
    Symbol::FromSignature("\x55\x8B\xEC\x83\xEC\x2A\x56\x8D\x4D"),
#else
    Symbol::FromSignature(
        "\x4C\x8B\xDC\x49\x89\x5B\x2A\x49\x89\x73\x2A\x57\x48\x81\xEC\x2A\x2A\x2A\x2A\x33\xC9\xC6\x44\x24"),
#endif
#elif defined SYSTEM_LINUX
#if defined ARCHITECTURE_X86_64
    Symbol::FromSignature("\x55\x48\x89\xE5\x41\x57\x49\x89\xD7\x41\x56\x49\x89\xF6\x41\x55\x41\x54"),
#else
    Symbol::FromName("_ZN14CVEngineServer17GMOD_SendToClientEP16IRecipientFilterPvi")
#endif
#endif
};

#if defined SYSTEM_WINDOWS && defined ARCHITECTURE_X86_64
std::vector<Symbol> IServer_Reference = {
    Symbol::FromSignature("\x48\x8D\x0D\x2A\x2A\x2A\x2A\xE8\x2A\x2A\x2A\x2A\x48\x8D\x0D\x2A"
                          "\x2A\x2A\x2A\x8B\xD8\xE8\x2A\x2A\x2A\x2A\x2B\xD8")};
#endif

struct GmodDataPackFile
{
    int time;

#if defined SYSTEM_LINUX || (defined SYSTEM_WINDOWS && defined ARCHITECTURE_X86)

    char *name;
    char *source;
    char *contents;

#elif defined SYSTEM_WINDOWS && defined ARCHITECTURE_X86_64
    std::string name;
    std::string source;
    std::string contents;

#endif

    Bootil::_AutoBuffer compressed;
    unsigned int timesloadedserver;
    unsigned int timesloadedclient;
};

struct GmodPlayer
{
    IClient *Client;
    bool LoadingIn;

    GmodPlayer() : Client(nullptr), LoadingIn(false)
    {
    }

    GmodPlayer(IClient *_Client, bool _LoadingIn) : Client(_Client), LoadingIn(_LoadingIn)
    {
    }
};

struct DataPackEntry
{
    std::string FilePath, Contents, OriginalContents;
    int Size, OriginalSize;
    _32CharArray SHA256 = {};
    std::vector<uint8_t> CompressedContents = {};

    DataPackEntry() : Size(0), OriginalSize(0){};

    DataPackEntry(const std::string &EntryFilePath, const std::string &EntryContents,
                  const std::string &EntryOriginalContents);
};

struct FileEntry
{
    std::string Contents;
    int Size;
    uint8_t *SHA256;

    FileEntry() : Size(0), SHA256(nullptr){};
    FileEntry(const std::string &EntryContents, int EntrySize);
};

struct Template
{
    std::string Pattern;
    std::string Replacement;
};

struct Extension
{
    enum _Type
    {
        APakr,
        GLuaPack
    } Type;

    void *Handle;
    apakr_filter Filter;
    apakr_mutate Mutate;

    Extension() : Type(APakr), Handle(nullptr), Filter(nullptr), Mutate(nullptr){};

    Extension(_Type __Type, void *_Handle, apakr_filter _Filter, apakr_mutate _Mutate)
        : Type(__Type), Handle(_Handle), Filter(_Filter), Mutate(_Mutate){};
};

class CApakrPlugin : public IServerPluginCallbacks, public IGameEventListener2
{
  public:
    static CApakrPlugin *Singleton;

    std::chrono::time_point<std::chrono::system_clock> LastRepack, LastUploadBegan, LastTemplateUpdate;
    bool Loaded, ChangingLevel, Ready, PackReady, NeedsRepack, Packing, FailedUpload, Disabled, WasDisabled,
        NeedsDLSetup;
    std::string CurrentPackName, CurrentPackSHA256, CurrentPackKey, TemplatePath, PreviousDLPath, CurrentDLPath;
    std::unordered_map<std::string, FileEntry> FileMap;
    std::filesystem::file_time_type LastTemplateEdit;
    std::vector<Template> PreprocessorTemplates;
    std::vector<Extension> LoadedExtensions;
    std::map<int, DataPackEntry> DataPackMap;
    double UnpackedSize, PackedSize;
    std::vector<GmodPlayer *> Players;
    int PackedFiles;

    CApakrPlugin()
        : Loaded(false), ChangingLevel(false), Ready(false), PackReady(false), NeedsRepack(false), Packing(false),
          FailedUpload(false), Disabled(false), WasDisabled(false), NeedsDLSetup(false), UnpackedSize(0), PackedSize(0),
          PackedFiles(0){};
    ~CApakrPlugin(){};

    virtual bool Load(CreateInterfaceFn InterfaceFactory, CreateInterfaceFn GameServerFactory);
    virtual void Unload();
    virtual void Pause(){};
    virtual void UnPause(){};

    virtual const char *GetPluginDescription()
    {
        return "Apakr";
    };

    virtual void LevelInit(const char *Map){};
    virtual void ServerActivate(edict_t *EntityList, int EntityCount, int MaxClients);
    virtual void GameFrame(bool Simulating);
    virtual void LevelShutdown();
    virtual void ClientActive(edict_t *Entity);
    virtual void ClientDisconnect(edict_t *Entity);
    virtual void ClientPutInServer(edict_t *Entity, const char *Name){};
    virtual void SetCommandClient(int Index){};
    virtual void ClientSettingsChanged(edict_t *Entity){};

    virtual PLUGIN_RESULT ClientConnect(bool *AllowConnection, edict_t *Entity, const char *Name, const char *Address,
                                        char *Rejection, int MaxRejectionLength);

    virtual PLUGIN_RESULT ClientCommand(edict_t *Entity, const CCommand &Arguments)
    {
        return PLUGIN_CONTINUE;
    };

    virtual PLUGIN_RESULT NetworkIDValidated(const char *Name, const char *NetworkID)
    {
        return PLUGIN_CONTINUE;
    };

    virtual void OnQueryCvarValueFinished(QueryCvarCookie_t Cookie, edict_t *Player, EQueryCvarValueStatus Status,
                                          const char *Name, const char *Value){};

    virtual void OnEdictAllocated(edict_t *EDict){};
    virtual void OnEdictFreed(const edict_t *EDict){};
    virtual void FireGameEvent(IGameEvent *Event){};
    void CheckForRepack();
    std::pair<std::string, int> GetDataPackInfo();
    void SetupClientFiles();

    bool UploadDataPack(const std::string &UploadURL, const std::string &Pack,
                        const std::vector<std::string> &PreviousPacks);

    bool CanDownloadPack(const std::string &DownloadURL);
    void BuildAndWriteDataPack();
    void SetupDL(const std::string &FilePath, const std::string &PreviousPath);
    void CheckDLSetup();
    void LoadPreprocessorTemplates();
    void LoadExtensions();
    void CloseExtension(void *Handle);
};

class GModDataPack;

class GModDataPackProxy : public Detouring::ClassProxy<GModDataPack, GModDataPackProxy>
{
  public:
    static GModDataPackProxy Singleton;

    GModDataPackProxy(){};
    ~GModDataPackProxy(){};

    bool Load();
    void Unload();
    void AddOrUpdateFile(GmodDataPackFile *File, bool Refresh);
    _32CharArray GetSHA256(const char *Data, size_t Length);
    std::string GetHexSHA256(const char *Data, size_t Length);
    std::string GetHexSHA256(const std::string &Data);
    std::string SHA256ToHex(const _32CharArray &SHA256);
    std::vector<uint8_t> Compress(uint8_t *Input, int Size);
    std::vector<uint8_t> Compress(const std::string &Input);
    std::string Decompress(const uint8_t *Input, int Size);
    std::vector<uint8_t> BZ2(const uint8_t *Input, int Size);
    void ProcessFile(std::string &Code);
    void SendDataPackFile(int Client, int FileID);
    void SendFileToClient(int Client, int FileID);

  private:
    FunctionPointers::GModDataPack_AddOrUpdateFile_t AddOrUpdateFile_Original;
    FunctionPointers::GModDataPack_SendFileToClient_t SendFileToClient_Original;
};

typedef void(
#if defined SYSTEM_WINDOWS && ARCHITECTURE_X86
    __stdcall
#endif
        *IVEngineServer_GMOD_SendFileToClients_t)(IVEngineServer *, IRecipientFilter *, void *, int);

class IVEngineServerProxy : public Detouring::ClassProxy<IVEngineServer, IVEngineServerProxy>
{
  public:
    static IVEngineServerProxy Singleton;

    IVEngineServerProxy(){};
    ~IVEngineServerProxy(){};

    bool Load();
    void Unload();
    void
#if defined SYSTEM_WINDOWS && ARCHITECTURE_X86
        __stdcall
#endif
        GMOD_SendFileToClients(IRecipientFilter *Filter, void *BF_Data, int BF_Size);

  private:
    IVEngineServer_GMOD_SendFileToClients_t GMOD_SendFileToClients_Original;
};

#ifdef SYSTEM_WINDOWS
void SetConsoleColor(int Color)
{
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), Color);
}

void Msg(const char *Format, ...)
{
    va_list Arguments;

    va_start(Arguments, Format);

    std::string Message;
    char Buffer[1024];

    vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);

    Message = std::string(Buffer);

    int White = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

    for (size_t Index = 0; Index < Message.size(); Index++)
    {
        if (Message[Index] == '\x1B' && Message[Index + 1] == '[')
        {
            int ColorCode = 0;
            sscanf(&Message[Index + 2], "%dm", &ColorCode);

            switch (ColorCode)
            {
            case 91:
                SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
                break;
            case 92:
                SetConsoleColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                break;
            case 93:
                SetConsoleColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                break;
            case 94:
                SetConsoleColor(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                break;
            case 95:
                SetConsoleColor(FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                break;
            case 96:
                SetConsoleColor(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                break;
            default:
                SetConsoleColor(White);
                break;
            }

            while (Message[Index] != 'm' && Index < Message.size())
                Index++;
        }
        else
            std::cout << Message[Index];
    }

    va_end(Arguments);
}
#endif

template <typename Return> Return TimeSince(const Time &When)
{
    Time Now = std::chrono::system_clock::now();

    if (When.time_since_epoch().count() == 0)
        return Return(0);

    return std::chrono::duration_cast<Return>(Now - When);
}

std::string PaddedHex(const int &Number, const int &Padding)
{
    std::ostringstream Stream;

    Stream << std::hex << std::setw(Padding) << std::setfill('0') << Number;

    return Stream.str();
}

std::string ReplaceAll(std::string &Input, const std::string &Replace, const std::string &With)
{
    if (Input.empty() || Replace.empty())
        return Input;

    size_t Position = 0;

    while ((Position = Input.find(Replace, Position)) != std::string::npos)
    {
        Input.replace(Position, Replace.length(), With);

        Position += With.length();
    }

    return Input;
}

std::string HumanSize(double Bytes)
{
    static const char *Units[] = {"B", "KB", "MB"};
    int Index = 0;

    while (Bytes >= 1024 && Index < 2)
    {
        Bytes /= 1024;
        Index++;
    }

    std::ostringstream Stream;

    Stream << std::fixed << std::setprecision(1) << Bytes << " " << Units[Index];

    return Stream.str();
}

double PercentageDifference(const double &UnpackedSize, const double &PackedSize)
{
    if (UnpackedSize == 0)
        return PackedSize == 0 ? 0 : 100;

    return ((PackedSize - UnpackedSize) / UnpackedSize) * 100;
}

std::string GetIPAddress()
{
    static ConVar *ip = g_pCVar->FindVar("ip");

    if (!ip)
        return "";

#if defined(APAKR_32_SERVER)
    return ip->GetString();
#else
    return ip->Get<const char *>();
#endif
}

bool IsBridgedInterface()
{
    static std::string IPAddress = GetIPAddress();

    return IPAddress != "localhost" && IPAddress != "0.0.0.0" && IPAddress.substr(0, 3) != "10." &&
           IPAddress.substr(0, 4) != "172." && IPAddress.substr(0, 4) != "192.";
}

static SymbolFinder Finder;

template <class T> T ResolveSymbol(const SourceSDK::FactoryLoader &Loader, const Symbol &Symbol)
{
    if (Symbol.type == Symbol::Type::None)
        return nullptr;

    uint8_t *Pointer = (uint8_t *)(Finder.Resolve(Loader.GetModule(), Symbol.name.c_str(), Symbol.length, nullptr));

    if (Pointer)
        Pointer += Symbol.offset;

    return reinterpret_cast<T>(Pointer);
}

template <class T> T ResolveSymbols(const SourceSDK::FactoryLoader &Loader, const std::vector<Symbol> &Symbols)
{
    for (const Symbol &Symbol : Symbols)
    {
        T Return = ResolveSymbol<T>(Loader, Symbol);

        if (Return)
            return Return;
    }

    return nullptr;
}

char *GetRealAddressFromRelative(char *Address, int Offset, int InstructionSize)
{
    if (!Address)
        return nullptr;

    char *Instruction = Address + Offset;
    int RelativeAddress = *(int *)(Instruction);
    char *RealAddress = Address + InstructionSize + RelativeAddress;

    return RealAddress;
}

void CloneFile(const std::string &CurrentPath, const std::filesystem::path &FullPath)
{
    try
    {
        std::filesystem::create_directories(FullPath.parent_path());
    }
    catch (std::filesystem::filesystem_error &_)
    {
    }

    char FullPathBuffer[512];

    if (g_pFullFileSystem->RelativePathToFullPath(CurrentPath.c_str(), "GAME", FullPathBuffer, sizeof(FullPathBuffer)))
        try
        {
            Msg("\x1B[94m[Apakr]: \x1B[97mCloning \x1B[93m%s \x1B[97mto \x1B[93m%s\x1B[97m.\n", FullPathBuffer,
                FullPath.c_str());

            std::filesystem::copy_file(FullPathBuffer, FullPath);
        }
        catch (std::filesystem::filesystem_error &_)
        {
        }
}

int BarSize = 40;
double LastPercentage = -1;

void DisplayProgressBar(const double &Percentage)
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

const char *GetConvarString(ConVar *CVar)
{
#if defined ARCHITECTURE_X86
    return CVar->GetString();
#else
    return CVar->Get<const char *>();
#endif
}

void InstallConvarChangeCallback(ConVar *CVar, FnChangeCallback_t Callback)
{
#if defined ARCHITECTURE_X86
    CVar->InstallChangeCallback(Callback);
#else
    CVar->InstallChangeCallback(Callback, false);
#endif
}

void RemoveConvarChangeCallback(ConVar *CVar, FnChangeCallback_t Callback)
{
#if defined ARCHITECTURE_X86
    ((HackedConVar *)CVar)->m_fnChangeCallback = nullptr;
#else
    CVar->RemoveChangeCallback(Callback);
#endif
}
