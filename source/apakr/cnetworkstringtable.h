//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//
#ifndef NETWORKSTRINGTABLE_H
#define NETWORKSTRINGTABLE_H
#ifdef _WIN32
#pragma once
#endif

#include <networkstringtabledefs.h>

#include <utldict.h>
#include <utlsymbol.h>
#include <utlvector.h>
#include <utlbuffer.h>
#include <tier1/bitbuf.h>
#include <utlstring.h>
#include <utlhashtable.h>

class SVC_CreateStringTable;
class CBaseClient;

class CNetworkStringTableItem
{
  public:
    enum
    {
        MAX_USERDATA_BITS = 14,
        MAX_USERDATA_SIZE = (1 << MAX_USERDATA_BITS)
    };

    struct itemchange_s
    {
        int tick;
        int length;
        unsigned char *data;
    };

    CNetworkStringTableItem(void);

    ~CNetworkStringTableItem(void)
    {
#ifndef SHARED_NET_STRING_TABLES
        if (m_pChangeList)
        {
            // free changelist and elements

            for (int i = 0; i < m_pChangeList->Count(); i++)
            {
                itemchange_s item = m_pChangeList->Element(i);

                if (item.data)
                    delete[] item.data;
            }

            delete m_pChangeList; // destructor calls Purge()

            m_pUserData = NULL;
        }
#endif

        if (m_pUserData)
        {
            delete[] m_pUserData;
        }
    }

#ifndef SHARED_NET_STRING_TABLES
    void EnableChangeHistory(void);
    void UpdateChangeList(int tick, int length, const void *userData);
    int RestoreTick(int tick);
    inline int GetTickCreated(void) const
    {
        return m_nTickCreated;
    }
#endif

    bool SetUserData(int tick, int length, const void *userdata);
    const void *GetUserData(int *length = 0);
    inline int GetUserDataLength() const
    {
        return m_nUserDataLength;
    }

    // Used by server only
    // void			SetTickCount( int count ) ;
    inline int GetTickChanged(void) const
    {
        return m_nTickChanged;
    }

    unsigned char *m_pUserData;
    int m_nUserDataLength;
    int m_nTickChanged;

#ifndef SHARED_NET_STRING_TABLES
    int m_nTickCreated;
    CUtlVector<itemchange_s> *m_pChangeList;
#endif
};

abstract_class INetworkStringDict
{
  public:
    virtual ~INetworkStringDict()
    {
    }

    virtual unsigned int Count() = 0;
    virtual void Purge() = 0;
    virtual const char *String(int index) = 0;
    virtual bool IsValidIndex(int index) = 0;
    virtual int Insert(const char *pString) = 0;
    virtual int Find(const char *pString) = 0;
    virtual CNetworkStringTableItem &Element(int index) = 0;
    virtual const CNetworkStringTableItem &Element(int index) const = 0;
};

class CNetworkStringDict : public INetworkStringDict
{
  public:
    CNetworkStringDict()
    {
    }

    virtual ~CNetworkStringDict()
    {
        Purge();
    }

    unsigned int Count()
    {
        return m_Items.Count();
    }

    void Purge()
    {
        m_Items.Purge();
    }

    const char *String(int index)
    {
        return m_Items.Key(index);
    }

    bool IsValidIndex(int index)
    {
        return m_Items.IsValidHandle(index);
    }

    int Insert(const char *pString)
    {
        return m_Items.Insert(pString);
    }

    int Find(const char *pString)
    {
        return m_Items.Find(pString);
    }

    CNetworkStringTableItem &Element(int index)
    {
        return m_Items.Element(index);
    }

    const CNetworkStringTableItem &Element(int index) const
    {
        return m_Items.Element(index);
    }

    // private:
    typedef CUtlStableHashtable<CUtlConstString, CNetworkStringTableItem, CaselessStringHashFunctor,
                                CaselessStringEqualFunctor, uint16, const char *>
        StableHashtable_t;

    StableHashtable_t m_Items;

    class _StableHashtable_t : public StableHashtable_t
    {
      public:
        Hashtable_t &GetHashTable()
        {
            return m_table;
        }

        LinkedList_t &GetLinkedList()
        {
            return m_data;
        }
    };
};

//-----------------------------------------------------------------------------
// Purpose: Client/Server shared string table definition
//-----------------------------------------------------------------------------
class CNetworkStringTable : public INetworkStringTable
{
  public:
    // Construction
    CNetworkStringTable(TABLEID id, const char *tableName, int maxentries, int userdatafixedsize,
                        int userdatanetworkbits, bool bIsFilenames);
    virtual ~CNetworkStringTable(void);

    // INetworkStringTable interface:

    const char *GetTableName(void) const;
    TABLEID GetTableId(void) const;
    int GetNumStrings(void) const;
    int GetMaxStrings(void) const;
    int GetEntryBits(void) const;

    // Networking
    void SetTick(int tick);
    bool ChangedSinceTick(int tick) const;

    int AddString(bool bIsServer, const char *value, int length = -1, const void *userdata = NULL);
    const char *GetString(int stringNumber);

    void SetStringUserData(int stringNumber, int length, const void *userdata);
    const void *GetStringUserData(int stringNumber, int *length);
    int FindStringIndex(char const *string);

    void SetStringChangedCallback(void *object, pfnStringChanged changeFunc);

    bool HasFileNameStrings() const;
    bool IsUserDataFixedSize() const;
    int GetUserDataSizeBits() const;
    int GetUserDataSize() const;

#ifndef SHARED_NET_STRING_TABLES
    int WriteUpdate(CBaseClient *client, bf_write &buf, int tick_ack);
    void ParseUpdate(bf_read &buf, int entries);

    // HLTV change history & rollback
    void EnableRollback();
    void RestoreTick(int tick);

    // local backdoor tables
    void SetMirrorTable(INetworkStringTable *table);
    void UpdateMirrorTable(int tick_ack);
    void CopyStringTable(CNetworkStringTable *table);
    // buffer IO
    void WriteStringTable(CUtlBuffer &buf);
    bool ReadStringTable(CUtlBuffer &buf);

    bool WriteBaselines(SVC_CreateStringTable &msg, char *msg_buffer, int msg_buffer_size);
#endif

    void TriggerCallbacks(int tick_ack);

    CNetworkStringTableItem *GetItem(int i);

    // debug ouptput
    virtual void Dump(void);
    virtual void Lock(bool bLock);

    void SetAllowClientSideAddString(bool state);
    pfnStringChanged GetCallback();

    // protected:
    void DataChanged(int stringNumber, CNetworkStringTableItem *item);

    // Destroy string table
    void DeleteAllStrings(void);

    CNetworkStringTable(const CNetworkStringTable &); // not implemented, not allowed

    TABLEID m_id;
    char *m_pszTableName;
    // Must be a power of 2, so encoding can determine # of bits to use based on log2
    int m_nMaxEntries;
    int m_nEntryBits;
    int m_nTickCount;
    int m_nLastChangedTick;

    bool m_bChangeHistoryEnabled : 1;
    bool m_bLocked : 1;
    bool m_bAllowClientSideAddString : 1;
    bool m_bUserDataFixedSize : 1;
    bool m_bIsFilenames : 1;

    int m_nUserDataSize;
    int m_nUserDataSizeBits;

    // Change function callback
    pfnStringChanged m_changeFunc;
    // Optional context/object
    void *m_pObject;

    // pointer to local backdoor table
    CNetworkStringTable *m_pMirrorTable;

    CNetworkStringDict *m_pItems;
    CNetworkStringDict *m_pItemsClientSide; // For m_bAllowClientSideAddString, these items are non-networked and are
                                            // referenced by a negative string index!!!
};

//-----------------------------------------------------------------------------
// Purpose: Implements game .dll string table interface
//-----------------------------------------------------------------------------
class CNetworkStringTableContainer : public INetworkStringTableContainer
{
  public:
    // Construction
    CNetworkStringTableContainer(void);
    ~CNetworkStringTableContainer(void);

    // Implement INetworkStringTableContainer
    INetworkStringTable *CreateStringTable(const char *tableName, int maxentries, int userdatafixedsize = 0,
                                           int userdatanetworkbits = 0)
    {
        return CreateStringTableEx(tableName, maxentries, userdatafixedsize, userdatanetworkbits, false);
    }
    INetworkStringTable *CreateStringTableEx(const char *tableName, int maxentries, int userdatafixedsize = 0,
                                             int userdatanetworkbits = 0, bool bIsFilenames = false);
    void RemoveAllTables(void);

    // table infos
    INetworkStringTable *FindTable(const char *tableName) const;
    INetworkStringTable *GetTable(TABLEID stringTable) const;
    int GetNumTables(void) const;

    virtual void SetAllowClientSideAddString(INetworkStringTable *table, bool bAllowClientSideAddString);

    // Update a client (called once during packet sending per client)
    void SetTick(int tick_count);

#ifndef SHARED_NET_STRING_TABLES
    // rollback feature
    void EnableRollback(bool bState);
    void RestoreTick(int tick);

    // Buffer I/O
    void WriteStringTables(CUtlBuffer &buf);
    bool ReadStringTables(CUtlBuffer &buf);

    void WriteUpdateMessage(CBaseClient *client, int tick_ack, bf_write &buf);
    void WriteBaselines(bf_write &buf);
    void DirectUpdate(int tick_ack); // fill mirror table directly with updates
#endif

    void TriggerCallbacks(int tick_ack); // fire callback functions

    // Guards so game .dll can't create tables at the wrong time
    void AllowCreation(bool state);

    // Print table data to console
    void Dump(void);
    // Sets the lock and returns the previous lock state
    bool Lock(bool bLock);

    void SetAllowClientSideAddString(bool state);

    bool m_bAllowCreation;  // creat guard Guard
    int m_nTickCount;       // current tick
    bool m_bLocked;         // currently locked?
    bool m_bEnableRollback; // enables rollback feature

    CUtlVector<CNetworkStringTable *> m_Tables; // the string tables
};

#endif // NETWORKSTRINGTABLE_H