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
#else
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>
#endif

#include <stdint.h>
#include <string.h>
#include "buffer.h"

// Size of each memory block. (= page size of VirtualAlloc)
#define MEMORY_BLOCK_SIZE 0x1000

// Max range for seeking a memory block. (= 1024MB)
#define MAX_MEMORY_RANGE 0x40000000

// Memory protection flags to check the executable address.
#define PAGE_EXECUTE_FLAGS \
    (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)

// Memory slot.
typedef union _MEMORY_SLOT
{
    union _MEMORY_SLOT *pNext;
    uint8_t buffer[MEMORY_SLOT_SIZE];
} MEMORY_SLOT, *PMEMORY_SLOT;

// Memory block info. Placed at the head of each block.
typedef struct _MEMORY_BLOCK
{
    struct _MEMORY_BLOCK *pNext;
    PMEMORY_SLOT pFree;         // First element of the free slot list.
    uint32_t usedCount;
} MEMORY_BLOCK, *PMEMORY_BLOCK;

//-------------------------------------------------------------------------
// Global Variables:
//-------------------------------------------------------------------------

// First element of the memory block list.
PMEMORY_BLOCK g_pMemoryBlocks;

//-------------------------------------------------------------------------
void InitializeBuffer(void)
{
    // Nothing to do for now.
}

//-------------------------------------------------------------------------
void UninitializeBuffer(void)
{
    PMEMORY_BLOCK pBlock = g_pMemoryBlocks;
    g_pMemoryBlocks = NULL;

    while (pBlock)
    {
        PMEMORY_BLOCK pNext = pBlock->pNext;
#ifdef _WIN32
        VirtualFree(pBlock, 0, MEM_RELEASE);
#else
        munmap(pBlock, MEMORY_BLOCK_SIZE);
#endif
        pBlock = pNext;
    }
}

//-------------------------------------------------------------------------
#if defined(MH_X86_64) && defined(_WIN32)
static LPVOID FindPrevFreeRegion(LPVOID pAddress, LPVOID pMinAddr, DWORD dwAllocationGranularity)
{
    ULONG_PTR tryAddr = (ULONG_PTR)pAddress;

    // Round down to the allocation granularity.
    tryAddr -= tryAddr % dwAllocationGranularity;

    // Start from the previous allocation granularity multiply.
    tryAddr -= dwAllocationGranularity;

    while (tryAddr >= (ULONG_PTR)pMinAddr)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPVOID)tryAddr, &mbi, sizeof(mbi)) == 0)
            break;

        if (mbi.State == MEM_FREE)
            return (LPVOID)tryAddr;

        if ((ULONG_PTR)mbi.AllocationBase < dwAllocationGranularity)
            break;

        tryAddr = (ULONG_PTR)mbi.AllocationBase - dwAllocationGranularity;
    }

    return NULL;
}
#endif

//-------------------------------------------------------------------------
#if defined(MH_X86_64) && defined(_WIN32)
static LPVOID FindNextFreeRegion(LPVOID pAddress, LPVOID pMaxAddr, DWORD dwAllocationGranularity)
{
    ULONG_PTR tryAddr = (ULONG_PTR)pAddress;

    // Round down to the allocation granularity.
    tryAddr -= tryAddr % dwAllocationGranularity;

    // Start from the next allocation granularity multiply.
    tryAddr += dwAllocationGranularity;

    while (tryAddr <= (ULONG_PTR)pMaxAddr)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPVOID)tryAddr, &mbi, sizeof(mbi)) == 0)
            break;

        if (mbi.State == MEM_FREE)
            return (LPVOID)tryAddr;

        tryAddr = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;

        // Round up to the next allocation granularity.
        tryAddr += dwAllocationGranularity - 1;
        tryAddr -= tryAddr % dwAllocationGranularity;
    }

    return NULL;
}
#endif

//-------------------------------------------------------------------------
static PMEMORY_BLOCK GetMemoryBlock(void *pOrigin)
{
    PMEMORY_BLOCK pBlock;
#ifdef MH_X86_64
    uintptr_t minAddr;
    uintptr_t maxAddr;

#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    DWORD dwAllocationGranularity = si.dwAllocationGranularity;
    minAddr = (uintptr_t)si.lpMinimumApplicationAddress;
    maxAddr = (uintptr_t)si.lpMaximumApplicationAddress;
#else
    // Taken from coreclr sysinfo.cpp
    long dwAllocationGranularity = sysconf(_SC_PAGESIZE);
    maxAddr = (uintptr_t)(1ull << 47);
    minAddr = (uintptr_t)dwAllocationGranularity;
#endif

    // pOrigin ± 512MB
    if ((uintptr_t)pOrigin > MAX_MEMORY_RANGE && minAddr < (uintptr_t)pOrigin - MAX_MEMORY_RANGE)
        minAddr = (uintptr_t)pOrigin - MAX_MEMORY_RANGE;

    if (maxAddr > (uintptr_t)pOrigin + MAX_MEMORY_RANGE)
        maxAddr = (uintptr_t)pOrigin + MAX_MEMORY_RANGE;

    // Make room for MEMORY_BLOCK_SIZE bytes.
    maxAddr -= MEMORY_BLOCK_SIZE - 1;
#else
    (void)pOrigin;
#endif

    // Look the registered blocks for a reachable one.
    for (pBlock = g_pMemoryBlocks; pBlock != NULL; pBlock = pBlock->pNext)
    {
#ifdef MH_X86_64
        // Ignore the blocks too far.
        if ((uintptr_t)pBlock < minAddr || (uintptr_t)pBlock >= maxAddr)
            continue;
#endif
        // The block has at least one unused slot.
        if (pBlock->pFree != NULL)
            return pBlock;
    }

#ifdef MH_X86_64
    // Alloc a new block above if not found.
    {
#ifdef _WIN32
        void *pAlloc = pOrigin;
        while ((uintptr_t)pAlloc >= minAddr)
        {
            pAlloc = FindPrevFreeRegion(pAlloc, (void *)minAddr, dwAllocationGranularity);
            if (pAlloc == NULL)
                break;

            pBlock = (PMEMORY_BLOCK)VirtualAlloc(
                pAlloc, MEMORY_BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (pBlock != NULL)
                break;
        }
#else
        // Let the kernel find us a block before the address given
        pBlock = (PMEMORY_BLOCK)mmap(
            (void *)minAddr, MEMORY_BLOCK_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);

        intptr_t diff = (intptr_t)pBlock - (intptr_t)pOrigin;
        if (diff < INT_MIN || diff > INT_MAX)
        {
            munmap(pBlock, MEMORY_BLOCK_SIZE);
            pBlock = NULL;
        }
#endif
    }

    // Alloc a new block below if not found.
    if (pBlock == NULL)
    {
#ifdef _WIN32
        void *pAlloc = pOrigin;
        while ((uintptr_t)pAlloc <= maxAddr)
        {
            pAlloc = FindNextFreeRegion(pAlloc, (void *)maxAddr, dwAllocationGranularity);
            if (pAlloc == NULL)
                break;

            pBlock = (PMEMORY_BLOCK)VirtualAlloc(
                pAlloc, MEMORY_BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

            if (pBlock != NULL)
                break;
        }
#else
        // Let the kernel find us a block after the address given
        pBlock = (PMEMORY_BLOCK)mmap(
            pOrigin, MEMORY_BLOCK_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);

        intptr_t diff = (intptr_t)pBlock - (intptr_t)pOrigin;
        if (diff > INT_MAX || diff < INT_MIN)
        {
            munmap(pBlock, MEMORY_BLOCK_SIZE);
            pBlock = NULL;
        }
#endif
    }
#else
    // In x86 mode, a memory block can be placed anywhere.
#ifdef _WIN32
    pBlock = (PMEMORY_BLOCK)VirtualAlloc(
        NULL, MEMORY_BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    pBlock = (PMEMORY_BLOCK)mmap(
        NULL, MEMORY_BLOCK_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
#endif
#endif

    if (pBlock != NULL)
    {
        // Build a linked list of all the slots.
        PMEMORY_SLOT pSlot = (PMEMORY_SLOT)pBlock + 1;
        pBlock->pFree = NULL;
        pBlock->usedCount = 0;
        do
        {
            pSlot->pNext = pBlock->pFree;
            pBlock->pFree = pSlot;
            pSlot++;
        } while ((uintptr_t)pSlot - (uintptr_t)pBlock <= MEMORY_BLOCK_SIZE - MEMORY_SLOT_SIZE);

        pBlock->pNext = g_pMemoryBlocks;
        g_pMemoryBlocks = pBlock;
    }

    return pBlock;
}

//-------------------------------------------------------------------------
void *AllocateBuffer(void *pOrigin)
{
    PMEMORY_SLOT  pSlot;
    PMEMORY_BLOCK pBlock = GetMemoryBlock(pOrigin);
    if (pBlock == NULL)
        return NULL;

    // Remove an unused slot from the list.
    pSlot = pBlock->pFree;
    pBlock->pFree = pSlot->pNext;
    pBlock->usedCount++;
#ifdef _DEBUG
    // Fill the slot with INT3 for debugging.
    memset(pSlot, 0xCC, sizeof(MEMORY_SLOT));
#endif
    return pSlot;
}

//-------------------------------------------------------------------------
void FreeBuffer(void *pBuffer)
{
    PMEMORY_BLOCK pBlock = g_pMemoryBlocks;
    PMEMORY_BLOCK pPrev = NULL;
    uintptr_t pTargetBlock = ((uintptr_t)pBuffer / MEMORY_BLOCK_SIZE) * MEMORY_BLOCK_SIZE;

    while (pBlock != NULL)
    {
        if ((uintptr_t)pBlock == pTargetBlock)
        {
            PMEMORY_SLOT pSlot = (PMEMORY_SLOT)pBuffer;
#ifdef _DEBUG
            // Clear the released slot for debugging.
            memset(pSlot, 0x00, sizeof(MEMORY_SLOT));
#endif
            // Restore the released slot to the list.
            pSlot->pNext = pBlock->pFree;
            pBlock->pFree = pSlot;
            pBlock->usedCount--;

            // Free if unused.
            if (pBlock->usedCount == 0)
            {
                if (pPrev)
                    pPrev->pNext = pBlock->pNext;
                else
                    g_pMemoryBlocks = pBlock->pNext;

#ifdef _WIN32
                VirtualFree(pBlock, 0, MEM_RELEASE);
#else
                munmap(pBlock, MEMORY_BLOCK_SIZE);
#endif
            }

            break;
        }

        pPrev = pBlock;
        pBlock = pBlock->pNext;
    }
}

//-------------------------------------------------------------------------
bool IsExecutableAddress(void *pAddress)
{
#if defined(_WIN32)
    MEMORY_BASIC_INFORMATION mi;
    VirtualQuery(pAddress, &mi, sizeof(mi));

    return (mi.State == MEM_COMMIT && (mi.Protect & PAGE_EXECUTE_FLAGS));
#elif defined(__APPLE__)
    mach_vm_address_t address = (mach_vm_address_t)pAddress;
    mach_vm_size_t vmsize = 0;
    vm_region_flavor_t flavor = VM_REGION_BASIC_INFO_64;
    vm_region_basic_info_data_64_t info = { 0 };
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    memory_object_name_t object = MACH_PORT_NULL;

    kern_return_t status = mach_vm_region(mach_task_self(), &address, &vmsize, flavor,
        (vm_region_info_t)&info, &info_count, &object);
    return status == KERN_SUCCESS && (info.protection & VM_PROT_EXECUTE);
#else
    uintptr_t address = (uintptr_t)pAddress;
    char line[BUFSIZ] = { 0 };

    FILE *file = fopen("/proc/self/maps", "r");
    if (file == NULL)
        return false;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        uint64_t start = 0, end = 0;
        char prot[5] = { 0 };
        if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %4[rwxsp-]", &start, &end, prot) == 3 && start <= address && end >= address)
        {
            fclose(file);
            return prot[2] == 'x';
        }
    }

    fclose(file);
    return false;
#endif
}
