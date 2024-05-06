#include <GarrysMod/InterfacePointers.hpp>
#include <GarrysMod/FunctionPointers.hpp>
#include <GarrysMod/Lua/Interface.h>
#include <apakr/plugin/encryption.h>
#include <apakr/plugin/shellcode.h>
#include <detouring/classproxy.hpp>
#include <apakr/cnetworkstringtable.h>
#include <../utils/lzma/C/Sha256.h>
#include <../utils/lzma/C/LzmaLib.h>
#include <engine/iserverplugin.h>
#include <GarrysMod/Symbol.hpp>
#include <filesystem_base.h>
#include <Bootil/Bootil.h>
#include <igameevents.h>
#include <filesystem>
#include <iserver.h>
#include <iclient.h>
#include <eiface.h>
#include <convar.h>
#include <iomanip>
#include <pthread.h>
#include <thread>
#include <random>
#include <map>

#ifndef FCVAR_LUA_SERVER
#define FCVAR_LUA_SERVER (1 << 19)
#endif

using Time = std::chrono::system_clock::time_point;
using _32CharArray = std::array<char, 32>;

template <typename Return> inline Return TimeSince(Time When)
{
    Time Now = std::chrono::system_clock::now();

    if (When.time_since_epoch().count() == 0)
        return Return(0);

    return std::chrono::duration_cast<Return>(Now - When);
}

inline std::string PaddedHex(int Number, int Padding)
{
    std::ostringstream Stream;

    Stream << std::hex << std::setw(Padding) << std::setfill('0') << Number;

    return Stream.str();
}

inline std::string ReplaceAll(std::string &Input, std::string Replace, std::string With)
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

inline double PercentageDifference(double UnpackedSize, double PackedSize)
{
    if (UnpackedSize == 0)
        return PackedSize == 0 ? 0 : 100;

    return ((PackedSize - UnpackedSize) / UnpackedSize) * 100;
}

class GModDataPack;
inline CNetworkStringTableContainer *g_pNetworkStringTableContainer = nullptr;
inline CNetworkStringTable *g_pClientLuaFiles = nullptr;
inline CNetworkStringTable *g_pDownloadables = nullptr;

#if defined(APAKR_32_SERVER)
inline IFileSystem *g_pFullFileSystem = nullptr;
#endif

inline IServer *g_pServer = nullptr;
inline IVEngineServer *g_pVEngineServer = nullptr;
inline ICvar *g_pConVars = nullptr;

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

struct DataPackEntry;

class CApakrPlugin : public IServerPluginCallbacks, public IGameEventListener2
{
  public:
    double UnpackedSize = 0;
    double PackedSize = 0;
    int PackedFiles = 0;
    std::string CurrentPackName = "";
    std::string CurrentPackSHA256 = "";
    std::string CurrentPackKey = "";
    std::chrono::time_point<std::chrono::system_clock> LastRepack;
    std::vector<GmodPlayer *> Players;
    bool Ready = false;
    bool NeedsRepack = false;
    bool Packing = false;
    std::map<std::string, std::pair<std::string, int>> FileMap;
    std::map<int, DataPackEntry> DataPackMap;

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
    void BuildAndWriteDataPack();
    void SetupFastDL(std::string Path, std::string PreviousPath);
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
    std::string GetHexSHA256(std::string Data);
    std::string SHA256ToHex(_32CharArray SHA256);
    std::vector<char> Compress(std::string &Input);
    std::vector<char> Compress(char *Input, int Size);
    void SendDataPackFile(int Client, int FileID);
    void SendFileToClient(int Client, int FileID);

  private:
    FunctionPointers::GModDataPack_AddOrUpdateFile_t AddOrUpdateFile_Original;
    FunctionPointers::GModDataPack_SendFileToClient_t SendFileToClient_Original;
};

struct DataPackEntry
{
    std::string Path = "";
    std::string String = "";
    int Size = 0;
    std::string OriginalString = "";
    int OriginalSize = 0;
    _32CharArray SHA256 = {};
    std::vector<char> Compressed = {};

    DataPackEntry()
    {
    }

    DataPackEntry(std::string _Path, std::string _String, std::string _Original)
    {
        this->Path = _Path;
        this->String = ReplaceAll(_String, "\r", "");
        this->Size = this->String.size() + 1;
        this->OriginalString = _Original;
        this->OriginalSize = this->OriginalString.size() + 1;
        this->SHA256 = GModDataPackProxy::Singleton.GetSHA256(this->String.data(), this->Size);
        this->Compressed = GModDataPackProxy::Singleton.Compress(this->String);
    }
};