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

#include "architecture.h"

#if defined(_MSC_VER) && !defined(MINHOOK_DISABLE_INTRINSICS)
    #define ALLOW_INTRINSICS
    #include <intrin.h>
#endif

#ifndef ARRAYSIZE
    #define ARRAYSIZE(A) (sizeof(A)/sizeof((A)[0]))
#endif

#ifdef MH_X86_64
    #include "hde/hde64.h"
    typedef hde64s HDE;
    #define HDE_DISASM(code, hs) hde64_disasm(code, hs)
#else
    #include "hde/hde32.h"
    typedef hde32s HDE;
    #define HDE_DISASM(code, hs) hde32_disasm(code, hs)
#endif

#include "trampoline.h"
#include "buffer.h"
#include <string.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

// Maximum size of a trampoline function.
#ifdef MH_X86_64
    #define TRAMPOLINE_MAX_SIZE (MEMORY_SLOT_SIZE - sizeof(JMP_ABS))
#else
    #define TRAMPOLINE_MAX_SIZE MEMORY_SLOT_SIZE
#endif

//-------------------------------------------------------------------------
static bool IsCodePadding(uint8_t *pInst, uint32_t size)
{
    uint32_t i;

    if (pInst[0] != 0x00 && pInst[0] != 0x90 && pInst[0] != 0xCC)
        return false;

    for (i = 1; i < size; ++i)
    {
        if (pInst[i] != pInst[0])
            return false;
    }
    return true;
}

//-------------------------------------------------------------------------
bool CreateTrampolineFunction(PTRAMPOLINE ct)
{
#ifdef MH_X86_64
    CALL_ABS call = {
        0xFF, 0x15, 0x00000002, // FF15 00000002: CALL [RIP+8]
        0xEB, 0x08,             // EB 08:         JMP +10
        0x0000000000000000ULL   // Absolute destination address
    };
    JMP_ABS jmp = {
        0xFF, 0x25, 0x00000000, // FF25 00000000: JMP [RIP+6]
        0x0000000000000000ULL   // Absolute destination address
    };
    JCC_ABS jcc = {
        0x70, 0x0E,             // 7* 0E:         J** +16
        0xFF, 0x25, 0x00000000, // FF25 00000000: JMP [RIP+6]
        0x0000000000000000ULL   // Absolute destination address
    };
#else
    CALL_REL call = {
        0xE8,                   // E8 xxxxxxxx: CALL +5+xxxxxxxx
        0x00000000              // Relative destination address
    };
    JMP_REL jmp = {
        0xE9,                   // E9 xxxxxxxx: JMP +5+xxxxxxxx
        0x00000000              // Relative destination address
    };
    JCC_REL jcc = {
        0x0F, 0x80,             // 0F8* xxxxxxxx: J** +6+xxxxxxxx
        0x00000000              // Relative destination address
    };
#endif

    uint8_t   oldPos   = 0;
    uint8_t   newPos   = 0;
    uintptr_t jmpDest  = 0;     // Destination address of an internal jump.
    bool      finished = false; // Is the function completed?
#ifdef MH_X86_64
    uint8_t   instBuf[16];
#endif

    ct->patchAbove = false;
    ct->nIP        = 0;

    do
    {
        HDE       hs;
        uint32_t  copySize;
        void     *pCopySrc;
        uintptr_t pOldInst = (uintptr_t)ct->pTarget     + oldPos;
        uintptr_t pNewInst = (uintptr_t)ct->pTrampoline + newPos;

        copySize = HDE_DISASM((void *)pOldInst, &hs);
        if (hs.flags & F_ERROR)
            return false;

        pCopySrc = (void *)pOldInst;
        if (oldPos >= sizeof(JMP_REL))
        {
            // The trampoline function is long enough.
            // Complete the function with the jump to the target function.
#ifdef MH_X86_64
            jmp.address = pOldInst;
#else
            jmp.operand = (uint32_t)(pOldInst - (pNewInst + sizeof(jmp)));
#endif
            pCopySrc = &jmp;
            copySize = sizeof(jmp);

            finished = true;
        }
#ifdef MH_X86_64
        else if ((hs.modrm & 0xC7) == 0x05)
        {
            // Instructions using RIP relative addressing. (ModR/M = 00???101B)

            // Modify the RIP relative address.
            uint32_t *pRelAddr;

            // Avoid using memcpy to reduce the footprint.
#ifndef ALLOW_INTRINSICS
            memcpy(instBuf, (uint8_t *)pOldInst, copySize);
#else
            __movsb(instBuf, (uint8_t *)pOldInst, copySize);
#endif
            pCopySrc = instBuf;

            // Relative address is stored at (instruction length - immediate value length - 4).
            pRelAddr = (uint32_t *)(instBuf + hs.len - ((hs.flags & 0x3C) >> 2) - 4);
            *pRelAddr
                = (uint32_t)((pOldInst + hs.len + (int32_t)hs.disp.disp32) - (pNewInst + hs.len));

            // Complete the function if JMP (FF /4).
            if (hs.opcode == 0xFF && hs.modrm_reg == 4)
                finished = true;
        }
#endif
        else if (hs.opcode == 0xE8)
        {
            // Direct relative CALL
            uintptr_t dest = pOldInst + hs.len + (int32_t)hs.imm.imm32;
#ifdef MH_X86_64
            call.address = dest;
#else
            switch (*(uint32_t *)dest)
            {
            // mov eax [esp]
            // ret
            case 0xC324048B:
                call.opcode = 0xB8;
                call.operand = pOldInst + 5;
                break;

            // mov ebx [esp]
            // ret
            case 0xC3241C8B:
                call.opcode = 0xBB;
                call.operand = pOldInst + 5;
                break;

            // mov ecx [esp]
            // ret
            case 0xC3240C8B:
                call.opcode = 0xB9;
                call.operand = pOldInst + 5;
                break;

            // mov edx [esp]
            // ret
            case 0xC324148B:
                call.opcode = 0xBA;
                call.operand = pOldInst + 5;
                break;

            // mov esi [esp]
            // ret
            case 0xC324348B:
                call.opcode = 0xBE;
                call.operand = pOldInst + 5;
                break;

            default:
                call.operand = (uint32_t)(dest - (pNewInst + sizeof(call)));
                break;
            }
#endif
            pCopySrc = &call;
            copySize = sizeof(call);
        }
        else if ((hs.opcode & 0xFD) == 0xE9)
        {
            // Direct relative JMP (EB or E9)
            uintptr_t dest = pOldInst + hs.len;

            if (hs.opcode == 0xEB) // isShort jmp
                dest += (int8_t)hs.imm.imm8;
            else
                dest += (int32_t)hs.imm.imm32;

            // Simply copy an internal jump.
            if ((uintptr_t)ct->pTarget <= dest
                && dest < ((uintptr_t)ct->pTarget + sizeof(JMP_REL)))
            {
                if (jmpDest < dest)
                    jmpDest = dest;
            }
            else
            {
#ifdef MH_X86_64
                jmp.address = dest;
#else
                jmp.operand = (uint32_t)(dest - (pNewInst + sizeof(jmp)));
#endif
                pCopySrc = &jmp;
                copySize = sizeof(jmp);

                // Exit the function if it is not in the branch.
                finished = (pOldInst >= jmpDest);
            }
        }
        else if ((hs.opcode & 0xF0) == 0x70
            || (hs.opcode & 0xFC) == 0xE0
            || (hs.opcode2 & 0xF0) == 0x80)
        {
            // Direct relative Jcc
            uintptr_t dest = pOldInst + hs.len;

            if ((hs.opcode & 0xF0) == 0x70      // Jcc
                || (hs.opcode & 0xFC) == 0xE0)  // LOOPNZ/LOOPZ/LOOP/JECXZ
                dest += (int8_t)hs.imm.imm8;
            else
                dest += (int32_t)hs.imm.imm32;

            // Simply copy an internal jump.
            if ((uintptr_t)ct->pTarget <= dest
                && dest < ((uintptr_t)ct->pTarget + sizeof(JMP_REL)))
            {
                if (jmpDest < dest)
                    jmpDest = dest;
            }
            else if ((hs.opcode & 0xFC) == 0xE0)
            {
                // LOOPNZ/LOOPZ/LOOP/JCXZ/JECXZ to the outside are not supported.
                return false;
            }
            else
            {
                uint8_t cond = ((hs.opcode != 0x0F ? hs.opcode : hs.opcode2) & 0x0F);
#ifdef MH_X86_64
                // Invert the condition in x64 mode to simplify the conditional jump logic.
                jcc.opcode  = 0x71 ^ cond;
                jcc.address = dest;
#else
                jcc.opcode1 = 0x80 | cond;
                jcc.operand = (uint32_t)(dest - (pNewInst + sizeof(jcc)));
#endif
                pCopySrc = &jcc;
                copySize = sizeof(jcc);
            }
        }
        else if ((hs.opcode & 0xFE) == 0xC2)
        {
            // RET (C2 or C3)

            // Complete the function if not in a branch.
            finished = (pOldInst >= jmpDest);
        }

        // Can't alter the instruction length in a branch.
        if (pOldInst < jmpDest && copySize != hs.len)
            return false;

        // Trampoline function is too large.
        if ((newPos + copySize) > TRAMPOLINE_MAX_SIZE)
            return false;

        // Trampoline function has too many instructions.
        if (ct->nIP >= ARRAYSIZE(ct->oldIPs))
            return false;

        ct->oldIPs[ct->nIP] = oldPos;
        ct->newIPs[ct->nIP] = newPos;
        ct->nIP++;

        // Avoid using memcpy to reduce the footprint.
#ifndef ALLOW_INTRINSICS
        memcpy((uint8_t *)ct->pTrampoline + newPos, pCopySrc, copySize);
#else
        __movsb((uint8_t *)ct->pTrampoline + newPos, (uint8_t *)pCopySrc, copySize);
#endif
        newPos += (uint8_t)copySize;
        oldPos += hs.len;
    }
    while (!finished);

    // Is there enough place for a long jump?
    if (oldPos < sizeof(JMP_REL)
        && !IsCodePadding((uint8_t *)ct->pTarget + oldPos, sizeof(JMP_REL) - oldPos))
    {
        // Is there enough place for a short jump?
        if (oldPos < sizeof(JMP_REL_SHORT)
            && !IsCodePadding((uint8_t *)ct->pTarget + oldPos, sizeof(JMP_REL_SHORT) - oldPos))
        {
            return false;
        }

        // Can we place the long jump above the function?
        if (!IsExecutableAddress((uint8_t *)ct->pTarget - sizeof(JMP_REL)))
            return false;

        if (!IsCodePadding((uint8_t *)ct->pTarget - sizeof(JMP_REL), sizeof(JMP_REL)))
            return false;

        ct->patchAbove = true;
    }

#ifdef MH_X86_64
    // Create a relay function.
    jmp.address = (uintptr_t)ct->pDetour;

    ct->pRelay = (uint8_t *)ct->pTrampoline + newPos;
    memcpy(ct->pRelay, &jmp, sizeof(jmp));
#endif

    return true;
}
