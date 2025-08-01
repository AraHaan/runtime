// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
//
// See makefile.inc.  During the build, this file is converted into a .inc
// file for inclusion by .asm files.  The #defines are converted into EQU's.
//
// Allow multiple inclusion.


#ifndef TARGET_AMD64
#error this file should only be used on an AMD64 platform
#endif // TARGET_AMD64

#include "../../inc/switches.h"

#ifndef ASMCONSTANTS_C_ASSERT
#define ASMCONSTANTS_C_ASSERT(cond)
#endif

#ifndef ASMCONSTANTS_RUNTIME_ASSERT
#define ASMCONSTANTS_RUNTIME_ASSERT(cond)
#endif


// Some constants are different in _DEBUG builds.  This macro factors out
// ifdefs from below.
#ifdef _DEBUG
#define DBG_FRE(dbg,fre) dbg
#else
#define DBG_FRE(dbg,fre) fre
#endif

#define DynamicHelperFrameFlags_Default     0
#define DynamicHelperFrameFlags_ObjectArg   1
#define DynamicHelperFrameFlags_ObjectArg2  2

#define ThisPtrRetBufPrecodeData__Target      0x00
ASMCONSTANTS_C_ASSERT(ThisPtrRetBufPrecodeData__Target == offsetof(ThisPtrRetBufPrecodeData, Target));

#define ASMCONSTANT_OFFSETOF_ASSERT(struct, member) \
ASMCONSTANTS_C_ASSERT(OFFSETOF__##struct##__##member == offsetof(struct, member));

#define ASMCONSTANT_SIZEOF_ASSERT(classname) \
ASMCONSTANTS_C_ASSERT(SIZEOF__##classname == sizeof(classname));

#define               ASM_ELEMENT_TYPE_R4                 0xC
ASMCONSTANTS_C_ASSERT(ASM_ELEMENT_TYPE_R4 == ELEMENT_TYPE_R4);

#define               ASM_ELEMENT_TYPE_R8                 0xD
ASMCONSTANTS_C_ASSERT(ASM_ELEMENT_TYPE_R8 == ELEMENT_TYPE_R8);


#define METHODDESC_REGNUM                    10
#define METHODDESC_REGISTER                 r10

#define PINVOKE_CALLI_TARGET_REGNUM          10
#define PINVOKE_CALLI_TARGET_REGISTER       r10

#define PINVOKE_CALLI_SIGTOKEN_REGNUM        11
#define PINVOKE_CALLI_SIGTOKEN_REGISTER     r11

#ifdef UNIX_AMD64_ABI
// rdi, rsi, rdx, rcx, r8, r9
#define SIZEOF_MAX_INT_ARG_SPILL  0x30
// xmm0...xmm7
#define SIZEOF_MAX_FP_ARG_SPILL             0x80
#else
// rcx, rdx, r8, r9
#define SIZEOF_MAX_OUTGOING_ARGUMENT_HOMES  0x20
// xmm0...xmm3
#define SIZEOF_MAX_FP_ARG_SPILL             0x40
#endif



#ifndef UNIX_AMD64_ABI
#define SIZEOF_CalleeSavedRegisters         0x40
ASMCONSTANTS_C_ASSERT(SIZEOF_CalleeSavedRegisters == sizeof(CalleeSavedRegisters));
#else
#define SIZEOF_CalleeSavedRegisters         0x30
ASMCONSTANTS_C_ASSERT(SIZEOF_CalleeSavedRegisters == sizeof(CalleeSavedRegisters));
#endif

#define FRAMETYPE_InlinedCallFrame 0x1
ASMCONSTANTS_C_ASSERT(FRAMETYPE_InlinedCallFrame == (int)FrameIdentifier::InlinedCallFrame)

#define               OFFSETOF__Frame____VFN_table  0

#define               OFFSETOF__Frame__m_Next       0x8
ASMCONSTANTS_C_ASSERT(OFFSETOF__Frame__m_Next
                    == offsetof(Frame, m_Next));

#define               SIZEOF__Frame                 0x10

#ifdef FEATURE_COMINTEROP
#define               SIZEOF__ComPrestubMethodFrame                 0x20
ASMCONSTANTS_C_ASSERT(SIZEOF__ComPrestubMethodFrame
                    == sizeof(ComPrestubMethodFrame));

#define               SIZEOF__ComMethodFrame                        0x20
ASMCONSTANTS_C_ASSERT(SIZEOF__ComMethodFrame
                    == sizeof(ComMethodFrame));

#endif // FEATURE_COMINTEROP

#define               OFFSETOF__Thread__m_fPreemptiveGCDisabled     0x04
ASMCONSTANTS_C_ASSERT(OFFSETOF__Thread__m_fPreemptiveGCDisabled
                    == offsetof(Thread, m_fPreemptiveGCDisabled));
#define Thread_m_fPreemptiveGCDisabled OFFSETOF__Thread__m_fPreemptiveGCDisabled

#define               OFFSETOF__Thread__m_pFrame                    0x08
ASMCONSTANTS_C_ASSERT(OFFSETOF__Thread__m_pFrame
                    == offsetof(Thread, m_pFrame));
#define Thread_m_pFrame OFFSETOF__Thread__m_pFrame

#define               OFFSETOF__RuntimeThreadLocals__ee_alloc_context 0
ASMCONSTANTS_C_ASSERT(OFFSETOF__RuntimeThreadLocals__ee_alloc_context == offsetof(RuntimeThreadLocals, alloc_context));

#define               OFFSETOF__ee_alloc_context__alloc_ptr 0x8
ASMCONSTANTS_C_ASSERT(OFFSETOF__ee_alloc_context__alloc_ptr == offsetof(ee_alloc_context, m_GCAllocContext) +
                                                               offsetof(gc_alloc_context, alloc_ptr));

#define               OFFSETOF__ee_alloc_context__combined_limit 0x0
ASMCONSTANTS_C_ASSERT(OFFSETOF__ee_alloc_context__combined_limit == offsetof(ee_alloc_context, m_CombinedLimit));

#define               OFFSETOF__ThreadExceptionState__m_pCurrentTracker 0x000
ASMCONSTANTS_C_ASSERT(OFFSETOF__ThreadExceptionState__m_pCurrentTracker
                    == offsetof(ThreadExceptionState, m_pCurrentTracker));


#define               OFFSETOF__DynamicHelperStubArgs__Constant1    0x0
ASMCONSTANTS_C_ASSERT(OFFSETOF__DynamicHelperStubArgs__Constant1
                    == offsetof(DynamicHelperStubArgs, Constant1));

#define               OFFSETOF__DynamicHelperStubArgs__Constant2    0x8
ASMCONSTANTS_C_ASSERT(OFFSETOF__DynamicHelperStubArgs__Constant2
                    == offsetof(DynamicHelperStubArgs, Constant2));

#define               OFFSETOF__DynamicHelperStubArgs__Helper    0x10
ASMCONSTANTS_C_ASSERT(OFFSETOF__DynamicHelperStubArgs__Helper
                    == offsetof(DynamicHelperStubArgs, Helper));

#define               OFFSETOF__GenericDictionaryDynamicHelperStubData__SecondIndir    0x0
ASMCONSTANTS_C_ASSERT(OFFSETOF__GenericDictionaryDynamicHelperStubData__SecondIndir
                    == offsetof(GenericDictionaryDynamicHelperStubData, SecondIndir));

#define               OFFSETOF__GenericDictionaryDynamicHelperStubData__LastIndir    0x4
ASMCONSTANTS_C_ASSERT(OFFSETOF__GenericDictionaryDynamicHelperStubData__LastIndir
                    == offsetof(GenericDictionaryDynamicHelperStubData, LastIndir));

#define               OFFSETOF__GenericDictionaryDynamicHelperStubData__SizeOffset    0x8
ASMCONSTANTS_C_ASSERT(OFFSETOF__GenericDictionaryDynamicHelperStubData__SizeOffset
                    == offsetof(GenericDictionaryDynamicHelperStubData, SizeOffset));

#define               OFFSETOF__GenericDictionaryDynamicHelperStubData__SlotOffset    0xc
ASMCONSTANTS_C_ASSERT(OFFSETOF__GenericDictionaryDynamicHelperStubData__SlotOffset
                    == offsetof(GenericDictionaryDynamicHelperStubData, SlotOffset));

#define               OFFSETOF__GenericDictionaryDynamicHelperStubData__HandleArgs    0x10
ASMCONSTANTS_C_ASSERT(OFFSETOF__GenericDictionaryDynamicHelperStubData__HandleArgs
                    == offsetof(GenericDictionaryDynamicHelperStubData, HandleArgs));

#ifdef FEATURE_INTERPRETER
#define               OFFSETOF__InstantiatedMethodDesc__m_pPerInstInfo    DBG_FRE(0x48, 0x20)
#else
#define               OFFSETOF__InstantiatedMethodDesc__m_pPerInstInfo    DBG_FRE(0x40, 0x18)
#endif // FEATURE_INTERPRETER
ASMCONSTANTS_C_ASSERT(OFFSETOF__InstantiatedMethodDesc__m_pPerInstInfo
                    == offsetof(InstantiatedMethodDesc, m_pPerInstInfo));


#define               OFFSETOF__MethodTable__m_dwFlags              0x00
ASMCONSTANTS_C_ASSERT(OFFSETOF__MethodTable__m_dwFlags
                    == offsetof(MethodTable, m_dwFlags));

#define               OFFSETOF__MethodTable__m_usComponentSize    0
ASMCONSTANTS_C_ASSERT(OFFSETOF__MethodTable__m_usComponentSize == offsetof(MethodTable, m_dwFlags));

#define               OFFSETOF__MethodTable__m_uBaseSize    0x04
ASMCONSTANTS_C_ASSERT(OFFSETOF__MethodTable__m_uBaseSize == offsetof(MethodTable, m_BaseSize));

#define               OFFSETOF__MethodTable__m_wNumInterfaces       0x0E
ASMCONSTANTS_C_ASSERT(OFFSETOF__MethodTable__m_wNumInterfaces
                    == offsetof(MethodTable, m_wNumInterfaces));

#define               OFFSETOF__MethodTable__m_pParentMethodTable   DBG_FRE(0x18, 0x10)
ASMCONSTANTS_C_ASSERT(OFFSETOF__MethodTable__m_pParentMethodTable
                    == offsetof(MethodTable, m_pParentMethodTable));

#define               OFFSETOF__MethodTable__m_pEEClass             DBG_FRE(0x30, 0x28)
ASMCONSTANTS_C_ASSERT(OFFSETOF__MethodTable__m_pEEClass
                    == offsetof(MethodTable, m_pEEClass));

#define               METHODTABLE_OFFSET_VTABLE          DBG_FRE(0x48, 0x40)
ASMCONSTANTS_C_ASSERT(METHODTABLE_OFFSET_VTABLE == sizeof(MethodTable));

#define               OFFSETOF__MethodTable__m_pPerInstInfo    DBG_FRE(0x38, 0x30)
ASMCONSTANTS_C_ASSERT(OFFSETOF__MethodTable__m_pPerInstInfo
                    == offsetof(MethodTable, m_pPerInstInfo));

#define               OFFSETOF__MethodTable__m_pInterfaceMap    DBG_FRE(0x40, 0x38)
ASMCONSTANTS_C_ASSERT(OFFSETOF__MethodTable__m_pInterfaceMap
                    == offsetof(MethodTable, m_pInterfaceMap));

#define               OFFSETOF__Object__m_pEEType   0
ASMCONSTANTS_C_ASSERT(OFFSETOF__Object__m_pEEType == offsetof(Object, m_pMethTab));

#define               OFFSETOF__Array__m_Length     0x8
ASMCONSTANTS_C_ASSERT(OFFSETOF__Array__m_Length == offsetof(ArrayBase, m_NumComponents));

#define MethodTable_VtableSlotsPerChunk     8
ASMCONSTANTS_C_ASSERT(MethodTable_VtableSlotsPerChunk == VTABLE_SLOTS_PER_CHUNK)

#define MethodTable_VtableSlotsPerChunkLog2 3
ASMCONSTANTS_C_ASSERT(MethodTable_VtableSlotsPerChunkLog2 == VTABLE_SLOTS_PER_CHUNK_LOG2)

#if defined(FEATURE_TYPEEQUIVALENCE)
#define               METHODTABLE_EQUIVALENCE_FLAGS 0x02000000
ASMCONSTANTS_C_ASSERT(METHODTABLE_EQUIVALENCE_FLAGS
                    == MethodTable::enum_flag_HasTypeEquivalence);
#else
#define               METHODTABLE_EQUIVALENCE_FLAGS 0x0
#endif

#define               MethodTable__enum_flag_ContainsGCPointers 0x01000000
ASMCONSTANTS_C_ASSERT(MethodTable__enum_flag_ContainsGCPointers
                    == MethodTable::enum_flag_ContainsGCPointers);

#define               OFFSETOF__InterfaceInfo_t__m_pMethodTable  0
ASMCONSTANTS_C_ASSERT(OFFSETOF__InterfaceInfo_t__m_pMethodTable
                    == offsetof(InterfaceInfo_t, m_pMethodTable));

#define               SIZEOF__InterfaceInfo_t   0x8
ASMCONSTANTS_C_ASSERT(SIZEOF__InterfaceInfo_t
                    == sizeof(InterfaceInfo_t));

ASMCONSTANTS_C_ASSERT(MethodTableAuxiliaryData::enum_flag_Initialized == 0x1);

#define                OFFSETOF__DynamicStaticsInfo__m_pMethodTable 0x10
ASMCONSTANTS_C_ASSERT(OFFSETOF__DynamicStaticsInfo__m_pMethodTable
                    == offsetof(DynamicStaticsInfo, m_pMethodTable));

#define                OFFSETOF__DynamicStaticsInfo__m_pNonGCStatics 0x8
ASMCONSTANTS_C_ASSERT(OFFSETOF__DynamicStaticsInfo__m_pNonGCStatics
                    == offsetof(DynamicStaticsInfo, m_pNonGCStatics));

#define                OFFSETOF__DynamicStaticsInfo__m_pGCStatics 0
ASMCONSTANTS_C_ASSERT(OFFSETOF__DynamicStaticsInfo__m_pGCStatics
                    == offsetof(DynamicStaticsInfo, m_pGCStatics));

#define               OFFSETOF__VASigCookie__pPInvokeILStub     0x8
ASMCONSTANTS_C_ASSERT(OFFSETOF__VASigCookie__pPInvokeILStub
                    == offsetof(VASigCookie, pPInvokeILStub));

#if defined(UNIX_AMD64_ABI) && !defined(HOST_WINDOWS)
// Expression is too complicated, is currently:
//     (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 +
//      /*XMM_SAVE_AREA32*/(2*2 + 1*2 + 2 + 4 + 2*2 + 4 + 2*2 + 4*2 + 16*8 + 16*16 + 1*96) + 26*16 + 8 + 8*5 +
//      /*XSTATE*/ + 8 + 8 +
//      /*XSTATE_AVX*/ 16*16 +
//      /*XSTATE_AVX512_KMASK*/ 8*8 +
//      /*XSTATE_AVX512_ZMM_H*/ 32*16 +
//      /*XSTATE_AVX512_ZMM*/ 64*16 +
//      /*XSTATE_APX*/ 8*16)
#define               SIZEOF__CONTEXT                 (3232)
#else
// Expression is too complicated, is currently:
//     (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + /*XMM_SAVE_AREA32*/(2*2 + 1*2 + 2 + 4 + 2*2 + 4 + 2*2 + 4*2 + 16*8 + 16*16 + 1*96) + 26*16 + 8 + 8*5)
#define               SIZEOF__CONTEXT                 (1232)
#endif
ASMCONSTANTS_C_ASSERT(SIZEOF__CONTEXT
                    == sizeof(CONTEXT));

#define               OFFSETOF__CONTEXT__ContextFlags (8*6)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__ContextFlags
                    == offsetof(CONTEXT, ContextFlags));

#define               OFFSETOF__CONTEXT__EFlags       (8*6 + 4*2 + 2*6)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__EFlags
                    == offsetof(CONTEXT, EFlags));

#define               OFFSETOF__CONTEXT__Rax          (8*6 + 4*2 + 2*6 + 4 + 8*6)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Rax
                    == offsetof(CONTEXT, Rax));

#define               OFFSETOF__CONTEXT__Rcx          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Rcx
                    == offsetof(CONTEXT, Rcx));

#define               OFFSETOF__CONTEXT__Rdx          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*2)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Rdx
                    == offsetof(CONTEXT, Rdx));

#define               OFFSETOF__CONTEXT__Rbx          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*3)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Rbx
                    == offsetof(CONTEXT, Rbx));

#define               OFFSETOF__CONTEXT__Rsp          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*4)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Rsp
                    == offsetof(CONTEXT, Rsp));

#define               OFFSETOF__CONTEXT__Rbp          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*5)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Rbp
                    == offsetof(CONTEXT, Rbp));

#define               OFFSETOF__CONTEXT__Rsi          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*6)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Rsi
                    == offsetof(CONTEXT, Rsi));

#define               OFFSETOF__CONTEXT__Rdi          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*7)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Rdi
                    == offsetof(CONTEXT, Rdi));

#define               OFFSETOF__CONTEXT__R8           (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*8)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__R8
                    == offsetof(CONTEXT, R8));

#define               OFFSETOF__CONTEXT__R9          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*9)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__R9
                    == offsetof(CONTEXT, R9));

#define               OFFSETOF__CONTEXT__R10          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*10)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__R10
                    == offsetof(CONTEXT, R10));

#define               OFFSETOF__CONTEXT__R11          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*11)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__R11
                    == offsetof(CONTEXT, R11));

#define               OFFSETOF__CONTEXT__R12          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*12)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__R12
                    == offsetof(CONTEXT, R12));

#define               OFFSETOF__CONTEXT__R13          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*13)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__R13
                    == offsetof(CONTEXT, R13));

#define               OFFSETOF__CONTEXT__R14          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*14)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__R14
                    == offsetof(CONTEXT, R14));

#define               OFFSETOF__CONTEXT__R15          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*15)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__R15
                    == offsetof(CONTEXT, R15));

#define               OFFSETOF__CONTEXT__Rip          (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Rip
                    == offsetof(CONTEXT, Rip));

#define               OFFSETOF__CONTEXT__FltSave      (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__FltSave
                    == offsetof(CONTEXT, FltSave));

#define               OFFSETOF__CONTEXT__Xmm0         (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm0
                    == offsetof(CONTEXT, Xmm0));

#define               OFFSETOF__CONTEXT__Xmm1         (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm1
                    == offsetof(CONTEXT, Xmm1));

#define               OFFSETOF__CONTEXT__Xmm2         (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*2)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm2
                    == offsetof(CONTEXT, Xmm2));

#define               OFFSETOF__CONTEXT__Xmm3         (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*3)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm3
                    == offsetof(CONTEXT, Xmm3));

#define               OFFSETOF__CONTEXT__Xmm4         (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*4)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm4
                    == offsetof(CONTEXT, Xmm4));

#define               OFFSETOF__CONTEXT__Xmm5         (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*5)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm5
                    == offsetof(CONTEXT, Xmm5));

#define               OFFSETOF__CONTEXT__Xmm6         (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*6)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm6
                    == offsetof(CONTEXT, Xmm6));

#define               OFFSETOF__CONTEXT__Xmm7         (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*7)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm7
                    == offsetof(CONTEXT, Xmm7));

#define               OFFSETOF__CONTEXT__Xmm8         (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*8)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm8
                    == offsetof(CONTEXT, Xmm8));

#define               OFFSETOF__CONTEXT__Xmm9         (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*9)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm9
                    == offsetof(CONTEXT, Xmm9));

#define               OFFSETOF__CONTEXT__Xmm10        (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*10)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm10
                    == offsetof(CONTEXT, Xmm10));

#define               OFFSETOF__CONTEXT__Xmm11        (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*11)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm11
                    == offsetof(CONTEXT, Xmm11));

#define               OFFSETOF__CONTEXT__Xmm12        (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*12)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm12
                    == offsetof(CONTEXT, Xmm12));

#define               OFFSETOF__CONTEXT__Xmm13        (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*13)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm13
                    == offsetof(CONTEXT, Xmm13));

#define               OFFSETOF__CONTEXT__Xmm14        (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*14)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm14
                    == offsetof(CONTEXT, Xmm14));

#define               OFFSETOF__CONTEXT__Xmm15        (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*15)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__Xmm15
                    == offsetof(CONTEXT, Xmm15));

#define               OFFSETOF__CONTEXT__VectorRegister (8*6 + 4*2 + 2*6 + 4 + 8*6 + 8*16 + 8 + 2*16 + 8*16 + 16*16 + 96)
ASMCONSTANTS_C_ASSERT(OFFSETOF__CONTEXT__VectorRegister
                    == offsetof(CONTEXT, VectorRegister[0]));

#define               SIZEOF__FaultingExceptionFrame  (0x20 + SIZEOF__CONTEXT + 16)
ASMCONSTANTS_C_ASSERT(SIZEOF__FaultingExceptionFrame
                    == sizeof(FaultingExceptionFrame));

#define               OFFSETOF__FaultingExceptionFrame__m_fFilterExecuted 0x10
ASMCONSTANTS_C_ASSERT(OFFSETOF__FaultingExceptionFrame__m_fFilterExecuted
                    == offsetof(FaultingExceptionFrame, m_fFilterExecuted));

#define               OFFSETOF__FaultingExceptionFrame__m_SSP (0x20 + SIZEOF__CONTEXT)
ASMCONSTANTS_C_ASSERT(OFFSETOF__FaultingExceptionFrame__m_SSP
                    == offsetof(FaultingExceptionFrame, m_SSP));

#define               OFFSETOF__PtrArray__m_NumComponents 0x8
ASMCONSTANTS_C_ASSERT(OFFSETOF__PtrArray__m_NumComponents
                    == offsetof(PtrArray, m_NumComponents));

#ifndef TARGET_UNIX
#define OFFSET__TEB__ThreadLocalStoragePointer 0x58
ASMCONSTANTS_C_ASSERT(OFFSET__TEB__ThreadLocalStoragePointer == offsetof(TEB, ThreadLocalStoragePointer));
#endif

// If you change these constants, you need to update code in
// RedirectHandledJITCase.asm and ExcepAMD64.cpp.
#define REDIRECTSTUB_ESTABLISHER_OFFSET_RBP 0
#define REDIRECTSTUB_RBP_OFFSET_CONTEXT     0x20

#define THROWSTUB_ESTABLISHER_OFFSET_FaultingExceptionFrame 0x20

#ifdef FEATURE_SPECIAL_USER_MODE_APC
#define OFFSETOF__APC_CALLBACK_DATA__ContextRecord 0x8
#endif

#define Thread__ObjectRefFlush  ?ObjectRefFlush@Thread@@SAXPEAV1@@Z


#define                     DELEGATE_FIELD_OFFSET__METHOD_AUX           0x20
ASMCONSTANTS_RUNTIME_ASSERT(DELEGATE_FIELD_OFFSET__METHOD_AUX == Object::GetOffsetOfFirstField() +
        CoreLibBinder::GetFieldOffset(FIELD__DELEGATE__METHOD_PTR_AUX));


#define               MAX_STRING_LENGTH 0x3FFFFFDF
ASMCONSTANTS_C_ASSERT(MAX_STRING_LENGTH == CORINFO_String_MaxLength);

#define               STRING_COMPONENT_SIZE 2

#define               STRING_BASE_SIZE 0x16
ASMCONSTANTS_C_ASSERT(STRING_BASE_SIZE == OBJECT_BASESIZE + sizeof(DWORD) + sizeof(WCHAR));

#define               SZARRAY_BASE_SIZE 0x18
ASMCONSTANTS_C_ASSERT(SZARRAY_BASE_SIZE == OBJECT_BASESIZE + sizeof(DWORD) + sizeof(DWORD));

// For JIT_PInvokeBegin and JIT_PInvokeEnd helpers
#define               OFFSETOF__InlinedCallFrame__m_Datum 0x10
ASMCONSTANTS_C_ASSERT(OFFSETOF__InlinedCallFrame__m_Datum
                    == offsetof(InlinedCallFrame, m_Datum));

#define               OFFSETOF__InlinedCallFrame__m_pCallSiteSP 0x18
ASMCONSTANTS_C_ASSERT(OFFSETOF__InlinedCallFrame__m_pCallSiteSP
                    == offsetof(InlinedCallFrame, m_pCallSiteSP));

#define               OFFSETOF__InlinedCallFrame__m_pCallerReturnAddress 0x20
ASMCONSTANTS_C_ASSERT(OFFSETOF__InlinedCallFrame__m_pCallerReturnAddress
                    == offsetof(InlinedCallFrame, m_pCallerReturnAddress));

#define               OFFSETOF__InlinedCallFrame__m_pCalleeSavedFP 0x28
ASMCONSTANTS_C_ASSERT(OFFSETOF__InlinedCallFrame__m_pCalleeSavedFP
                    == offsetof(InlinedCallFrame, m_pCalleeSavedFP));

#define               OFFSETOF__InlinedCallFrame__m_pThread 0x30
ASMCONSTANTS_C_ASSERT(OFFSETOF__InlinedCallFrame__m_pThread
                    == offsetof(InlinedCallFrame, m_pThread));

#define CallDescrData__pSrc                0x00
#define CallDescrData__numStackSlots       0x08
#ifdef UNIX_AMD64_ABI
#define CallDescrData__pArgumentRegisters  0x10
#define CallDescrData__pFloatArgumentRegisters 0x18
#define CallDescrData__fpReturnSize        0x20
#define CallDescrData__pTarget             0x28
#define CallDescrData__returnValue         0x30
#else
#define CallDescrData__dwRegTypeMap        0x10
#define CallDescrData__fpReturnSize        0x18
#define CallDescrData__pTarget             0x20
#define CallDescrData__returnValue         0x28
#endif

ASMCONSTANTS_C_ASSERT(CallDescrData__pSrc                 == offsetof(CallDescrData, pSrc))
ASMCONSTANTS_C_ASSERT(CallDescrData__numStackSlots        == offsetof(CallDescrData, numStackSlots))
#ifdef UNIX_AMD64_ABI
ASMCONSTANTS_C_ASSERT(CallDescrData__pArgumentRegisters   == offsetof(CallDescrData, pArgumentRegisters))
ASMCONSTANTS_C_ASSERT(CallDescrData__pFloatArgumentRegisters == offsetof(CallDescrData, pFloatArgumentRegisters))
#else
ASMCONSTANTS_C_ASSERT(CallDescrData__dwRegTypeMap         == offsetof(CallDescrData, dwRegTypeMap))
#endif
ASMCONSTANTS_C_ASSERT(CallDescrData__fpReturnSize         == offsetof(CallDescrData, fpReturnSize))
ASMCONSTANTS_C_ASSERT(CallDescrData__pTarget              == offsetof(CallDescrData, pTarget))
ASMCONSTANTS_C_ASSERT(CallDescrData__returnValue          == offsetof(CallDescrData, returnValue))

#ifdef UNIX_AMD64_ABI
#define OFFSETOF__TransitionBlock__m_argumentRegisters    0x00
ASMCONSTANTS_C_ASSERT(OFFSETOF__TransitionBlock__m_argumentRegisters == offsetof(TransitionBlock, m_argumentRegisters))
#endif // UNIX_AMD64_ABI

#define FixupPrecodeData__Target 0x00
ASMCONSTANTS_C_ASSERT(FixupPrecodeData__Target            == offsetof(FixupPrecodeData, Target))

#define FixupPrecodeData__MethodDesc 0x08
ASMCONSTANTS_C_ASSERT(FixupPrecodeData__MethodDesc        == offsetof(FixupPrecodeData, MethodDesc))

#define FixupPrecodeData__PrecodeFixupThunk 0x10
ASMCONSTANTS_C_ASSERT(FixupPrecodeData__PrecodeFixupThunk == offsetof(FixupPrecodeData, PrecodeFixupThunk))

#define StubPrecodeData__Target 0x08
ASMCONSTANTS_C_ASSERT(StubPrecodeData__Target            == offsetof(StubPrecodeData, Target))

#define StubPrecodeData__SecretParam 0x00
ASMCONSTANTS_C_ASSERT(StubPrecodeData__SecretParam        == offsetof(StubPrecodeData, SecretParam))

#define CallCountingStubData__RemainingCallCountCell 0x00
ASMCONSTANTS_C_ASSERT(CallCountingStubData__RemainingCallCountCell == offsetof(CallCountingStubData, RemainingCallCountCell))

#define CallCountingStubData__TargetForMethod 0x08
ASMCONSTANTS_C_ASSERT(CallCountingStubData__TargetForMethod == offsetof(CallCountingStubData, TargetForMethod))

#define CallCountingStubData__TargetForThresholdReached 0x10
ASMCONSTANTS_C_ASSERT(CallCountingStubData__TargetForThresholdReached == offsetof(CallCountingStubData, TargetForThresholdReached))

#ifdef FEATURE_CACHED_INTERFACE_DISPATCH
#define OFFSETOF__InterfaceDispatchCache__m_rgEntries 0x20
ASMCONSTANTS_C_ASSERT(OFFSETOF__InterfaceDispatchCache__m_rgEntries == offsetof(InterfaceDispatchCache, m_rgEntries))

#define OFFSETOF__InterfaceDispatchCell__m_pCache 0x08
ASMCONSTANTS_C_ASSERT(OFFSETOF__InterfaceDispatchCell__m_pCache == offsetof(InterfaceDispatchCell, m_pCache))
#endif // FEATURE_CACHED_INTERFACE_DISPATCH

#define OFFSETOF__ThreadLocalInfo__m_pThread 0
ASMCONSTANTS_C_ASSERT(OFFSETOF__ThreadLocalInfo__m_pThread == offsetof(ThreadLocalInfo, m_pThread))

#ifdef FEATURE_INTERPRETER
#ifdef _DEBUG
#define OFFSETOF__InterpMethod__pCallStub 0x20
#else
#define OFFSETOF__InterpMethod__pCallStub 0x18
#endif
ASMCONSTANTS_C_ASSERT(OFFSETOF__InterpMethod__pCallStub == offsetof(InterpMethod, pCallStub))

#ifdef TARGET_UNIX
#define OFFSETOF__Thread__m_pInterpThreadContext 0xb50
#else // TARGET_UNIX
#define OFFSETOF__Thread__m_pInterpThreadContext 0xba8
#endif // TARGET_UNIX
ASMCONSTANTS_C_ASSERT(OFFSETOF__Thread__m_pInterpThreadContext == offsetof(Thread, m_pInterpThreadContext))

#define OFFSETOF__InterpThreadContext__pStackPointer 0x10
ASMCONSTANTS_C_ASSERT(OFFSETOF__InterpThreadContext__pStackPointer == offsetof(InterpThreadContext, pStackPointer))

#define OFFSETOF__CallStubHeader__Routines 0x10
ASMCONSTANTS_C_ASSERT(OFFSETOF__CallStubHeader__Routines == offsetof(CallStubHeader, Routines))

#ifdef TARGET_UNIX
#define SIZEOF__TransitionBlock 0x68
#else // TARGET_UNIX
#define SIZEOF__TransitionBlock 0x48
#endif // TARGET_UNIX
ASMCONSTANTS_C_ASSERT(SIZEOF__TransitionBlock == sizeof(TransitionBlock))

#endif // FEATURE_INTERPRETER

#ifdef PROFILING_SUPPORTED
#define PROFILE_ENTER        0x1
#define PROFILE_LEAVE        0x2
#define PROFILE_TAILCALL     0x4

#define ASMCONSTANTS_C_ASSERT_OFFSET(type, field) \
    ASMCONSTANTS_C_ASSERT(type##__##field == offsetof(type, field))

#if defined(UNIX_AMD64_ABI)
    #define SIZEOF__PROFILE_PLATFORM_SPECIFIC_DATA__buffer 0x8*16
    ASMCONSTANTS_C_ASSERT(SIZEOF__PROFILE_PLATFORM_SPECIFIC_DATA__buffer ==
            sizeof((*(PROFILE_PLATFORM_SPECIFIC_DATA*)0).buffer))
    #define SIZEOF__PROFILE_PLATFORM_SPECIFIC_DATA 0x8*22 + SIZEOF__PROFILE_PLATFORM_SPECIFIC_DATA__buffer
#else
    #define SIZEOF__PROFILE_PLATFORM_SPECIFIC_DATA__buffer 0
    #define SIZEOF__PROFILE_PLATFORM_SPECIFIC_DATA 0x8*12
#endif  // UNIX_AMD64_ABI
ASMCONSTANT_SIZEOF_ASSERT(PROFILE_PLATFORM_SPECIFIC_DATA)

#define PROFILE_PLATFORM_SPECIFIC_DATA__functionId 0x0
ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, functionId)
#define PROFILE_PLATFORM_SPECIFIC_DATA__rbp 0x8
ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, rbp)
#define PROFILE_PLATFORM_SPECIFIC_DATA__probeRsp 0x10
ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, probeRsp)
#define PROFILE_PLATFORM_SPECIFIC_DATA__ip 0x18
ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, ip)
#define PROFILE_PLATFORM_SPECIFIC_DATA__profiledRsp 0x20
ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, profiledRsp)
#define PROFILE_PLATFORM_SPECIFIC_DATA__rax 0x28
ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, rax)
#define PROFILE_PLATFORM_SPECIFIC_DATA__hiddenArg 0x30
ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, hiddenArg)
#define PROFILE_PLATFORM_SPECIFIC_DATA__flt0 0x38
ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, flt0)
#define PROFILE_PLATFORM_SPECIFIC_DATA__flt1 0x40
ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, flt1)
#define PROFILE_PLATFORM_SPECIFIC_DATA__flt2 0x48
ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, flt2)
#define PROFILE_PLATFORM_SPECIFIC_DATA__flt3 0x50
ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, flt3)
#if defined(UNIX_AMD64_ABI)
    #define PROFILE_PLATFORM_SPECIFIC_DATA__flt4 0x58
    ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, flt4)
    #define PROFILE_PLATFORM_SPECIFIC_DATA__flt5 0x60
    ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, flt5)
    #define PROFILE_PLATFORM_SPECIFIC_DATA__flt6 0x68
    ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, flt6)
    #define PROFILE_PLATFORM_SPECIFIC_DATA__flt7 0x70
    ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, flt7)
    #define PROFILE_PLATFORM_SPECIFIC_DATA__rdi 0x78
    ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, rdi)
    #define PROFILE_PLATFORM_SPECIFIC_DATA__rsi 0x80
    ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, rsi)
    #define PROFILE_PLATFORM_SPECIFIC_DATA__rdx 0x88
    ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, rdx)
    #define PROFILE_PLATFORM_SPECIFIC_DATA__rcx 0x90
    ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, rcx)
    #define PROFILE_PLATFORM_SPECIFIC_DATA__r8 0x98
    ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, r8)
    #define PROFILE_PLATFORM_SPECIFIC_DATA__r9 0xa0
    ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, r9)
    #define PROFILE_PLATFORM_SPECIFIC_DATA__flags 0xa8
#else  // !UNIX_AMD64_ABI
    #define PROFILE_PLATFORM_SPECIFIC_DATA__flags 0x58
#endif  // UNIX_AMD64_ABI
ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, flags)
#if defined(UNIX_AMD64_ABI)
    #define PROFILE_PLATFORM_SPECIFIC_DATA__buffer 0xb0
    ASMCONSTANTS_C_ASSERT_OFFSET(PROFILE_PLATFORM_SPECIFIC_DATA, buffer)
#endif

#undef ASMCONSTANTS_C_ASSERT_OFFSET
#endif  // PROFILING_SUPPORTED

#undef ASMCONSTANTS_RUNTIME_ASSERT
#undef ASMCONSTANTS_C_ASSERT
#ifndef UNIX_AMD64_ABI
#undef DBG_FRE
#endif // UNIX_AMD64_ABI


//#define USE_COMPILE_TIME_CONSTANT_FINDER // Uncomment this line to use the constant finder
#if defined(__cplusplus) && defined(USE_COMPILE_TIME_CONSTANT_FINDER)
// This class causes the compiler to emit an error with the constant we're interested in
// in the error message. This is useful if a size or offset changes. To use, comment out
// the compile-time assert that is firing, enable the constant finder, add the appropriate
// constant to find to BogusFunction(), and build.
//
// Here's a sample compiler error:
// d:\dd\clr\src\ndp\clr\src\vm\i386\asmconstants.h(326) : error C2248: 'FindCompileTimeConstant<N>::FindCompileTimeConstant' : cannot access private member declared in class 'FindCompileTimeConstant<N>'
//         with
//         [
//             N=1520
//         ]
//         d:\dd\clr\src\ndp\clr\src\vm\i386\asmconstants.h(321) : see declaration of 'FindCompileTimeConstant<N>::FindCompileTimeConstant'
//         with
//         [
//             N=1520
//         ]
template<size_t N>
class FindCompileTimeConstant
{
private:
	FindCompileTimeConstant();
};

void BogusFunction()
{
	// Sample usage to generate the error
	FindCompileTimeConstant<offsetof(Thread, m_pDomain)> bogus_variable;
	FindCompileTimeConstant<offsetof(Thread, m_ExceptionState)> bogus_variable2;
}
#endif // defined(__cplusplus) && defined(USE_COMPILE_TIME_CONSTANT_FINDER)
