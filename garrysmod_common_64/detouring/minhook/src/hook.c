/*
 *  MinHook - The Minimalistic API Hooking Library for x64/x86
 *  Copyright (C) 2009-2017 Tsuda Kageyu.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 *  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 *  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#include <unistd.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <string.h>
#include <locale.h>
#endif

#include <limits.h>

#include "../include/MinHook.h"
#include "buffer.h"
#include "trampoline.h"

#ifndef ARRAYSIZE
    #define ARRAYSIZE(A) (sizeof(A)/sizeof((A)[0]))
#endif

// Initial capacity of the HOOK_ENTRY buffer.
#define INITIAL_HOOK_CAPACITY   32

// Special hook position values.
#define INVALID_HOOK_POS UINT_MAX
#define ALL_HOOKS_POS    UINT_MAX

// Hook information.
typedef struct _HOOK_ENTRY
{
    void    *pTarget;             // Address of the target function.
    void    *pDetour;             // Address of the detour or relay function.
    void    *pTrampoline;         // Address of the trampoline function.
    uint8_t  backup[8];           // Original prologue of the target function.

    int32_t  patchAbove  : 1;     // Uses the hot patch area.
    int32_t  isEnabled   : 1;     // Enabled.
    int32_t  queueEnable : 1;     // Queued for enabling/disabling when != isEnabled.

    uint32_t nIP : 4;             // Count of the instruction boundaries.
    uint8_t  oldIPs[8];           // Instruction boundaries of the target function.
    uint8_t  newIPs[8];           // Instruction boundaries of the trampoline function.
} HOOK_ENTRY, *PHOOK_ENTRY;

#if defined(_WIN32) || defined(__APPLE__)
// Initial capacity of the thread IDs buffer.
#define INITIAL_THREAD_CAPACITY 128

// Freeze() action argument defines.
#define ACTION_DISABLE      0
#define ACTION_ENABLE       1
#define ACTION_APPLY_QUEUED 2

#ifdef _WIN32
// Thread access rights for suspending/resuming threads.
#define THREAD_ACCESS \
    (THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | THREAD_SET_CONTEXT)

typedef HANDLE THREAD_HANDLE;
typedef uint32_t *THREAD_ITEMS;
#else
typedef thread_act_t THREAD_HANDLE, *THREAD_ITEMS;
#endif

// Suspended threads for Freeze()/Unfreeze().
typedef struct _FROZEN_THREADS
{
    THREAD_ITEMS pItems;         // Data heap
    uint32_t     capacity;       // Size of allocated data heap, items
    uint32_t     size;           // Actual number of data items
} FROZEN_THREADS, *PFROZEN_THREADS;
#endif

//-------------------------------------------------------------------------
// Global Variables:
//-------------------------------------------------------------------------

#ifdef _WIN32
// Spin lock flag for EnterSpinLock()/LeaveSpinLock().
volatile LONG g_isLocked = FALSE;

// Private heap handle. If not NULL, this library is initialized.
HANDLE g_hHeap = NULL;
#else
// Spin lock flag for EnterSpinLock()/LeaveSpinLock().
volatile bool g_isLocked = false;
#endif

// Initialization counter, only uninitialize when it hits zero.
volatile int32_t g_initCounter = 0;

// Hook entries.
struct
{
    PHOOK_ENTRY pItems;     // Data heap
    uint32_t    capacity;   // Size of allocated data heap, items
    uint32_t    size;       // Actual number of data items
} g_hooks;

//-------------------------------------------------------------------------
// Returns INVALID_HOOK_POS if not found.
static uint32_t FindHookEntry(void *pTarget)
{
    uint32_t i;
    for (i = 0; i < g_hooks.size; ++i)
    {
        if ((uintptr_t)pTarget == (uintptr_t)g_hooks.pItems[i].pTarget)
            return i;
    }

    return INVALID_HOOK_POS;
}

//-------------------------------------------------------------------------
static PHOOK_ENTRY AddHookEntry()
{
    if (g_hooks.pItems == NULL)
    {
        g_hooks.capacity = INITIAL_HOOK_CAPACITY;
#ifdef _WIN32
        g_hooks.pItems = (PHOOK_ENTRY)HeapAlloc(
            g_hHeap, 0, g_hooks.capacity * sizeof(HOOK_ENTRY));
#else
        g_hooks.pItems = (PHOOK_ENTRY)malloc(g_hooks.capacity * sizeof(HOOK_ENTRY));
#endif
        if (g_hooks.pItems == NULL)
            return NULL;
    }
    else if (g_hooks.size >= g_hooks.capacity)
    {
#ifdef _WIN32
        PHOOK_ENTRY p = (PHOOK_ENTRY)HeapReAlloc(
            g_hHeap, 0, g_hooks.pItems, (g_hooks.capacity * 2) * sizeof(HOOK_ENTRY));
#else
        PHOOK_ENTRY p = (PHOOK_ENTRY)realloc(
            g_hooks.pItems, (g_hooks.capacity * 2) * sizeof(HOOK_ENTRY));
#endif
        if (p == NULL)
            return NULL;

        g_hooks.capacity *= 2;
        g_hooks.pItems = p;
    }

    return &g_hooks.pItems[g_hooks.size++];
}

//-------------------------------------------------------------------------
static void DeleteHookEntry(uint32_t pos)
{
    if (pos < g_hooks.size - 1)
        g_hooks.pItems[pos] = g_hooks.pItems[g_hooks.size - 1];

    g_hooks.size--;

    if (g_hooks.capacity / 2 >= INITIAL_HOOK_CAPACITY && g_hooks.capacity / 2 >= g_hooks.size)
    {
#ifdef _WIN32
        PHOOK_ENTRY p = (PHOOK_ENTRY)HeapReAlloc(
            g_hHeap, 0, g_hooks.pItems, (g_hooks.capacity / 2) * sizeof(HOOK_ENTRY));
#else
        PHOOK_ENTRY p = (PHOOK_ENTRY)realloc(
            g_hooks.pItems, (g_hooks.capacity / 2) * sizeof(HOOK_ENTRY));
#endif
        if (p == NULL)
            return;

        g_hooks.capacity /= 2;
        g_hooks.pItems = p;
    }
}

//-------------------------------------------------------------------------
#if defined(_WIN32) || defined(__APPLE__)
static uintptr_t FindOldIP(PHOOK_ENTRY pHook, uintptr_t ip)
{
    uint32_t i;

    if (pHook->patchAbove && ip == ((uintptr_t)pHook->pTarget - sizeof(JMP_REL)))
        return (uintptr_t)pHook->pTarget;

    for (i = 0; i < pHook->nIP; ++i)
    {
        if (ip == ((uintptr_t)pHook->pTrampoline + pHook->newIPs[i]))
            return (uintptr_t)pHook->pTarget + pHook->oldIPs[i];
    }

#ifdef MH_X86_64
    // Check relay function.
    if (ip == (uintptr_t)pHook->pDetour)
        return (uintptr_t)pHook->pTarget;
#endif

    return 0;
}
#endif

//-------------------------------------------------------------------------
#if defined(_WIN32) || defined(__APPLE__)
static uintptr_t FindNewIP(PHOOK_ENTRY pHook, uintptr_t ip)
{
    uint32_t i;
    for (i = 0; i < pHook->nIP; ++i)
    {
        if (ip == ((uintptr_t)pHook->pTarget + pHook->oldIPs[i]))
            return (uintptr_t)pHook->pTrampoline + pHook->newIPs[i];
    }

    return 0;
}
#endif

//-------------------------------------------------------------------------
#if defined(_WIN32) || defined(__APPLE__)
static void ProcessThreadIPs(THREAD_HANDLE hThread, uint32_t pos, uint32_t action)
{
    // If the thread suspended in the overwritten area,
    // move IP to the proper address.

#ifdef _WIN32
    CONTEXT  c;
#ifdef MH_X86_64
    PDWORD64 pIP = &c.Rip;
#else
    PDWORD   pIP = &c.Eip;
#endif
#else
#ifdef MH_X86_64
    mach_msg_type_number_t stateCount = x86_THREAD_STATE64_COUNT;
    x86_thread_state64_t   c;
    uint64_t              *pIP = &c.__rip;
#else
    mach_msg_type_number_t stateCount = x86_THREAD_STATE32_COUNT;
    i386_thread_state_t    c;
    uint32_t              *pIP = &c.__eip;
#endif
#endif
    uint32_t count;

#ifdef _WIN32
    c.ContextFlags = CONTEXT_CONTROL;
    if (!GetThreadContext(hThread, &c))
#else
#ifdef MH_X86_64
    if (thread_get_state(hThread, x86_THREAD_STATE64, (thread_state_t)&c, &stateCount) != KERN_SUCCESS)
#else
    if (thread_get_state(hThread, x86_THREAD_STATE32, (thread_state_t)&c, &stateCount) != KERN_SUCCESS)
#endif
#endif
        return;

    if (pos == ALL_HOOKS_POS)
    {
        pos = 0;
        count = g_hooks.size;
    }
    else
    {
        count = pos + 1;
    }

    for (; pos < count; ++pos)
    {
        PHOOK_ENTRY pHook = &g_hooks.pItems[pos];
        bool        enable;
        uintptr_t   ip;

        switch (action)
        {
        case ACTION_DISABLE:
            enable = false;
            break;

        case ACTION_ENABLE:
            enable = true;
            break;

        default: // ACTION_APPLY_QUEUED
            enable = pHook->queueEnable;
            break;
        }
        if (pHook->isEnabled == enable)
            continue;

        if (enable)
            ip = FindNewIP(pHook, *pIP);
        else
            ip = FindOldIP(pHook, *pIP);

        if (ip != 0)
        {
            *pIP = ip;
#ifdef _WIN32
            SetThreadContext(hThread, &c);
#else
#ifdef MH_X86_64
            thread_set_state(hThread, x86_THREAD_STATE64, (thread_state_t)&c, x86_THREAD_STATE64_COUNT);
#else
            thread_set_state(hThread, x86_THREAD_STATE32, (thread_state_t)&c, x86_THREAD_STATE32_COUNT);
#endif
#endif
        }
    }
}
#endif

//-------------------------------------------------------------------------
#if defined(_WIN32)
static bool EnumerateThreads(PFROZEN_THREADS pThreads)
{
    bool succeeded = false;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE)
    {
        THREADENTRY32 te;
        te.dwSize = sizeof(THREADENTRY32);
        if (Thread32First(hSnapshot, &te))
        {
            succeeded = true;
            do
            {
                if (te.dwSize >= (FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(uint32_t))
                    && te.th32OwnerProcessID == GetCurrentProcessId()
                    && te.th32ThreadID != GetCurrentThreadId())
                {
                    if (pThreads->pItems == NULL)
                    {
                        pThreads->capacity = INITIAL_THREAD_CAPACITY;
                        pThreads->pItems
                            = (uint32_t *)HeapAlloc(g_hHeap, 0, pThreads->capacity * sizeof(uint32_t));
                        if (pThreads->pItems == NULL)
                        {
                            succeeded = false;
                            break;
                        }
                    }
                    else if (pThreads->size >= pThreads->capacity)
                    {
                        pThreads->capacity *= 2;
                        uint32_t *p = (uint32_t *)HeapReAlloc(
                            g_hHeap, 0, pThreads->pItems, pThreads->capacity * sizeof(uint32_t));
                        if (p == NULL)
                        {
                            succeeded = false;
                            break;
                        }

                        pThreads->pItems = p;
                    }
                    pThreads->pItems[pThreads->size++] = te.th32ThreadID;
                }

                te.dwSize = sizeof(THREADENTRY32);
            } while (Thread32Next(hSnapshot, &te));

            if (succeeded && GetLastError() != ERROR_NO_MORE_FILES)
                succeeded = false;

            if (!succeeded && pThreads->pItems != NULL)
            {
                HeapFree(g_hHeap, 0, pThreads->pItems);
                pThreads->pItems = NULL;
            }
        }
        CloseHandle(hSnapshot);
    }

    return succeeded;
}
#elif defined(__APPLE__)
static bool EnumerateThreads(PFROZEN_THREADS pThreads)
{
    thread_act_port_array_t threadList;
    mach_msg_type_number_t threadCount, i;
    mach_port_t curtask = mach_task_self();
    thread_t curthread;

    bool succeeded = false;
    if (task_threads(curtask, &threadList, &threadCount) != KERN_SUCCESS)
        return succeeded;

    succeeded = true;
    curthread = mach_thread_self();
    for (i = 0; i < threadCount; ++i)
    {
        if (threadList[i] == curthread)
            continue;

        if (pThreads->pItems == NULL)
        {
            pThreads->capacity = INITIAL_THREAD_CAPACITY;
            pThreads->pItems = (uint32_t *)malloc(pThreads->capacity * sizeof(uint32_t));
            if (pThreads->pItems == NULL)
            {
                succeeded = false;
                break;
            }
        }
        else if (pThreads->size >= pThreads->capacity)
        {
            pThreads->capacity *= 2;
            uint32_t *p = (uint32_t *)realloc(
                pThreads->pItems, pThreads->capacity * sizeof(uint32_t));
            if (p == NULL)
            {
                succeeded = false;
                break;
            }

            pThreads->pItems = p;
        }

        pThreads->pItems[pThreads->size++] = threadList[i];
    }

    if (!succeeded && pThreads->pItems != NULL)
    {
        free(pThreads->pItems);
        pThreads->pItems = NULL;
    }

    mach_vm_deallocate(curtask, (mach_vm_address_t)threadList, threadCount * sizeof(thread_act_t));
    mach_port_deallocate(curtask, curthread);
    return succeeded;
}
#endif

//-------------------------------------------------------------------------
#if defined(_WIN32) || defined(__APPLE__)
static MH_STATUS Freeze(PFROZEN_THREADS pThreads, uint32_t pos, uint32_t action)
{
    MH_STATUS status = MH_OK;

    pThreads->pItems   = NULL;
    pThreads->capacity = 0;
    pThreads->size     = 0;
    if (!EnumerateThreads(pThreads))
    {
        status = MH_ERROR_MEMORY_ALLOC;
    }
    else if (pThreads->pItems != NULL)
    {
        uint32_t i;
        for (i = 0; i < pThreads->size; ++i)
        {
#ifdef _WIN32
            HANDLE hThread = OpenThread(THREAD_ACCESS, FALSE, pThreads->pItems[i]);
            if (hThread != NULL)
            {
                SuspendThread(hThread);
                ProcessThreadIPs(hThread, pos, action);
                CloseHandle(hThread);
            }
#else
            thread_act_t hThread = pThreads->pItems[i];
            thread_suspend(hThread);
            ProcessThreadIPs(hThread, pos, action);
#endif
        }
    }

    return status;
}
#endif

//-------------------------------------------------------------------------
#if defined(_WIN32) || defined(__APPLE__)
static void Unfreeze(PFROZEN_THREADS pThreads)
{
    if (pThreads->pItems != NULL)
    {
#ifdef __APPLE__
        task_t curtask = mach_thread_self();
#endif
        uint32_t i;
        for (i = 0; i < pThreads->size; ++i)
        {
#ifdef _WIN32
            HANDLE hThread = OpenThread(THREAD_ACCESS, FALSE, pThreads->pItems[i]);
            if (hThread != NULL)
            {
                ResumeThread(hThread);
                CloseHandle(hThread);
            }
#else
            thread_act_t hThread = pThreads->pItems[i];
            thread_resume(hThread);
            mach_port_deallocate(curtask, hThread);
#endif
        }

#ifdef _WIN32
        HeapFree(g_hHeap, 0, pThreads->pItems);
#else
        free(pThreads->pItems);
#endif
    }
}
#endif

//-------------------------------------------------------------------------
static MH_STATUS EnableHookLL(uint32_t pos, bool enable)
{
    PHOOK_ENTRY pHook = &g_hooks.pItems[pos];
    size_t patchSize      = sizeof(JMP_REL);
    uint8_t *pPatchTarget = (uint8_t *)pHook->pTarget;
#ifdef _WIN32
    unsigned long oldProtect;
#endif

    if (pHook->patchAbove)
    {
        pPatchTarget -= sizeof(JMP_REL);
        patchSize    += sizeof(JMP_REL_SHORT);
    }

#ifdef _WIN32
    if (!VirtualProtect(pPatchTarget, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect))
        return MH_ERROR_MEMORY_PROTECT;
#else
    uintptr_t uiMemory = (uintptr_t)pPatchTarget, diff = uiMemory % (uintptr_t)sysconf(_SC_PAGESIZE);
    if (mprotect((void *)(uiMemory - diff), diff + patchSize, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return MH_ERROR_MEMORY_PROTECT;
#endif

    if (enable)
    {
        PJMP_REL pJmp = (PJMP_REL)pPatchTarget;
        pJmp->opcode = 0xE9;
        pJmp->operand = (uint32_t)((uint8_t *)pHook->pDetour - (pPatchTarget + sizeof(JMP_REL)));

        if (pHook->patchAbove)
        {
            PJMP_REL_SHORT pShortJmp = (PJMP_REL_SHORT)pHook->pTarget;
            pShortJmp->opcode = 0xEB;
            pShortJmp->operand = (uint8_t)(0 + sizeof(JMP_REL) - (sizeof(JMP_REL_SHORT)));
        }
    }
    else
    {
        if (pHook->patchAbove)
            memcpy(pPatchTarget, pHook->backup, sizeof(JMP_REL) + sizeof(JMP_REL_SHORT));
        else
            memcpy(pPatchTarget, pHook->backup, sizeof(JMP_REL));
    }

#ifdef _WIN32
    VirtualProtect(pPatchTarget, patchSize, oldProtect, &oldProtect);

    // Just-in-case measure.
    FlushInstructionCache(GetCurrentProcess(), pPatchTarget, patchSize);
#else
    mprotect((void *)(uiMemory - diff), diff + patchSize, PROT_READ | PROT_EXEC);
#endif

    pHook->isEnabled   = enable;
    pHook->queueEnable = enable;

    return MH_OK;
}

//-------------------------------------------------------------------------
static MH_STATUS EnableAllHooksLL(bool enable)
{
    MH_STATUS status = MH_OK;
    uint32_t i, first = INVALID_HOOK_POS;

    for (i = 0; i < g_hooks.size; ++i)
    {
        if (g_hooks.pItems[i].isEnabled != enable)
        {
            first = i;
            break;
        }
    }

    if (first != INVALID_HOOK_POS)
    {
#if defined(_WIN32) || defined(__APPLE__)
        FROZEN_THREADS threads;
        status = Freeze(&threads, ALL_HOOKS_POS, enable ? ACTION_ENABLE : ACTION_DISABLE);
        if (status == MH_OK)
        {
#endif

            for (i = first; i < g_hooks.size; ++i)
            {
                if (g_hooks.pItems[i].isEnabled != enable)
                {
                    status = EnableHookLL(i, enable);
                    if (status != MH_OK)
                        break;
                }
            }

#if defined(_WIN32) || defined(__APPLE__)
            Unfreeze(&threads);
        }
#endif
    }

    return status;
}

//-------------------------------------------------------------------------
static void EnterSpinLock(void)
{
    size_t spinCount = 0;

    // Wait until the flag is FALSE.
#ifdef _WIN32
    while (InterlockedCompareExchange(&g_isLocked, TRUE, FALSE) != FALSE)
#else
    while (__sync_val_compare_and_swap(&g_isLocked, false, true) != true)
#endif
    {
        // No need to generate a memory barrier here, since InterlockedCompareExchange()
        // generates a full memory barrier itself.

        // Prevent the loop from being too busy.
        if (spinCount < 32)
#ifdef _WIN32
            Sleep(0);
#else
            usleep(0);
#endif
        else
#ifdef _WIN32
            Sleep(1);
#else
            usleep(1);
#endif

        spinCount++;
    }
}

//-------------------------------------------------------------------------
static void LeaveSpinLock(void)
{
    // No need to generate a memory barrier here, since InterlockedExchange()
    // generates a full memory barrier itself.

#ifdef _WIN32
    InterlockedExchange(&g_isLocked, FALSE);
#else
    (void)__sync_lock_test_and_set(&g_isLocked, false);
#endif
}

//-------------------------------------------------------------------------
MH_STATUS MH_API MH_Initialize(void)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_initCounter == 0)
    {
#ifdef _WIN32
        g_hHeap = HeapCreate(0, 0, 0);
        if (g_hHeap != NULL)
        {
#endif
            // Initialize the internal function buffer.
            InitializeBuffer();
#ifdef _WIN32
        }
        else
        {
            status = MH_ERROR_MEMORY_ALLOC;
        }
#endif
    }

    if (status == MH_OK)
    {
        ++g_initCounter;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS MH_API MH_Uninitialize(void)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_initCounter == 0)
    {
        return MH_ERROR_NOT_INITIALIZED;
    }

    if (g_initCounter == 1)
    {
        status = EnableAllHooksLL(false);
        if (status == MH_OK)
        {
            // Free the internal function buffer.

            // HeapFree is actually not required, but some tools detect a false
            // memory leak without HeapFree.

            UninitializeBuffer();

#ifdef _WIN32
            HeapFree(g_hHeap, 0, g_hooks.pItems);
            HeapDestroy(g_hHeap);

            g_hHeap = NULL;
#else
            free(g_hooks.pItems);
#endif

            g_hooks.pItems   = NULL;
            g_hooks.capacity = 0;
            g_hooks.size     = 0;
        }
    }

    if (status == MH_OK)
    {
        --g_initCounter;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS MH_API MH_CreateHook(void *pTarget, void *pDetour, void ** ppOriginal)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

#ifdef _WIN32
    if (g_hHeap != NULL)
    {
#endif
        if (IsExecutableAddress(pTarget) && IsExecutableAddress(pDetour))
        {
            uint32_t pos = FindHookEntry(pTarget);
            if (pos == INVALID_HOOK_POS)
            {
                void *pBuffer = AllocateBuffer(pTarget);
                if (pBuffer != NULL)
                {
                    TRAMPOLINE ct;

                    ct.pTarget     = pTarget;
                    ct.pDetour     = pDetour;
                    ct.pTrampoline = pBuffer;
                    if (CreateTrampolineFunction(&ct))
                    {
                        PHOOK_ENTRY pHook = AddHookEntry();
                        if (pHook != NULL)
                        {
                            pHook->pTarget     = ct.pTarget;
#ifdef MH_X86_64
                            pHook->pDetour     = ct.pRelay;
#else
                            pHook->pDetour     = ct.pDetour;
#endif
                            pHook->pTrampoline = ct.pTrampoline;
                            pHook->patchAbove  = ct.patchAbove;
                            pHook->isEnabled   = false;
                            pHook->queueEnable = false;
                            pHook->nIP         = ct.nIP;
                            memcpy(pHook->oldIPs, ct.oldIPs, ARRAYSIZE(ct.oldIPs));
                            memcpy(pHook->newIPs, ct.newIPs, ARRAYSIZE(ct.newIPs));

                            // Back up the target function.

                            if (ct.patchAbove)
                            {
                                memcpy(
                                    pHook->backup,
                                    (uint8_t *)pTarget - sizeof(JMP_REL),
                                    sizeof(JMP_REL) + sizeof(JMP_REL_SHORT));
                            }
                            else
                            {
                                memcpy(pHook->backup, pTarget, sizeof(JMP_REL));
                            }

                            if (ppOriginal != NULL)
                                *ppOriginal = pHook->pTrampoline;
                        }
                        else
                        {
                            status = MH_ERROR_MEMORY_ALLOC;
                        }
                    }
                    else
                    {
                        status = MH_ERROR_UNSUPPORTED_FUNCTION;
                    }

                    if (status != MH_OK)
                    {
                        FreeBuffer(pBuffer);
                    }
                }
                else
                {
                    status = MH_ERROR_MEMORY_ALLOC;
                }
            }
            else
            {
                status = MH_ERROR_ALREADY_CREATED;
            }
        }
        else
        {
            status = MH_ERROR_NOT_EXECUTABLE;
        }
#ifdef _WIN32
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }
#endif

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS MH_API MH_RemoveHook(void *pTarget)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

#ifdef _WIN32
    if (g_hHeap != NULL)
    {
#endif
        uint32_t pos = FindHookEntry(pTarget);
        if (pos != INVALID_HOOK_POS)
        {
            if (g_hooks.pItems[pos].isEnabled)
            {
#if defined(_WIN32) || defined(__APPLE__)
                FROZEN_THREADS threads;
                status = Freeze(&threads, pos, ACTION_DISABLE);
                if (status == MH_OK)
                {
#endif

                    status = EnableHookLL(pos, true);

#if defined(_WIN32) || defined(__APPLE__)
                    Unfreeze(&threads);
                }
#endif
            }

            if (status == MH_OK)
            {
                FreeBuffer(g_hooks.pItems[pos].pTrampoline);
                DeleteHookEntry(pos);
            }
        }
        else
        {
            status = MH_ERROR_NOT_CREATED;
        }
#ifdef _WIN32
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }
#endif

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS MH_API MH_IsHookEnabled(void *pTarget)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

#ifdef _WIN32
    if (g_hHeap != NULL)
    {
#endif
        uint32_t pos = FindHookEntry(pTarget);
        if (pos != INVALID_HOOK_POS)
        {
            status = g_hooks.pItems[pos].isEnabled ? MH_HOOK_ENABLED : MH_HOOK_DISABLED;
        }
        else
        {
            status = MH_ERROR_NOT_CREATED;
        }
#ifdef _WIN32
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }
#endif

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
static MH_STATUS EnableHook(void *pTarget, bool enable)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

#ifdef _WIN32
    if (g_hHeap != NULL)
    {
#endif
        if (pTarget == MH_ALL_HOOKS)
        {
            status = EnableAllHooksLL(enable);
        }
        else
        {
            uint32_t pos = FindHookEntry(pTarget);
            if (pos != INVALID_HOOK_POS)
            {
                if (g_hooks.pItems[pos].isEnabled != enable)
                {
#if defined(_WIN32) || defined(__APPLE__)
                    FROZEN_THREADS threads;
                    status = Freeze(&threads, pos, ACTION_ENABLE);
                    if (status == MH_OK)
                    {
#endif

                        status = EnableHookLL(pos, enable);

#if defined(_WIN32) || defined(__APPLE__)
                        Unfreeze(&threads);
                    }
#endif
                }
                else
                {
                    status = enable ? MH_ERROR_ENABLED : MH_ERROR_DISABLED;
                }
            }
            else
            {
                status = MH_ERROR_NOT_CREATED;
            }
        }
#ifdef _WIN32
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }
#endif

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS MH_API MH_EnableHook(void *pTarget)
{
    return EnableHook(pTarget, true);
}

//-------------------------------------------------------------------------
MH_STATUS MH_API MH_DisableHook(void *pTarget)
{
    return EnableHook(pTarget, false);
}

//-------------------------------------------------------------------------
static MH_STATUS QueueHook(void *pTarget, bool queueEnable)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

#ifdef _WIN32
    if (g_hHeap != NULL)
    {
#endif
        if (pTarget == MH_ALL_HOOKS)
        {
            uint32_t i;
            for (i = 0; i < g_hooks.size; ++i)
                g_hooks.pItems[i].queueEnable = queueEnable;
        }
        else
        {
            uint32_t pos = FindHookEntry(pTarget);
            if (pos != INVALID_HOOK_POS)
            {
                g_hooks.pItems[pos].queueEnable = queueEnable;
            }
            else
            {
                status = MH_ERROR_NOT_CREATED;
            }
        }
#ifdef _WIN32
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }
#endif

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS MH_API MH_QueueEnableHook(void *pTarget)
{
    return QueueHook(pTarget, true);
}

//-------------------------------------------------------------------------
MH_STATUS MH_API MH_QueueDisableHook(void *pTarget)
{
    return QueueHook(pTarget, false);
}

//-------------------------------------------------------------------------
MH_STATUS MH_API MH_ApplyQueued(void)
{
    MH_STATUS status = MH_OK;
    uint32_t i, first = INVALID_HOOK_POS;

    EnterSpinLock();

#ifdef _WIN32
    if (g_hHeap != NULL)
    {
#endif
        for (i = 0; i < g_hooks.size; ++i)
        {
            if (g_hooks.pItems[i].isEnabled != g_hooks.pItems[i].queueEnable)
            {
                first = i;
                break;
            }
        }

        if (first != INVALID_HOOK_POS)
        {
#if defined(_WIN32) || defined(__APPLE__)
            FROZEN_THREADS threads;
            status = Freeze(&threads, ALL_HOOKS_POS, ACTION_APPLY_QUEUED);
            if (status == MH_OK)
            {
#endif

                for (i = first; i < g_hooks.size; ++i)
                {
                    PHOOK_ENTRY pHook = &g_hooks.pItems[i];
                    if (pHook->isEnabled != pHook->queueEnable)
                    {
                        status = EnableHookLL(i, pHook->queueEnable);
                        if (status != MH_OK)
                            break;
                    }
                }

#if defined(_WIN32) || defined(__APPLE__)
                Unfreeze(&threads);
            }
#endif
        }
#ifdef _WIN32
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }
#endif

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS MH_API MH_CreateHookApiEx(
    const wchar_t *pszModule, const char *pszProcName, void *pDetour,
    void **ppOriginal, void **ppTarget)
{
#ifdef _WIN32
    HMODULE hModule;
    void *pTarget;

    hModule = GetModuleHandleW(pszModule);
    if (hModule == NULL)
        return MH_ERROR_MODULE_NOT_FOUND;

    pTarget = (void *)GetProcAddress(hModule, pszProcName);
#else
    void *hModule;
    void *pTarget;

    setlocale(LC_CTYPE, "");
    size_t len = wcstombs(NULL, pszModule, 0);
    if (len == (size_t)-1)
        return MH_ERROR_MEMORY_ALLOC;

    ++len;
    char *pszModuleMB = (char *)malloc(len);
    if (pszModuleMB == NULL)
        return MH_ERROR_MEMORY_ALLOC;

    len = wcstombs(pszModuleMB, pszModule, len);
    if (len == (size_t)-1)
    {
        free(pszModuleMB);
        return MH_ERROR_MEMORY_ALLOC;
    }

    hModule = dlopen(pszModuleMB, RTLD_LAZY | RTLD_NOLOAD);
    free(pszModuleMB);
    if (hModule == NULL)
        return MH_ERROR_MODULE_NOT_FOUND;

    pTarget = dlsym(hModule, pszProcName);
    dlclose(hModule);
#endif
    if (pTarget == NULL)
        return MH_ERROR_FUNCTION_NOT_FOUND;

    if(ppTarget != NULL)
        *ppTarget = pTarget;

    return MH_CreateHook(pTarget, pDetour, ppOriginal);
}

//-------------------------------------------------------------------------
MH_STATUS MH_API MH_CreateHookApi(
    const wchar_t *pszModule, const char *pszProcName, void *pDetour, void ** ppOriginal)
{
   return MH_CreateHookApiEx(pszModule, pszProcName, pDetour, ppOriginal, NULL);
}

//-------------------------------------------------------------------------
const char *MH_API MH_StatusToString(MH_STATUS status)
{
#define MH_ST2STR(x)    \
    case x:             \
        return #x;

    switch (status) {
        MH_ST2STR(MH_UNKNOWN)
        MH_ST2STR(MH_OK)
        MH_ST2STR(MH_ERROR_NOT_INITIALIZED)
        MH_ST2STR(MH_ERROR_ALREADY_CREATED)
        MH_ST2STR(MH_ERROR_NOT_CREATED)
        MH_ST2STR(MH_ERROR_ENABLED)
        MH_ST2STR(MH_ERROR_DISABLED)
        MH_ST2STR(MH_ERROR_NOT_EXECUTABLE)
        MH_ST2STR(MH_ERROR_UNSUPPORTED_FUNCTION)
        MH_ST2STR(MH_ERROR_MEMORY_ALLOC)
        MH_ST2STR(MH_ERROR_MEMORY_PROTECT)
        MH_ST2STR(MH_ERROR_MODULE_NOT_FOUND)
        MH_ST2STR(MH_ERROR_FUNCTION_NOT_FOUND)
    }

#undef MH_ST2STR

    return "(unknown)";
}
