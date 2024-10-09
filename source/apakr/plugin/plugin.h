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

#ifndef FCVAR_LUA_SERVER
#define FCVAR_LUA_SERVER (1 << 19)
#endif

#define BZ2_DEFAULT_BLOCKSIZE100k 9
#define BZ2_DEFAULT_WORKFACTOR 0
#define GMOD_LUASHARED_INTERFACE "LUASHARED003"

using Time = std::chrono::system_clock::time_point;
using _32CharArray = std::array<char, 32>;

struct Template
{
    std::string Pattern;
    std::string Replacement;
};

namespace GarrysMod
{
namespace Lua
{
class ILuaInterface;

namespace State
{
enum
{
    CLIENT = 0,
    SERVER,
    MENU
};
} // namespace State

class ILuaShared
{
  public:
    virtual ~ILuaShared() = 0;
    virtual void Init(void *(*)(const char *, int *), bool, CSteamAPIContext *, IGet *) = 0;
    virtual void Shutdown() = 0;
    virtual void DumpStats() = 0;
    virtual ILuaInterface *CreateLuaInterface(unsigned char, bool) = 0;
    virtual void CloseLuaInterface(ILuaInterface *) = 0;
    virtual ILuaInterface *GetLuaInterface(unsigned char) = 0;
    virtual void *LoadFile(const std::string &path, const std::string &pathId, bool fromDatatable, bool fromFile) = 0;
    virtual void *GetCache(const std::string &);
    virtual void MountLua(const char *) = 0;
    virtual void MountLuaAdd(const char *, const char *) = 0;
    virtual void UnMountLua(const char *) = 0;
    virtual void SetFileContents(const char *, const char *) = 0;
    virtual void SetLuaFindHook(void *) = 0;
    virtual void FindScripts(const std::string &, const std::string &, std::vector<std::string> &) = 0;
    virtual const char *GetStackTraces() = 0;
    virtual void InvalidateCache(const std::string &) = 0;
    virtual void EmptyCache() = 0;
};
} // namespace Lua
} // namespace GarrysMod

extern IServer *g_pServer;
extern IVEngineServer *g_pVEngineServer;
extern CNetworkStringTableContainer *g_pNetworkStringTableContainer;
extern CNetworkStringTable *g_pClientLuaFiles;
extern CNetworkStringTable *g_pDownloadables;
extern GarrysMod::Lua::ILuaShared *g_pILuaShared;
extern GarrysMod::Lua::ILuaInterface *g_pLUAServer;

template <typename Return> inline Return TimeSince(const Time &When)
{
    Time Now = std::chrono::system_clock::now();

    if (When.time_since_epoch().count() == 0)
        return Return(0);

    return std::chrono::duration_cast<Return>(Now - When);
}

inline std::string PaddedHex(const int &Number, const int &Padding)
{
    std::ostringstream Stream;

    Stream << std::hex << std::setw(Padding) << std::setfill('0') << Number;

    return Stream.str();
}

inline std::string ReplaceAll(std::string &Input, const std::string &Replace, const std::string &With)
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

inline std::string HumanSize(double Bytes)
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

inline double PercentageDifference(const double &UnpackedSize, const double &PackedSize)
{
    if (UnpackedSize == 0)
        return PackedSize == 0 ? 0 : 100;

    return ((PackedSize - UnpackedSize) / UnpackedSize) * 100;
}

inline std::string GetIPAddress()
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

inline bool IsBridgedInterface()
{
    static std::string IPAddress = GetIPAddress();

    return IPAddress != "localhost" && IPAddress != "0.0.0.0" && IPAddress.substr(0, 3) != "10." &&
           IPAddress.substr(0, 4) != "172." && IPAddress.substr(0, 4) != "192.";
}

static SymbolFinder Finder;

template <class T> inline T ResolveSymbol(const SourceSDK::FactoryLoader &Loader, const Symbol &Symbol)
{
    if (Symbol.type == Symbol::Type::None)
        return nullptr;

    auto Pointer = (uint8_t *)(Finder.Resolve(Loader.GetModule(), Symbol.name.c_str(), Symbol.length, nullptr));

    if (Pointer)
        Pointer += Symbol.offset;

    return reinterpret_cast<T>(Pointer);
}

template <class T> inline T ResolveSymbols(const SourceSDK::FactoryLoader &Loader, const std::vector<Symbol> &Symbols)
{
    for (const auto &Symbol : Symbols)
    {
        T Return = ResolveSymbol<T>(Loader, Symbol);

        if (Return)
            return Return;
    }

    return nullptr;
}

inline void CloneFile(const std::string &CurrentPath, const std::filesystem::path &FullPath)
{
    try
    {
        std::filesystem::create_directories(FullPath.parent_path());
    }
    catch (std::filesystem::filesystem_error &Error)
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
        catch (std::filesystem::filesystem_error &Error)
        {
        }
}

static int BarSize = 40;
static double LastPercentage = -1;

inline void DisplayProgressBar(const double &Percentage)
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

inline const char *GetConvarString(ConVar *CVar)
{
#if defined ARCHITECTURE_X86
    return CVar->GetString();
#else
    return CVar->Get<const char *>();
#endif
}

inline void InstallConvarChangeCallback(ConVar *CVar, FnChangeCallback_t Callback)
{
#if defined ARCHITECTURE_X86
    CVar->InstallChangeCallback(Callback);
#else
    CVar->InstallChangeCallback(Callback, false);
#endif
}

inline void RemoveConvarChangeCallback(ConVar *CVar, FnChangeCallback_t Callback)
{
#if defined ARCHITECTURE_X86
    ((HackedConVar *)CVar)->m_fnChangeCallback = nullptr;
#else
    CVar->RemoveChangeCallback(Callback);
#endif
}

class GModDataPack;

#if defined(APAKR_32_SERVER)
inline IFileSystem *g_pFullFileSystem = nullptr;
#endif

struct GmodDataPackFile
{
    int time;
    const char *name;
    const char *source;
    const char *contents;

    struct AutoBuffer
    {
        void *m_pData;
        unsigned int m_iSize;
        unsigned int m_iPos;
        unsigned int m_iWritten;
    } compressed;

    unsigned int timesloadedserver;
    unsigned int timesloadedclient;
};

struct GmodPlayer
{
    IClient *Client = nullptr;
    bool LoadingIn = false;

    GmodPlayer()
    {
    }

    GmodPlayer(IClient *_Client, bool _LoadingIn)
    {
        this->Client = _Client;
        this->LoadingIn = _LoadingIn;
    }
};

struct DataPackEntry
{
    std::string FilePath = "";
    std::string Contents = "";
    int Size = 0;
    std::string OriginalContents = "";
    int OriginalSize = 0;
    _32CharArray SHA256 = {};
    std::vector<char> CompressedContents = {};

    DataPackEntry(){};

    DataPackEntry(const std::string &EntryFilePath, const std::string &EntryContents,
                  const std::string &EntryOriginalContents);
};

struct FileEntry
{
    std::string Contents = "";
    int Size = 0;
    char *SHA256 = nullptr;

    FileEntry(){};
    FileEntry(const std::string &EntryContents, int EntrySize);
};

class CApakrPlugin : public IServerPluginCallbacks, public IGameEventListener2
{
  public:
    static CApakrPlugin *Singleton;

    double UnpackedSize = 0;
    double PackedSize = 0;
    int PackedFiles = 0;
    std::string CurrentPackName = "";
    std::string CurrentPackSHA256 = "";
    std::string CurrentPackKey = "";
    std::string TemplatePath = "";
    std::filesystem::file_time_type LastTemplateEdit;
    std::chrono::time_point<std::chrono::system_clock> LastRepack, LastUploadBegan, LastTemplateUpdate;
    std::vector<GmodPlayer *> Players;
    bool Ready = false;
    bool PackReady = false;
    bool NeedsRepack = false;
    bool Packing = false;
    bool FailedUpload = false;
    bool Disabled = false;
    bool WasDisabled = false;
    std::unordered_map<std::string, FileEntry> FileMap;
    std::map<int, DataPackEntry> DataPackMap;
    std::vector<Template> PreprocessorTemplates;

    CApakrPlugin(){};
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
    virtual void LevelShutdown(){};
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
    Bootil::AutoBuffer GetDataPackBuffer();
    void SetupClientFiles();

    bool UploadDataPack(const std::string &UploadURL, const std::string &Pack,
                        const std::vector<std::string> &PreviousPacks);

    bool CanDownloadPack(const std::string &DownloadURL);
    void BuildAndWriteDataPack();
    void SetupDL(const std::string &FilePath, const std::string &PreviousPath);
    void LoadPreprocessorTemplates();
};

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
    std::vector<char> Compress(char *Input, int Size);
    std::vector<char> Compress(const std::string &Input);
    std::string Decompress(char *Input, int Size);
    std::vector<char> BZ2(char *Input, int Size);
    void ProcessFile(std::string &Code);
    void SendDataPackFile(int Client, int FileID);
    void SendFileToClient(int Client, int FileID);

  private:
    FunctionPointers::GModDataPack_AddOrUpdateFile_t AddOrUpdateFile_Original;
    FunctionPointers::GModDataPack_SendFileToClient_t SendFileToClient_Original;
};

typedef void (*IVEngineServer_GMOD_SendFileToClient_t)(IVEngineServer *, IRecipientFilter *, void *, int);

class IVEngineServerProxy : public Detouring::ClassProxy<IVEngineServer, IVEngineServerProxy>
{
  public:
    static IVEngineServerProxy Singleton;

    IVEngineServerProxy(){};
    ~IVEngineServerProxy(){};

    bool Load();
    void Unload();
    void GMOD_SendFileToClient(IRecipientFilter *Filter, void *BF_Data, int BF_Size);

  private:
    IVEngineServer_GMOD_SendFileToClient_t GMOD_SendFileToClient_Original;
};