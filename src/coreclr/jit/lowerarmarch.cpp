// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

/*XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XX                                                                           XX
XX             Lowering for ARM and ARM64 common code                        XX
XX                                                                           XX
XX  This encapsulates common logic for lowering trees for the ARM and ARM64  XX
XX  architectures.  For a more detailed view of what is lowering, please     XX
XX  take a look at Lower.cpp                                                 XX
XX                                                                           XX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
*/

#include "jitpch.h"
#ifdef _MSC_VER
#pragma hdrstop
#endif

#ifdef TARGET_ARMARCH // This file is ONLY used for ARM and ARM64 architectures

#include "jit.h"
#include "sideeffects.h"
#include "lower.h"
#include "lsra.h"

#ifdef FEATURE_HW_INTRINSICS
#include "hwintrinsic.h"
#endif

//------------------------------------------------------------------------
// IsCallTargetInRange: Can a call target address be encoded in-place?
//
// Return Value:
//    True if the addr fits into the range.
//
bool Lowering::IsCallTargetInRange(void* addr)
{
    return comp->codeGen->validImmForBL((ssize_t)addr);
}

//------------------------------------------------------------------------
// IsContainableImmed: Is an immediate encodable in-place?
//
// Return Value:
//    True if the immediate can be folded into an instruction,
//    for example small enough and non-relocatable.
//
bool Lowering::IsContainableImmed(GenTree* parentNode, GenTree* childNode) const
{
    if (!varTypeIsFloating(parentNode->TypeGet()))
    {
#ifdef TARGET_ARM64
        if (parentNode->OperIsCompare() && childNode->IsFloatPositiveZero())
        {
            // Contain 0.0 constant in fcmp on arm64
            // TODO: Enable for arm too (vcmp)

            // We currently don't emit these for floating points
            assert(!parentNode->OperIs(GT_TEST_EQ, GT_TEST_NE));
            return true;
        }
#endif

        // Make sure we have an actual immediate
        if (!childNode->IsCnsIntOrI())
            return false;
        if (childNode->AsIntCon()->ImmedValNeedsReloc(comp))
        {
            if (comp->IsTargetAbi(CORINFO_NATIVEAOT_ABI) && TargetOS::IsWindows &&
                childNode->IsIconHandle(GTF_ICON_SECREL_OFFSET))
            {
                // for windows/arm64, the immediate constant should be contained because it gets
                // generated as part of ADD instruction that consumes this constant. See
                // emitIns_Add_Add_Tls_Reloc().
                return true;
            }
            else
            {
                return false;
            }
        }

        // TODO-CrossBitness: we wouldn't need the cast below if GenTreeIntCon::gtIconVal had target_ssize_t type.
        target_ssize_t immVal = (target_ssize_t)childNode->AsIntCon()->gtIconVal;
        emitAttr       attr   = emitActualTypeSize(childNode->TypeGet());
        emitAttr       size   = EA_SIZE(attr);
#ifdef TARGET_ARM
        insFlags flags = (parentNode->gtOverflowEx() || parentNode->gtSetFlags()) ? INS_FLAGS_SET : INS_FLAGS_DONT_CARE;
#endif

        switch (parentNode->OperGet())
        {
            case GT_ADD:
            case GT_SUB:
#ifdef TARGET_ARM64
                return emitter::emitIns_valid_imm_for_add(immVal, size);
            case GT_CMPXCHG:
            case GT_LOCKADD:
            case GT_XORR:
            case GT_XAND:
            case GT_XADD:
                return comp->compOpportunisticallyDependsOn(InstructionSet_Atomics)
                           ? false
                           : emitter::emitIns_valid_imm_for_add(immVal, size);
#elif defined(TARGET_ARM)
                return emitter::emitIns_valid_imm_for_add(immVal, flags);
#endif
                break;

#ifdef TARGET_ARM64
            case GT_EQ:
            case GT_NE:
            case GT_LT:
            case GT_LE:
            case GT_GE:
            case GT_GT:
            case GT_CMP:
            case GT_BOUNDS_CHECK:
                return emitter::emitIns_valid_imm_for_cmp(immVal, size);
            case GT_AND:
            case GT_OR:
            case GT_XOR:
            case GT_TEST_EQ:
            case GT_TEST_NE:
                return emitter::emitIns_valid_imm_for_alu(immVal, size);
            case GT_JCMP:
                assert(immVal == 0);
                return true;
            case GT_JTEST:
                assert(isPow2(immVal));
                return true;
#elif defined(TARGET_ARM)
            case GT_EQ:
            case GT_NE:
            case GT_LT:
            case GT_LE:
            case GT_GE:
            case GT_GT:
            case GT_CMP:
            case GT_AND:
            case GT_OR:
            case GT_XOR:
                return emitter::emitIns_valid_imm_for_alu(immVal);
#endif // TARGET_ARM

#ifdef TARGET_ARM64
            case GT_STORE_LCL_FLD:
            case GT_STORE_LCL_VAR:
                if (immVal == 0)
                    return true;
                break;
#endif

            default:
                break;
        }
    }

    return false;
}

#ifdef TARGET_ARM64
//------------------------------------------------------------------------
// IsContainableUnaryOrBinaryOp: Is the child node a unary/binary op that is containable from the parent node?
//
// Return Value:
//    True if the child node can be contained.
//
// Notes:
//    This can handle the decision to emit 'madd' or 'msub'.
//
bool Lowering::IsContainableUnaryOrBinaryOp(GenTree* parentNode, GenTree* childNode) const
{
#ifdef DEBUG
    // The node we're checking should be one of the two child nodes
    if (parentNode->OperIsBinary())
    {
        assert((parentNode->gtGetOp1() == childNode) || (parentNode->gtGetOp2() == childNode));
    }
    else
    {
        assert(parentNode->OperIsUnary());
        assert((parentNode->gtGetOp1() == childNode));
    }
#endif // DEBUG

    // We cannot contain if the parent node
    // * is contained
    // * is not operating on an integer
    // * is already marking a child node as contained
    // * is required to throw on overflow

    if (parentNode->isContained())
        return false;

    if (!varTypeIsIntegral(parentNode))
        return false;

    if (parentNode->gtGetOp1()->isContained() || (parentNode->OperIsBinary() && parentNode->gtGetOp2()->isContained()))
        return false;

    if (parentNode->OperMayOverflow() && parentNode->gtOverflow())
        return false;

    // We cannot contain if the child node:
    // * is not operating on an integer
    // * is required to set a flag
    // * is required to throw on overflow

    if (!varTypeIsIntegral(childNode))
        return false;

    if ((childNode->gtFlags & GTF_SET_FLAGS) != 0)
        return false;

    if (childNode->OperMayOverflow() && childNode->gtOverflow())
        return false;

    GenTree* matchedOp = nullptr;

    if (childNode->OperIs(GT_MUL))
    {
        if (childNode->gtGetOp1()->isContained() || childNode->gtGetOp2()->isContained())
        {
            // Cannot contain if either of the childs operands is already contained
            return false;
        }

        if ((parentNode->gtFlags & GTF_SET_FLAGS) != 0)
        {
            // Cannot contain if the parent operation needs to set flags
            return false;
        }

        if (parentNode->OperIs(GT_ADD))
        {
            // Find "c + (a * b)" or "(a * b) + c"
            return IsInvariantInRange(childNode, parentNode);
        }

        if (parentNode->OperIs(GT_SUB))
        {
            // Find "c - (a * b)"
            assert(childNode == parentNode->gtGetOp2());
            return IsInvariantInRange(childNode, parentNode);
        }

        return false;
    }

    if (childNode->OperIs(GT_LSH, GT_RSH, GT_RSZ))
    {
        // Find "a op (b shift cns)"

        if (childNode->gtGetOp1()->isContained())
        {
            // Cannot contain if the childs op1 is already contained
            return false;
        }

        GenTree* shiftAmountNode = childNode->gtGetOp2();

        if (!shiftAmountNode->IsCnsIntOrI())
        {
            // Cannot contain if the childs op2 is not a constant
            return false;
        }

        const ssize_t shiftAmount = shiftAmountNode->AsIntCon()->IconValue();
        const ssize_t maxShift    = (static_cast<ssize_t>(genTypeSize(parentNode)) * BITS_PER_BYTE) - 1;

        if ((shiftAmount < 0x01) || (shiftAmount > maxShift))
        {
            // Cannot contain if the shift amount is less than 1 or greater than maxShift
            return false;
        }

        if (parentNode->OperIs(GT_ADD, GT_SUB, GT_AND, GT_NEG))
        {
            // These operations can still report flags

            if (IsInvariantInRange(childNode, parentNode))
            {
                assert(shiftAmountNode->isContained());
                return true;
            }
        }

        if ((parentNode->gtFlags & GTF_SET_FLAGS) != 0)
        {
            // Cannot contain if the parent operation needs to set flags
            return false;
        }

        if (parentNode->OperIs(GT_CMP, GT_OR, GT_XOR) || parentNode->OperIsCompare())
        {
            if (IsInvariantInRange(childNode, parentNode))
            {
                assert(shiftAmountNode->isContained());
                return true;
            }
        }

        if (childNode->OperIs(GT_LSH, GT_RSH, GT_RSZ) && parentNode->OperIs(GT_NOT, GT_AND_NOT, GT_OR_NOT, GT_XOR_NOT))
        {
            return true;
        }

        // TODO: Handle CMN, NEG/NEGS, BIC/BICS, EON, MVN, ORN, TST
        return false;
    }

    if (childNode->OperIs(GT_ROL, GT_ROR))
    {
        // Find "a op (b rotate cns)"

        if (childNode->gtGetOp1()->isContained())
        {
            // Cannot contain if the childs op1 is already contained
            return false;
        }

        GenTree* rotateAmountNode = childNode->gtGetOp2();

        if (!rotateAmountNode->IsCnsIntOrI())
        {
            // Cannot contain if the childs op2 is not a constant
            return false;
        }

        const ssize_t wrapAmount = (static_cast<ssize_t>(genTypeSize(childNode)) * BITS_PER_BYTE);
        assert((wrapAmount == 32) || (wrapAmount == 64));

        // Rotation is circular, so normalize to [0, wrapAmount - 1]
        ssize_t rotateAmount = rotateAmountNode->AsIntCon()->IconValue() % wrapAmount;
        assert((rotateAmount >= 0) && (rotateAmount <= (wrapAmount - 1)));

        if (childNode->OperIs(GT_ROL))
        {
            // The actual instructions only encode rotate right but
            // since rotating left by 1 is equivalen to rotating
            // right by (rotateAmount - 1), we can fix things here.

            childNode->SetOper(GT_ROR);
            rotateAmount = wrapAmount - rotateAmount;
        }

        rotateAmountNode->AsIntCon()->SetIconValue(rotateAmount);
        assert(childNode->OperIs(GT_ROR));

        if (parentNode->OperIs(GT_AND))
        {
            // These operations can still report flags

            if (IsInvariantInRange(childNode, parentNode))
            {
                assert(rotateAmountNode->isContained());
                return true;
            }
        }

        if ((parentNode->gtFlags & GTF_SET_FLAGS) != 0)
        {
            // Cannot contain if the parent operation needs to set flags
            return false;
        }

        if (parentNode->OperIs(GT_OR, GT_XOR))
        {
            if (IsInvariantInRange(childNode, parentNode))
            {
                assert(rotateAmountNode->isContained());
                return true;
            }
        }

        // TODO: Handle BIC/BICS, EON, MVN, ORN, TST
        return false;
    }

    if (childNode->OperIs(GT_NEG))
    {
        // If we have a contained LSH, RSH or RSZ, we can still contain NEG if the parent is a EQ or NE.
        if (childNode->gtGetOp1()->isContained() && !childNode->gtGetOp1()->OperIs(GT_LSH, GT_RSH, GT_RSZ))
        {
            // Cannot contain if the childs op1 is already contained
            return false;
        }

        if ((parentNode->gtFlags & GTF_SET_FLAGS) != 0)
        {
            // Cannot contain if the parent operation needs to set flags
            return false;
        }

        // EQ and NE are the only valid comparison ops that can contain NEG.
        if (parentNode->OperIs(GT_EQ, GT_NE))
        {
            if (!IsInvariantInRange(childNode, parentNode))
            {
                return false;
            }
            return true;
        }

        return false;
    }

    if (childNode->OperIs(GT_CAST))
    {
        // Find "a op cast(b)"
        GenTree* castOp = childNode->AsCast()->CastOp();

        bool isSupportedCast = false;

        if (varTypeIsSmall(childNode->CastToType()))
        {
            // The JIT doesn't track upcasts from small types, instead most types
            // are tracked as TYP_INT and then we get explicit downcasts to the
            // desired small type instead.

            assert(!varTypeIsFloating(castOp));
            isSupportedCast = true;
        }
        else if (childNode->TypeIs(TYP_LONG) && genActualTypeIsInt(castOp))
        {
            // We can handle "INT -> LONG", "INT -> ULONG", "UINT -> LONG", and "UINT -> ULONG"
            isSupportedCast = true;
        }

        if (!isSupportedCast)
        {
            return false;
        }

        if (!IsInvariantInRange(childNode, parentNode))
        {
            return false;
        }

        if (parentNode->OperIs(GT_ADD, GT_SUB))
        {
            // These operations can still report flags
            return true;
        }

        if ((parentNode->gtFlags & GTF_SET_FLAGS) != 0)
        {
            // Cannot contain if the parent operation needs to set flags
            return false;
        }

        if (parentNode->OperIs(GT_CMP))
        {
            return true;
        }

        if (parentNode->OperIsCmpCompare())
        {
            if (castOp->isContained())
            {
                return false;
            }

            if (IsContainableMemoryOp(castOp))
            {
                // The cast node will contain a memory operation which will perform
                // the cast on load/store, so we don't need to contain it here.
                // This check won't catch spills, so if register pressure is high
                // this can result in cmp (extended-register) taking higher priority
                // over a load/store with extension.
                return false;
            }

            return true;
        }

        return false;
    }

    return false;
}
#endif // TARGET_ARM64

//------------------------------------------------------------------------
// LowerStoreLoc: Lower a store of a lclVar
//
// Arguments:
//    storeLoc - the local store (GT_STORE_LCL_FLD or GT_STORE_LCL_VAR)
//
// Notes:
//    This involves:
//    - Widening small stores (on ARM).
//
// Returns:
//   Next node to lower.
//
GenTree* Lowering::LowerStoreLoc(GenTreeLclVarCommon* storeLoc)
{
#ifdef TARGET_ARM
    // On ARM, small stores can cost a bit more in terms of code size so we try to widen them. This is legal
    // as most small locals have 4-byte-wide stack homes, the common exception being (dependent) struct fields.
    //
    if (storeLoc->OperIs(GT_STORE_LCL_VAR) && varTypeIsSmall(storeLoc) && storeLoc->Data()->IsCnsIntOrI())
    {
        LclVarDsc* varDsc = comp->lvaGetDesc(storeLoc);
        if (!varDsc->lvIsStructField && (varDsc->GetStackSlotHomeType() == TYP_INT))
        {
            storeLoc->gtType = TYP_INT;
        }
    }
#endif // TARGET_ARM

    if (storeLoc->OperIs(GT_STORE_LCL_FLD))
    {
        // We should only encounter this for lclVars that are lvDoNotEnregister.
        verifyLclFldDoNotEnregister(storeLoc->GetLclNum());
    }

    ContainCheckStoreLoc(storeLoc);

    GenTree* next = storeLoc->gtNext;

#ifdef TARGET_ARM64
    if (comp->opts.OptimizationEnabled())
    {
        TryMoveAddSubRMWAfterIndir(storeLoc);
    }
#endif

    return next;
}

//------------------------------------------------------------------------
// LowerStoreIndir: Determine addressing mode for an indirection, and whether operands are contained.
//
// Arguments:
//    node       - The indirect store node (GT_STORE_IND) of interest
//
// Return Value:
//    Next node to lower.
//
GenTree* Lowering::LowerStoreIndir(GenTreeStoreInd* node)
{
    GenTree* next = node->gtNext;
    ContainCheckStoreIndir(node);

#ifdef TARGET_ARM64
    if (comp->opts.OptimizationEnabled())
    {
        OptimizeForLdpStp(node);
    }
#endif

    return next;
}

//------------------------------------------------------------------------
// LowerMul: Lower a GT_MUL/GT_MULHI/GT_MUL_LONG node.
//
// For ARM64 recognized GT_MULs that can be turned into GT_MUL_LONGs, as
// those are cheaper. Performs contaiment checks.
//
// Arguments:
//    mul - The node to lower
//
// Return Value:
//    The next node to lower.
//
GenTree* Lowering::LowerMul(GenTreeOp* mul)
{
    assert(mul->OperIsMul());

#ifdef TARGET_ARM64
    if (comp->opts.OptimizationEnabled() && mul->OperIs(GT_MUL) && mul->IsValidLongMul())
    {
        GenTreeCast* op1 = mul->gtGetOp1()->AsCast();
        GenTree*     op2 = mul->gtGetOp2();

        mul->ClearOverflow();
        mul->ClearUnsigned();
        if (op1->IsUnsigned())
        {
            mul->SetUnsigned();
        }

        op1->CastOp()->ClearContained(); // Uncontain any memory operands.
        mul->gtOp1 = op1->CastOp();
        BlockRange().Remove(op1);

        if (op2->OperIs(GT_CAST))
        {
            op2->AsCast()->CastOp()->ClearContained(); // Uncontain any memory operands.
            mul->gtOp2 = op2->AsCast()->CastOp();
            BlockRange().Remove(op2);
        }
        else
        {
            assert(op2->IsIntegralConst());
            assert(FitsIn<int32_t>(op2->AsIntConCommon()->IntegralValue()));

            op2->ChangeType(TYP_INT);
        }

        mul->ChangeOper(GT_MUL_LONG);
    }
#endif // TARGET_ARM64

    ContainCheckMul(mul);

    return mul->gtNext;
}

//------------------------------------------------------------------------
// LowerBinaryArithmetic: lowers the given binary arithmetic node.
//
// Arguments:
//    node - the arithmetic node to lower
//
// Returns:
//    The next node to lower.
//
GenTree* Lowering::LowerBinaryArithmetic(GenTreeOp* binOp)
{
    if (comp->opts.OptimizationEnabled())
    {
        if (binOp->OperIs(GT_AND))
        {
            GenTree* opNode  = nullptr;
            GenTree* notNode = nullptr;
            if (binOp->gtGetOp1()->OperIs(GT_NOT))
            {
                notNode = binOp->gtGetOp1();
                opNode  = binOp->gtGetOp2();
            }
            else if (binOp->gtGetOp2()->OperIs(GT_NOT))
            {
                notNode = binOp->gtGetOp2();
                opNode  = binOp->gtGetOp1();
            }

            if (notNode != nullptr)
            {
                binOp->gtOp1 = opNode;
                binOp->gtOp2 = notNode->AsUnOp()->gtGetOp1();
                binOp->ChangeOper(GT_AND_NOT);
                BlockRange().Remove(notNode);
            }
        }

#ifdef TARGET_ARM64
        if (binOp->OperIs(GT_AND, GT_OR))
        {
            GenTree* next;
            if (TryLowerAndOrToCCMP(binOp, &next))
            {
                return next;
            }
        }

        if (binOp->OperIs(GT_SUB))
        {
            // Attempt to optimize for umsubl/smsubl.
            GenTree* next;
            if (TryLowerAddSubToMulLongOp(binOp, &next))
            {
                return next;
            }
        }

        if (binOp->OperIs(GT_OR, GT_XOR))
        {
            GenTree* opNode  = nullptr;
            GenTree* notNode = nullptr;
            if (binOp->gtGetOp1()->OperIs(GT_NOT))
            {
                notNode = binOp->gtGetOp1();
                opNode  = binOp->gtGetOp2();
            }
            else if (binOp->gtGetOp2()->OperIs(GT_NOT))
            {
                notNode = binOp->gtGetOp2();
                opNode  = binOp->gtGetOp1();
            }

            if (notNode != nullptr)
            {
                binOp->gtOp1 = opNode;
                binOp->gtOp2 = notNode->AsUnOp()->gtGetOp1();
                if (binOp->OperIs(GT_OR))
                {
                    binOp->ChangeOper(GT_OR_NOT);
                }
                else
                {
                    binOp->ChangeOper(GT_XOR_NOT);
                }
                BlockRange().Remove(notNode);
            }
        }
#endif
    }

    ContainCheckBinary(binOp);

    return binOp->gtNext;
}

//------------------------------------------------------------------------
// LowerBlockStore: Lower a block store node
//
// Arguments:
//    blkNode - The block store node to lower
//
void Lowering::LowerBlockStore(GenTreeBlk* blkNode)
{
    GenTree* dstAddr = blkNode->Addr();
    GenTree* src     = blkNode->Data();
    unsigned size    = blkNode->Size();

    if (blkNode->OperIsInitBlkOp())
    {
#ifdef DEBUG
        // Use BlkOpKindLoop for more cases under stress mode
        if (comp->compStressCompile(Compiler::STRESS_STORE_BLOCK_UNROLLING, 50) && blkNode->OperIs(GT_STORE_BLK) &&
            ((blkNode->GetLayout()->GetSize() % TARGET_POINTER_SIZE) == 0) && src->IsIntegralConst(0))
        {
            blkNode->gtBlkOpKind = GenTreeBlk::BlkOpKindLoop;
#ifdef TARGET_ARM64
            // On ARM64 we can just use REG_ZR instead of having to load
            // the constant into a real register like on ARM32.
            src->SetContained();
#endif
            return;
        }
#endif

        if (src->OperIs(GT_INIT_VAL))
        {
            src->SetContained();
            src = src->AsUnOp()->gtGetOp1();
        }

        if ((size <= comp->getUnrollThreshold(Compiler::UnrollKind::Memset)) && src->OperIs(GT_CNS_INT))
        {
            blkNode->gtBlkOpKind = GenTreeBlk::BlkOpKindUnroll;

            // The fill value of an initblk is interpreted to hold a
            // value of (unsigned int8) however a constant of any size
            // may practically reside on the evaluation stack. So extract
            // the lower byte out of the initVal constant and replicate
            // it to a larger constant whose size is sufficient to support
            // the largest width store of the desired inline expansion.

            ssize_t fill = src->AsIntCon()->IconValue() & 0xFF;

            if (fill == 0)
            {
#ifdef TARGET_ARM64
                // On ARM64 we can just use REG_ZR instead of having to load
                // the constant into a real register like on ARM32.
                src->SetContained();
#endif
            }
#ifdef TARGET_ARM64
            else if (size >= REGSIZE_BYTES)
            {
                fill *= 0x0101010101010101LL;
                src->gtType = TYP_LONG;
            }
#endif
            else
            {
                fill *= 0x01010101;
            }

            src->AsIntCon()->SetIconValue(fill);

            ContainBlockStoreAddress(blkNode, size, dstAddr, nullptr);
        }
        else if (blkNode->IsZeroingGcPointersOnHeap())
        {
            blkNode->gtBlkOpKind = GenTreeBlk::BlkOpKindLoop;
#ifdef TARGET_ARM64
            // On ARM64 we can just use REG_ZR instead of having to load
            // the constant into a real register like on ARM32.
            src->SetContained();
#endif
        }
        else
        {
            LowerBlockStoreAsHelperCall(blkNode);
            return;
        }
    }
    else
    {
        assert(src->OperIs(GT_IND, GT_LCL_VAR, GT_LCL_FLD));
        src->SetContained();

        if (src->OperIs(GT_LCL_VAR))
        {
            // TODO-1stClassStructs: for now we can't work with STORE_BLOCK source in register.
            const unsigned srcLclNum = src->AsLclVar()->GetLclNum();
            comp->lvaSetVarDoNotEnregister(srcLclNum DEBUGARG(DoNotEnregisterReason::BlockOp));
        }

        ClassLayout* layout               = blkNode->GetLayout();
        bool         doCpObj              = layout->HasGCPtr();
        unsigned     copyBlockUnrollLimit = comp->getUnrollThreshold(Compiler::UnrollKind::Memcpy);

        if (doCpObj && (size <= copyBlockUnrollLimit))
        {
            // No write barriers are needed on the stack.
            // If the layout contains a byref, then we know it must live on the stack.
            if (blkNode->IsAddressNotOnHeap(comp))
            {
                // If the size is small enough to unroll then we need to mark the block as non-interruptible
                // to actually allow unrolling. The generated code does not report GC references loaded in the
                // temporary register(s) used for copying.
                doCpObj                  = false;
                blkNode->gtBlkOpGcUnsafe = true;
            }
        }

        if (doCpObj)
        {
            // Try to use bulk copy helper
            if (TryLowerBlockStoreAsGcBulkCopyCall(blkNode))
            {
                return;
            }

            assert(dstAddr->TypeIs(TYP_BYREF, TYP_I_IMPL));
            blkNode->gtBlkOpKind = GenTreeBlk::BlkOpKindCpObjUnroll;
        }
        else if (blkNode->OperIs(GT_STORE_BLK) && (size <= copyBlockUnrollLimit))
        {
            blkNode->gtBlkOpKind = GenTreeBlk::BlkOpKindUnroll;

            if (src->OperIs(GT_IND))
            {
                ContainBlockStoreAddress(blkNode, size, src->AsIndir()->Addr(), src->AsIndir());
            }

            ContainBlockStoreAddress(blkNode, size, dstAddr, nullptr);
        }
        else
        {
            assert(blkNode->OperIs(GT_STORE_BLK));
            LowerBlockStoreAsHelperCall(blkNode);
        }
    }
}

//------------------------------------------------------------------------
// ContainBlockStoreAddress: Attempt to contain an address used by an unrolled block store.
//
// Arguments:
//    blkNode - the block store node
//    size - the block size
//    addr - the address node to try to contain
//    addrParent - the parent of addr, in case this is checking containment of the source address.
//
void Lowering::ContainBlockStoreAddress(GenTreeBlk* blkNode, unsigned size, GenTree* addr, GenTree* addrParent)
{
    assert(blkNode->OperIs(GT_STORE_BLK) && (blkNode->gtBlkOpKind == GenTreeBlk::BlkOpKindUnroll));
    assert(size < INT32_MAX);

    if (addr->OperIs(GT_LCL_ADDR) && IsContainableLclAddr(addr->AsLclFld(), size))
    {
        addr->SetContained();
        return;
    }

    if (!addr->OperIs(GT_ADD) || addr->gtOverflow() || !addr->AsOp()->gtGetOp2()->OperIs(GT_CNS_INT))
    {
        return;
    }

    GenTreeIntCon* offsetNode = addr->AsOp()->gtGetOp2()->AsIntCon();
    ssize_t        offset     = offsetNode->IconValue();

#ifdef TARGET_ARM
    // All integer load/store instructions on Arm support offsets in range -255..255.
    // Of course, this is a rather conservative check.
    if ((offset < -255) || (offset > 255) || (offset + static_cast<int>(size) > 256))
    {
        return;
    }
#else  // !TARGET_ARM
    if ((ClrSafeInt<int>(offset) + ClrSafeInt<int>(size)).IsOverflow())
    {
        return;
    }
#endif // !TARGET_ARM

    if (!IsInvariantInRange(addr, blkNode, addrParent))
    {
        return;
    }

    BlockRange().Remove(offsetNode);

    addr->ChangeOper(GT_LEA);
    addr->AsAddrMode()->SetIndex(nullptr);
    addr->AsAddrMode()->SetScale(0);
    addr->AsAddrMode()->SetOffset(static_cast<int>(offset));
    addr->SetContained();
}

//------------------------------------------------------------------------
// LowerPutArgStk: Lower a GT_PUTARG_STK.
//
// Arguments:
//    putArgStk - The node to lower
//
void Lowering::LowerPutArgStk(GenTreePutArgStk* putArgNode)
{
    GenTree* src = putArgNode->Data();

    if (src->TypeIs(TYP_STRUCT))
    {
        // STRUCT args (FIELD_LIST / BLK / LCL_VAR / LCL_FLD) will always be contained.
        MakeSrcContained(putArgNode, src);

        if (src->OperIs(GT_LCL_VAR))
        {
            // TODO-1stClassStructs: support struct enregistration here by retyping "src" to its register type for
            // the non-split case.
            comp->lvaSetVarDoNotEnregister(src->AsLclVar()->GetLclNum() DEBUGARG(DoNotEnregisterReason::IsStructArg));
        }
    }
}

//------------------------------------------------------------------------
// LowerCast: Lower GT_CAST(srcType, DstType) nodes.
//
// Arguments:
//    tree - GT_CAST node to be lowered
//
// Return Value:
//    None.
//
void Lowering::LowerCast(GenTree* tree)
{
    assert(tree->OperIs(GT_CAST));

    JITDUMP("LowerCast for: ");
    DISPNODE(tree);
    JITDUMP("\n");

    GenTree*  op1     = tree->AsOp()->gtOp1;
    var_types dstType = tree->CastToType();
    var_types srcType = genActualType(op1->TypeGet());

    if (varTypeIsFloating(srcType))
    {
        // Overflow casts should have been converted to helper call in morph.
        noway_assert(!tree->gtOverflow());
        // Small types should have had an intermediate int cast inserted in morph.
        assert(!varTypeIsSmall(dstType));
    }

    assert(!varTypeIsSmall(srcType));

    // Now determine if we have operands that should be contained.
    ContainCheckCast(tree->AsCast());
}

//------------------------------------------------------------------------
// LowerRotate: Lower GT_ROL and GT_ROR nodes.
//
// Arguments:
//    tree - the node to lower
//
// Return Value:
//    None.
//
void Lowering::LowerRotate(GenTree* tree)
{
    if (tree->OperIs(GT_ROL))
    {
        // There is no ROL instruction on ARM. Convert ROL into ROR.
        GenTree* rotatedValue        = tree->AsOp()->gtOp1;
        unsigned rotatedValueBitSize = genTypeSize(rotatedValue->gtType) * 8;
        GenTree* rotateLeftIndexNode = tree->AsOp()->gtOp2;

        if (rotateLeftIndexNode->IsCnsIntOrI())
        {
            ssize_t rotateLeftIndex                    = rotateLeftIndexNode->AsIntCon()->gtIconVal;
            ssize_t rotateRightIndex                   = rotatedValueBitSize - rotateLeftIndex;
            rotateLeftIndexNode->AsIntCon()->gtIconVal = rotateRightIndex;
        }
        else
        {
            GenTree* tmp = comp->gtNewOperNode(GT_NEG, genActualType(rotateLeftIndexNode->gtType), rotateLeftIndexNode);
            BlockRange().InsertAfter(rotateLeftIndexNode, tmp);
            tree->AsOp()->gtOp2 = tmp;
        }
        tree->ChangeOper(GT_ROR);
    }
    ContainCheckShiftRotate(tree->AsOp());
}

#ifdef TARGET_ARM64
//------------------------------------------------------------------------
// LowerModPow2: Lower GT_MOD if the second operand is a constant power of 2.
//
// Arguments:
//    tree - the node to lower
//
// Notes:
//     TODO: We could do this optimization in morph but we do not have
//           a conditional select op in HIR. At some point, we may
//           introduce such an op.
void Lowering::LowerModPow2(GenTree* node)
{
    assert(node->OperIs(GT_MOD));
    GenTreeOp* mod      = node->AsOp();
    GenTree*   dividend = mod->gtGetOp1();
    GenTree*   divisor  = mod->gtGetOp2();

    JITDUMP("Lower: optimize X MOD POW2");

    assert(divisor->IsIntegralConstPow2());

    const var_types type = mod->TypeGet();
    assert((type == TYP_INT) || (type == TYP_LONG));

    ssize_t divisorCnsValue         = static_cast<ssize_t>(divisor->AsIntConCommon()->IntegralValue());
    ssize_t divisorCnsValueMinusOne = divisorCnsValue - 1;

    BlockRange().Remove(divisor);

    // We need to use the dividend node multiple times so its value needs to be
    // computed once and stored in a temp variable.
    LIR::Use opDividend(BlockRange(), &mod->AsOp()->gtOp1, mod);
    dividend = ReplaceWithLclVar(opDividend);

    GenTree* dividend2 = comp->gtClone(dividend);
    BlockRange().InsertAfter(dividend, dividend2);

    GenTreeIntCon* cns = comp->gtNewIconNode(divisorCnsValueMinusOne, type);
    BlockRange().InsertAfter(dividend2, cns);

    GenTree* const trueExpr = comp->gtNewOperNode(GT_AND, type, dividend, cns);
    BlockRange().InsertAfter(cns, trueExpr);
    LowerNode(trueExpr);

    if (divisorCnsValue == 2)
    {
        // {expr} % 2
        // Logically turns into:
        //     let a = {expr}
        //     if a < 0 then -(a & 1) else (a & 1)
        // which then turns into:
        //     and   reg1, reg0, #1
        //     cmp   reg0, #0
        //     cneg  reg0, reg1, lt

        GenTreeIntCon* cnsZero = comp->gtNewIconNode(0, type);
        BlockRange().InsertAfter(trueExpr, cnsZero);

        GenTree* const cmp = comp->gtNewOperNode(GT_CMP, TYP_VOID, dividend2, cnsZero);
        cmp->gtFlags |= GTF_SET_FLAGS;
        BlockRange().InsertAfter(cnsZero, cmp);
        LowerNode(cmp);

        mod->ChangeOper(GT_SELECT_NEGCC);
        GenTreeOpCC* node = mod->AsOpCC();
        node->gtOp1       = trueExpr;
        node->gtOp2       = nullptr;
        node->gtCondition = GenCondition::SLT;
    }
    else
    {
        // {expr} % {cns}
        // Logically turns into:
        //     let a = {expr}
        //     if a > 0 then (a & ({cns} - 1)) else -(-a & ({cns} - 1))
        // which then turns into:
        //     and   reg1, reg0, #({cns} - 1)
        //     negs  reg0, reg0
        //     and   reg0, reg0, #({cns} - 1)
        //     csneg reg0, reg1, reg0, mi

        GenTree* const neg = comp->gtNewOperNode(GT_NEG, type, dividend2);
        neg->gtFlags |= GTF_SET_FLAGS;
        BlockRange().InsertAfter(trueExpr, neg);

        GenTreeIntCon* cns2 = comp->gtNewIconNode(divisorCnsValueMinusOne, type);
        BlockRange().InsertAfter(neg, cns2);

        GenTree* const falseExpr = comp->gtNewOperNode(GT_AND, type, neg, cns2);
        BlockRange().InsertAfter(cns2, falseExpr);
        LowerNode(falseExpr);

        mod->SetOper(GT_SELECT_NEGCC);
        GenTreeOpCC* node = mod->AsOpCC();
        node->gtOp1       = trueExpr;
        node->gtOp2       = falseExpr;
        node->gtCondition = GenCondition::S;
    }

    ContainCheckNode(mod);
}

//------------------------------------------------------------------------
// LowerCnsMask: Lower GT_CNS_MSK. Ensure the mask matches a known pattern.
//               If not then lower to a constant vector.
//
// Arguments:
//    mask - the node to lower
//
GenTree* Lowering::LowerCnsMask(GenTreeMskCon* mask)
{
    // Try every type until a match is found

    if (mask->IsZero())
    {
        return mask->gtNext;
    }

    if (EvaluateSimdMaskToPattern<simd16_t>(TYP_BYTE, mask->gtSimdMaskVal) != SveMaskPatternNone)
    {
        return mask->gtNext;
    }

    if (EvaluateSimdMaskToPattern<simd16_t>(TYP_SHORT, mask->gtSimdMaskVal) != SveMaskPatternNone)
    {
        return mask->gtNext;
    }

    if (EvaluateSimdMaskToPattern<simd16_t>(TYP_INT, mask->gtSimdMaskVal) != SveMaskPatternNone)
    {
        return mask->gtNext;
    }

    if (EvaluateSimdMaskToPattern<simd16_t>(TYP_LONG, mask->gtSimdMaskVal) != SveMaskPatternNone)
    {
        return mask->gtNext;
    }

    // Not a valid pattern, so cannot be created using ptrue/pfalse. Instead the mask will require
    // loading from memory. There is no way to load to a predicate from memory using a PC relative
    // address, so instead use a constant vector plus conversion to mask. Using basetype byte will
    // ensure every entry in the mask is converted.

    LABELEDDISPTREERANGE("lowering cns mask to cns vector (before)", BlockRange(), mask);

    // Create a vector constant
    GenTreeVecCon* vecCon = comp->gtNewVconNode(TYP_SIMD16);
    EvaluateSimdCvtMaskToVector<simd16_t>(TYP_BYTE, &vecCon->gtSimdVal, mask->gtSimdMaskVal);
    BlockRange().InsertBefore(mask, vecCon);

    // Convert the vector constant to a mask
    GenTree* convertedVec = comp->gtNewSimdCvtVectorToMaskNode(TYP_MASK, vecCon, CORINFO_TYPE_BYTE, 16);
    BlockRange().InsertBefore(mask, convertedVec->AsHWIntrinsic()->Op(1));
    BlockRange().InsertBefore(mask, convertedVec);

    // Update use
    LIR::Use use;
    if (BlockRange().TryGetUse(mask, &use))
    {
        use.ReplaceWith(convertedVec);
    }
    else
    {
        convertedVec->SetUnusedValue();
    }

    BlockRange().Remove(mask);

    LABELEDDISPTREERANGE("lowering cns mask to cns vector (after)", BlockRange(), vecCon);

    return vecCon->gtNext;
}

const int POST_INDEXED_ADDRESSING_MAX_DISTANCE = 16;

//------------------------------------------------------------------------
// TryMoveAddSubRMWAfterIndir: Try to move an RMW update of a local with an
// ADD/SUB operand earlier to happen right after an indirection on the same
// local, attempting to make these combinable intro post-indexed addressing.
//
// Arguments:
//    store - The store to a local
//
// Return Value:
//    True if the store was moved; otherwise false.
//
bool Lowering::TryMoveAddSubRMWAfterIndir(GenTreeLclVarCommon* store)
{
    if (!store->OperIs(GT_STORE_LCL_VAR))
    {
        return false;
    }

    unsigned lclNum = store->GetLclNum();
    if (comp->lvaGetDesc(lclNum)->lvDoNotEnregister)
    {
        return false;
    }

    GenTree* data = store->Data();
    if (!data->OperIs(GT_ADD, GT_SUB) || data->gtOverflow())
    {
        return false;
    }

    GenTree* op1 = data->gtGetOp1();
    GenTree* op2 = data->gtGetOp2();
    if (!op1->OperIs(GT_LCL_VAR) || !op2->isContainedIntOrIImmed())
    {
        return false;
    }

    if (op1->AsLclVarCommon()->GetLclNum() != lclNum)
    {
        return false;
    }

    int maxCount = min(m_blockIndirs.Height(), POST_INDEXED_ADDRESSING_MAX_DISTANCE / 2);
    for (int i = 0; i < maxCount; i++)
    {
        SavedIndir& prev = m_blockIndirs.TopRef(i);
        if ((prev.AddrBase->GetLclNum() != lclNum) || (prev.Offset != 0))
        {
            continue;
        }

        GenTreeIndir* prevIndir = prev.Indir;
        if ((prevIndir == nullptr) || (prevIndir->gtNext == nullptr))
        {
            continue;
        }

        JITDUMP(
            "[%06u] is an an RMW ADD/SUB on local V%02u which is used as the address to [%06u]. Trying to make them adjacent.\n",
            Compiler::dspTreeID(store), lclNum, Compiler::dspTreeID(prevIndir));

        if (TryMakeIndirAndStoreAdjacent(prevIndir, store))
        {
            prev.Indir = nullptr;
            return true;
        }
    }

    return false;
}

//------------------------------------------------------------------------
// TryMakeIndirAndStoreAdjacent: Try to move a store earlier, right after the
// specified indirection.
//
// Arguments:
//   prevIndir - Indirection that comes before "store"
//   store     - Store that we want to happen next to the indirection
//
// Return Value:
//    True if the store was moved; otherwise false.
//
bool Lowering::TryMakeIndirAndStoreAdjacent(GenTreeIndir* prevIndir, GenTreeLclVarCommon* store)
{
    GenTree* cur = prevIndir;
    for (int i = 0; i < POST_INDEXED_ADDRESSING_MAX_DISTANCE; i++)
    {
        // No nodes should be marked yet
        assert((cur->gtLIRFlags & LIR::Flags::Mark) == 0);

        cur = cur->gtNext;
        if (cur == store)
            break;
    }

    if (cur != store)
    {
        JITDUMP("  Too far separated, giving up\n");
        return false;
    }

    JITDUMP("  They are close. Trying to move the following range (where * are nodes part of the data flow):\n\n");
#ifdef DEBUG
    bool     isClosed;
    GenTree* startDumpNode = BlockRange().GetTreeRange(prevIndir, &isClosed).FirstNode();
    GenTree* endDumpNode   = store->gtNext;

    auto dumpWithMarks = [=]() {
        if (!comp->verbose)
        {
            return;
        }

        for (GenTree* node = startDumpNode; node != endDumpNode; node = node->gtNext)
        {
            const char* prefix;
            if (node == prevIndir)
                prefix = "1. ";
            else if (node == store)
                prefix = "2. ";
            else if ((node->gtLIRFlags & LIR::Flags::Mark) != 0)
                prefix = "*  ";
            else
                prefix = "   ";

            comp->gtDispLIRNode(node, prefix);
        }
    };

#endif

    // Unmark tree when we exit the current scope
    auto code = [this, store] {
        UnmarkTree(store);
    };
    jitstd::utility::scoped_code<decltype(code)> finally(code);

    MarkTree(store);

    INDEBUG(dumpWithMarks());
    JITDUMP("\n");

    assert((prevIndir->gtLIRFlags & LIR::Flags::Mark) == 0);
    m_scratchSideEffects.Clear();

    for (GenTree* cur = prevIndir->gtNext; cur != store; cur = cur->gtNext)
    {
        if ((cur->gtLIRFlags & LIR::Flags::Mark) != 0)
        {
            // 'cur' is part of data flow of 'store', so we will be moving the
            // currently recorded effects past 'cur'.
            if (m_scratchSideEffects.InterferesWith(comp, cur, true))
            {
                JITDUMP("Giving up due to interference with [%06u]\n", Compiler::dspTreeID(cur));
                return false;
            }
        }
        else
        {
            // Not part of dataflow; add its effects that will move past
            // 'store'.
            m_scratchSideEffects.AddNode(comp, cur);
        }
    }

    if (m_scratchSideEffects.InterferesWith(comp, store, true))
    {
        JITDUMP("Have interference. Giving up.\n");
        return false;
    }

    JITDUMP("Interference checks passed. Moving nodes that are not part of data flow of [%06u]\n\n",
            Compiler::dspTreeID(store));

    GenTree* previous = prevIndir;
    for (GenTree* node = prevIndir->gtNext;;)
    {
        GenTree* next = node->gtNext;

        if ((node->gtLIRFlags & LIR::Flags::Mark) != 0)
        {
            // Part of data flow. Move it to happen right after 'previous'.
            BlockRange().Remove(node);
            BlockRange().InsertAfter(previous, node);
            previous = node;
        }

        if (node == store)
        {
            break;
        }

        node = next;
    }

    JITDUMP("Result:\n\n");
    INDEBUG(dumpWithMarks());
    JITDUMP("\n");
    return true;
}

//------------------------------------------------------------------------
// LowerAddForPossibleContainment: Tries to lower GT_ADD in such a way
//                                 that would allow one of its operands
//                                 to be contained.
//
// Arguments:
//    node - the node to lower
//    next - [out] Next node to lower if this function returns true
//
// Return Value:
//    false if no changes were made
//
bool Lowering::TryLowerAddForPossibleContainment(GenTreeOp* node, GenTree** next)
{
    assert(node->OperIs(GT_ADD));

    if (!comp->opts.OptimizationEnabled())
        return false;

    if (node->isContained())
        return false;

    if (!varTypeIsIntegral(node))
        return false;

    if (node->gtFlags & GTF_SET_FLAGS)
        return false;

    if (node->gtOverflow())
        return false;

    GenTree* op1 = node->gtGetOp1();
    GenTree* op2 = node->gtGetOp2();

    // If the second operand is a containable immediate,
    // then we do not want to risk moving it around
    // in this transformation.
    if (IsContainableImmed(node, op2))
        return false;

    GenTree* mul = nullptr;
    GenTree* c   = nullptr;
    if (op1->OperIs(GT_MUL))
    {
        // Swap
        mul = op1;
        c   = op2;
    }
    else
    {
        mul = op2;
        c   = op1;
    }

    if (mul->OperIs(GT_MUL) && !(mul->gtFlags & GTF_SET_FLAGS) && varTypeIsIntegral(mul) && !mul->gtOverflow() &&
        !mul->isContained() && !c->isContained())
    {
        GenTree* a = mul->gtGetOp1();
        GenTree* b = mul->gtGetOp2();

        // Transform "-a * b + c" to "c - a * b"
        if (a->OperIs(GT_NEG) && !(a->gtFlags & GTF_SET_FLAGS) && !b->OperIs(GT_NEG) && !a->isContained() &&
            !a->gtGetOp1()->isContained())
        {
            mul->AsOp()->gtOp1 = a->gtGetOp1();
            BlockRange().Remove(a);
            node->gtOp1 = c;
            node->gtOp2 = mul;
            node->ChangeOper(GT_SUB);

            ContainCheckNode(node);

            *next = node->gtNext;
            return true;
        }
        // Transform "a * -b + c" to "c - a * b"
        else if (b->OperIs(GT_NEG) && !(b->gtFlags & GTF_SET_FLAGS) && !a->OperIs(GT_NEG) && !b->isContained() &&
                 !b->gtGetOp1()->isContained())
        {
            mul->AsOp()->gtOp2 = b->gtGetOp1();
            BlockRange().Remove(b);
            node->gtOp1 = c;
            node->gtOp2 = mul;
            node->ChangeOper(GT_SUB);

            ContainCheckNode(node);

            *next = node->gtNext;
            return true;
        }
        // Transform "a * b + c" to "c + a * b"
        else if (op1->OperIs(GT_MUL))
        {
            node->gtOp1 = c;
            node->gtOp2 = mul;

            ContainCheckNode(node);

            *next = node->gtNext;
            return true;
        }
    }

    return false;
}
#endif

#ifdef FEATURE_HW_INTRINSICS

//----------------------------------------------------------------------------------------------
// LowerHWIntrinsicFusedMultiplyAddScalar: Lowers AdvSimd_FusedMultiplyAddScalar intrinsics
//   when some of the operands are negated by "containing" such negation.
//
//  Arguments:
//     node - The original hardware intrinsic node
//
// |  op1 | op2 | op3 |
// |  +   |  +  |  +  | AdvSimd_FusedMultiplyAddScalar
// |  +   |  +  |  -  | AdvSimd_FusedMultiplySubtractScalar
// |  +   |  -  |  +  | AdvSimd_FusedMultiplySubtractScalar
// |  +   |  -  |  -  | AdvSimd_FusedMultiplyAddScalar
// |  -   |  +  |  +  | AdvSimd_FusedMultiplySubtractNegatedScalar
// |  -   |  +  |  -  | AdvSimd_FusedMultiplyAddNegatedScalar
// |  -   |  -  |  +  | AdvSimd_FusedMultiplyAddNegatedScalar
// |  -   |  -  |  -  | AdvSimd_FusedMultiplySubtractNegatedScalar
//
void Lowering::LowerHWIntrinsicFusedMultiplyAddScalar(GenTreeHWIntrinsic* node)
{
    assert(node->GetHWIntrinsicId() == NI_AdvSimd_FusedMultiplyAddScalar);

    GenTree* op1 = node->Op(1);
    GenTree* op2 = node->Op(2);
    GenTree* op3 = node->Op(3);

    auto lowerOperand = [this](GenTree* op) {
        bool wasNegated = false;

        if (op->OperIsHWIntrinsic() &&
            ((op->AsHWIntrinsic()->GetHWIntrinsicId() == NI_AdvSimd_Arm64_DuplicateToVector64) ||
             (op->AsHWIntrinsic()->GetHWIntrinsicId() == NI_Vector64_CreateScalarUnsafe)))
        {
            GenTreeHWIntrinsic* createVector64 = op->AsHWIntrinsic();
            GenTree*            valueOp        = createVector64->Op(1);

            if (valueOp->OperIs(GT_NEG))
            {
                createVector64->Op(1) = valueOp->gtGetOp1();
                BlockRange().Remove(valueOp);
                wasNegated = true;
            }
        }

        return wasNegated;
    };

    const bool op1WasNegated = lowerOperand(op1);
    const bool op2WasNegated = lowerOperand(op2);
    const bool op3WasNegated = lowerOperand(op3);

    if (op1WasNegated)
    {
        if (op2WasNegated != op3WasNegated)
        {
            node->ChangeHWIntrinsicId(NI_AdvSimd_FusedMultiplyAddNegatedScalar);
        }
        else
        {
            node->ChangeHWIntrinsicId(NI_AdvSimd_FusedMultiplySubtractNegatedScalar);
        }
    }
    else if (op2WasNegated != op3WasNegated)
    {
        node->ChangeHWIntrinsicId(NI_AdvSimd_FusedMultiplySubtractScalar);
    }
}

//----------------------------------------------------------------------------------------------
// Lowering::LowerHWIntrinsic: Perform containment analysis for a hardware intrinsic node.
//
//  Arguments:
//     node - The hardware intrinsic node.
//
GenTree* Lowering::LowerHWIntrinsic(GenTreeHWIntrinsic* node)
{
    if (node->TypeIs(TYP_SIMD12))
    {
        // GT_HWINTRINSIC node requiring to produce TYP_SIMD12 in fact
        // produces a TYP_SIMD16 result
        node->gtType = TYP_SIMD16;
    }

    NamedIntrinsic intrinsicId = node->GetHWIntrinsicId();

    bool       isScalar = false;
    genTreeOps oper     = node->GetOperForHWIntrinsicId(&isScalar);

    switch (oper)
    {
        case GT_AND:
        case GT_OR:
        {
            // We want to recognize (~op1 & op2) and transform it
            // into AdvSimd.AndNot(op2, op1) as well as (op1 & ~op2)
            // transforming it into AdvSimd.AndNot(op1, op2)
            //
            // We want to similarly handle (~op1 | op2) and (op1 | ~op2)

            bool transform = false;

            GenTree* op1 = node->Op(1);
            GenTree* op2 = node->Op(2);

            if (op2->OperIsHWIntrinsic())
            {
                GenTreeHWIntrinsic* op2Intrin = op2->AsHWIntrinsic();

                bool       op2IsScalar = false;
                genTreeOps op2Oper     = op2Intrin->GetOperForHWIntrinsicId(&op2IsScalar);

                if (op2Oper == GT_NOT)
                {
                    assert(!op2IsScalar);
                    transform = true;

                    op2 = op2Intrin->Op(1);
                    BlockRange().Remove(op2Intrin);
                }
            }

            if (!transform && op1->OperIsHWIntrinsic())
            {
                GenTreeHWIntrinsic* opIntrin = op1->AsHWIntrinsic();

                bool       op1IsScalar = false;
                genTreeOps op1Oper     = opIntrin->GetOperForHWIntrinsicId(&op1IsScalar);

                if (op1Oper == GT_NOT)
                {
                    assert(!op1IsScalar);
                    transform = true;

                    op1 = opIntrin->Op(1);
                    BlockRange().Remove(opIntrin);

                    std::swap(op1, op2);
                }
            }

            if (transform)
            {
                if (oper == GT_AND)
                {
                    oper        = GT_AND_NOT;
                    intrinsicId = NI_AdvSimd_BitwiseClear;
                }
                else
                {
                    assert(oper == GT_OR);
                    oper        = GT_NONE;
                    intrinsicId = NI_AdvSimd_OrNot;
                }

                node->ChangeHWIntrinsicId(intrinsicId, op1, op2);
                oper = GT_AND_NOT;
            }
            break;
        }

        default:
        {
            break;
        }
    }

    switch (intrinsicId)
    {
        case NI_Vector64_Create:
        case NI_Vector128_Create:
        case NI_Vector64_CreateScalar:
        case NI_Vector128_CreateScalar:
        {
            // We don't directly support the Vector64.Create or Vector128.Create methods in codegen
            // and instead lower them to other intrinsic nodes in LowerHWIntrinsicCreate so we expect
            // that the node is modified to either not be a HWIntrinsic node or that it is no longer
            // the same intrinsic as when it came in.

            return LowerHWIntrinsicCreate(node);
        }

        case NI_Vector64_Dot:
        case NI_Vector128_Dot:
        {
            return LowerHWIntrinsicDot(node);
        }

        case NI_Vector64_GetElement:
        case NI_Vector128_GetElement:
        {
            GenTree* op1 = node->Op(1);
            GenTree* op2 = node->Op(2);

            bool isContainableMemory = IsContainableMemoryOp(op1) && IsSafeToContainMem(node, op1);

            if (isContainableMemory || !op2->OperIsConst())
            {
                unsigned    simdSize        = node->GetSimdSize();
                CorInfoType simdBaseJitType = node->GetSimdBaseJitType();
                var_types   simdBaseType    = node->GetSimdBaseType();
                var_types   simdType        = Compiler::getSIMDTypeForSize(simdSize);

                // We're either already loading from memory or we need to since
                // we don't know what actual index is going to be retrieved.

                unsigned lclNum  = BAD_VAR_NUM;
                unsigned lclOffs = 0;

                if (!isContainableMemory)
                {
                    // We aren't already in memory, so we need to spill there

                    comp->getSIMDInitTempVarNum(simdType);
                    lclNum = comp->lvaSIMDInitTempVarNum;

                    GenTree* storeLclVar = comp->gtNewStoreLclVarNode(lclNum, op1);
                    BlockRange().InsertBefore(node, storeLclVar);
                    LowerNode(storeLclVar);
                }
                else if (op1->IsLocal())
                {
                    // We're an existing local that is loaded from memory
                    GenTreeLclVarCommon* lclVar = op1->AsLclVarCommon();

                    lclNum  = lclVar->GetLclNum();
                    lclOffs = lclVar->GetLclOffs();

                    BlockRange().Remove(op1);
                }

                if (lclNum != BAD_VAR_NUM)
                {
                    // We need to get the address of the local
                    op1 = comp->gtNewLclAddrNode(lclNum, lclOffs, TYP_BYREF);
                    BlockRange().InsertBefore(node, op1);
                    LowerNode(op1);
                }
                else
                {
                    assert(op1->isIndir());

                    // We need to get the underlying address
                    GenTree* addr = op1->AsIndir()->Addr();
                    BlockRange().Remove(op1);
                    op1 = addr;
                }

                GenTree* offset       = op2;
                unsigned baseTypeSize = genTypeSize(simdBaseType);

                if (offset->OperIsConst())
                {
                    // We have a constant index, so scale it up directly
                    GenTreeIntConCommon* index = offset->AsIntCon();
                    index->SetIconValue(index->IconValue() * baseTypeSize);
                }
                else
                {
                    // We have a non-constant index, so scale it up via mul but
                    // don't lower the GT_MUL node since the indir will try to
                    // create an addressing mode and will do folding itself. We
                    // do, however, skip the multiply for scale == 1

                    if (baseTypeSize != 1)
                    {
                        GenTreeIntConCommon* scale = comp->gtNewIconNode(baseTypeSize);
                        BlockRange().InsertBefore(node, scale);

                        offset = comp->gtNewOperNode(GT_MUL, offset->TypeGet(), offset, scale);
                        BlockRange().InsertBefore(node, offset);
                    }
                }

                // Add the offset, don't lower the GT_ADD node since the indir will
                // try to create an addressing mode and will do folding itself. We
                // do, however, skip the add for offset == 0
                GenTree* addr = op1;

                if (!offset->IsIntegralConst(0))
                {
                    addr = comp->gtNewOperNode(GT_ADD, addr->TypeGet(), addr, offset);
                    BlockRange().InsertBefore(node, addr);
                }
                else
                {
                    BlockRange().Remove(offset);
                }

                // Finally we can indirect the memory address to get the actual value
                GenTreeIndir* indir = comp->gtNewIndir(JITtype2varType(simdBaseJitType), addr);
                BlockRange().InsertBefore(node, indir);

                LIR::Use use;
                if (BlockRange().TryGetUse(node, &use))
                {
                    use.ReplaceWith(indir);
                }
                else
                {
                    indir->SetUnusedValue();
                }

                BlockRange().Remove(node);
                return LowerNode(indir);
            }

            assert(op2->OperIsConst());
            break;
        }

        case NI_Vector64_op_Equality:
        case NI_Vector128_op_Equality:
        {
            return LowerHWIntrinsicCmpOp(node, GT_EQ);
        }

        case NI_Vector64_op_Inequality:
        case NI_Vector128_op_Inequality:
        {
            return LowerHWIntrinsicCmpOp(node, GT_NE);
        }

        case NI_Sve_TestAnyTrue:
        {
            LowerNodeCC(node, GenCondition::NE);
            node->gtType = TYP_VOID;
            return node->gtNext;
        }

        case NI_Sve_TestFirstTrue:
        {
            LowerNodeCC(node, GenCondition::SLT);
            node->gtType = TYP_VOID;
            return node->gtNext;
        }

        case NI_Sve_TestLastTrue:
        {
            LowerNodeCC(node, GenCondition::ULT);
            node->gtType = TYP_VOID;
            return node->gtNext;
        }

        case NI_Vector128_WithLower:
        case NI_Vector128_WithUpper:
        {
            // Converts to equivalent managed code:
            //   AdvSimd.InsertScalar(vector.AsUInt64(), 0, value.AsUInt64()).As<ulong, T>();
            // -or-
            //   AdvSimd.InsertScalar(vector.AsUInt64(), 1, value.AsUInt64()).As<ulong, T>();

            int index = (intrinsicId == NI_Vector128_WithUpper) ? 1 : 0;

            GenTree* op1 = node->Op(1);
            GenTree* op2 = node->Op(2);

            GenTree* op3 = comp->gtNewIconNode(index);
            BlockRange().InsertBefore(node, op3);
            LowerNode(op3);

            node->SetSimdBaseJitType(CORINFO_TYPE_ULONG);
            node->ResetHWIntrinsicId(NI_AdvSimd_InsertScalar, comp, op1, op3, op2);
            break;
        }

        case NI_AdvSimd_FusedMultiplyAddScalar:
            LowerHWIntrinsicFusedMultiplyAddScalar(node);
            break;

        case NI_Sve_ConditionalSelect:
            return LowerHWIntrinsicCndSel(node);

        case NI_Sve_SetFfr:
        {
            StoreFFRValue(node);
            break;
        }

        case NI_Sve_GetFfrByte:
        case NI_Sve_GetFfrInt16:
        case NI_Sve_GetFfrInt32:
        case NI_Sve_GetFfrInt64:
        case NI_Sve_GetFfrSByte:
        case NI_Sve_GetFfrUInt16:
        case NI_Sve_GetFfrUInt32:
        case NI_Sve_GetFfrUInt64:
        {
            LIR::Use use;
            bool     foundUse = BlockRange().TryGetUse(node, &use);
            if (foundUse)
            {
                unsigned lclNum = comp->getFFRegisterVarNum();
                GenTree* lclVar = comp->gtNewLclvNode(lclNum, TYP_MASK);
                BlockRange().InsertBefore(node, lclVar);
                use.ReplaceWith(lclVar);
                GenTree* next = node->gtNext;
                BlockRange().Remove(node);
                return next;
            }
            else
            {
                node->SetUnusedValue();
            }

            break;
        }

        case NI_Sve_GatherVectorByteZeroExtendFirstFaulting:
        case NI_Sve_GatherVectorFirstFaulting:
        case NI_Sve_GatherVectorInt16SignExtendFirstFaulting:
        case NI_Sve_GatherVectorInt16WithByteOffsetsSignExtendFirstFaulting:
        case NI_Sve_GatherVectorInt32SignExtendFirstFaulting:
        case NI_Sve_GatherVectorInt32WithByteOffsetsSignExtendFirstFaulting:
        case NI_Sve_GatherVectorSByteSignExtendFirstFaulting:
        case NI_Sve_GatherVectorUInt16WithByteOffsetsZeroExtendFirstFaulting:
        case NI_Sve_GatherVectorUInt16ZeroExtendFirstFaulting:
        case NI_Sve_GatherVectorUInt32WithByteOffsetsZeroExtendFirstFaulting:
        case NI_Sve_GatherVectorUInt32ZeroExtendFirstFaulting:
        case NI_Sve_GatherVectorWithByteOffsetFirstFaulting:
        case NI_Sve_LoadVectorByteZeroExtendFirstFaulting:
        case NI_Sve_LoadVectorFirstFaulting:
        case NI_Sve_LoadVectorInt16SignExtendFirstFaulting:
        case NI_Sve_LoadVectorInt32SignExtendFirstFaulting:
        case NI_Sve_LoadVectorSByteSignExtendFirstFaulting:
        case NI_Sve_LoadVectorUInt16ZeroExtendFirstFaulting:
        case NI_Sve_LoadVectorUInt32ZeroExtendFirstFaulting:
        case NI_Sve_LoadVectorByteNonFaultingZeroExtendToInt16:
        case NI_Sve_LoadVectorByteNonFaultingZeroExtendToInt32:
        case NI_Sve_LoadVectorByteNonFaultingZeroExtendToInt64:
        case NI_Sve_LoadVectorByteNonFaultingZeroExtendToUInt16:
        case NI_Sve_LoadVectorByteNonFaultingZeroExtendToUInt32:
        case NI_Sve_LoadVectorByteNonFaultingZeroExtendToUInt64:
        case NI_Sve_LoadVectorInt16NonFaultingSignExtendToInt32:
        case NI_Sve_LoadVectorInt16NonFaultingSignExtendToInt64:
        case NI_Sve_LoadVectorInt16NonFaultingSignExtendToUInt32:
        case NI_Sve_LoadVectorInt16NonFaultingSignExtendToUInt64:
        case NI_Sve_LoadVectorInt32NonFaultingSignExtendToInt64:
        case NI_Sve_LoadVectorInt32NonFaultingSignExtendToUInt64:
        case NI_Sve_LoadVectorNonFaulting:
        case NI_Sve_LoadVectorSByteNonFaultingSignExtendToInt16:
        case NI_Sve_LoadVectorSByteNonFaultingSignExtendToInt32:
        case NI_Sve_LoadVectorSByteNonFaultingSignExtendToInt64:
        case NI_Sve_LoadVectorSByteNonFaultingSignExtendToUInt16:
        case NI_Sve_LoadVectorSByteNonFaultingSignExtendToUInt32:
        case NI_Sve_LoadVectorSByteNonFaultingSignExtendToUInt64:
        case NI_Sve_LoadVectorUInt16NonFaultingZeroExtendToInt32:
        case NI_Sve_LoadVectorUInt16NonFaultingZeroExtendToInt64:
        case NI_Sve_LoadVectorUInt16NonFaultingZeroExtendToUInt32:
        case NI_Sve_LoadVectorUInt16NonFaultingZeroExtendToUInt64:
        case NI_Sve_LoadVectorUInt32NonFaultingZeroExtendToInt64:
        case NI_Sve_LoadVectorUInt32NonFaultingZeroExtendToUInt64:
        {
            LIR::Use use;
            bool     foundUse = BlockRange().TryGetUse(node, &use);

            if (m_ffrTrashed)
            {
                // Consume the FFR register value from local variable to simulate "use" of FFR,
                // only if it was trashed. If it was not trashed, we do not have to reload the
                // contents of the FFR register.

                unsigned lclNum = comp->getFFRegisterVarNum();
                GenTree* lclVar = comp->gtNewLclvNode(lclNum, TYP_MASK);
                BlockRange().InsertBefore(node, lclVar);
                LowerNode(lclVar);

                if (node->GetOperandCount() == 3)
                {
                    node->ResetHWIntrinsicId(intrinsicId, comp, node->Op(1), node->Op(2), node->Op(3), lclVar);
                }
                else
                {
                    assert(node->GetOperandCount() == 2);
                    node->ResetHWIntrinsicId(intrinsicId, comp, node->Op(1), node->Op(2), lclVar);
                }
            }

            if (foundUse)
            {
                unsigned   tmpNum    = comp->lvaGrabTemp(true DEBUGARG("Return value result/FFR"));
                LclVarDsc* tmpVarDsc = comp->lvaGetDesc(tmpNum);
                tmpVarDsc->lvType    = node->TypeGet();
                GenTree* storeLclVar;
                use.ReplaceWithLclVar(comp, tmpNum, &storeLclVar);
            }
            else
            {
                node->SetUnusedValue();
            }

            StoreFFRValue(node);
            break;
        }

        default:
            break;
    }

    if (HWIntrinsicInfo::IsEmbeddedMaskedOperation(intrinsicId))
    {
        LIR::Use use;
        LABELEDDISPTREERANGE("lowering EmbeddedMasked HWIntrinisic (before)", BlockRange(), node);

        // Use lastOp to verify if it's a ConditionlSelectNode.
        size_t lastOpNum = node->GetOperandCount();

        if (node->Op(lastOpNum)->OperIsHWIntrinsic() &&
            node->Op(lastOpNum)->AsHWIntrinsic()->GetHWIntrinsicId() == NI_Sve_ConditionalSelect &&
            TryContainingCselOp(node, node->Op(lastOpNum)->AsHWIntrinsic()))
        {
            LABELEDDISPTREERANGE("Contained conditional select", BlockRange(), node);
            return node->gtNext;
        }

        // Wrap a conditional select around the embedded mask operation

        CorInfoType simdBaseJitType = node->GetSimdBaseJitType();
        unsigned    simdSize        = node->GetSimdSize();
        var_types   simdType        = Compiler::getSIMDTypeForSize(simdSize);

        bool      foundUse = BlockRange().TryGetUse(node, &use);
        GenTree*  trueMask = comp->gtNewSimdAllTrueMaskNode(simdBaseJitType);
        GenTree*  falseVal = comp->gtNewZeroConNode(simdType);
        var_types nodeType = simdType;

        if (HWIntrinsicInfo::ReturnsPerElementMask(node->GetHWIntrinsicId()))
        {
            nodeType = TYP_MASK;
        }

        BlockRange().InsertBefore(node, trueMask);
        BlockRange().InsertBefore(node, falseVal);

        GenTreeHWIntrinsic* condSelNode =
            comp->gtNewSimdHWIntrinsicNode(nodeType, trueMask, node, falseVal, NI_Sve_ConditionalSelect,
                                           simdBaseJitType, simdSize);
        BlockRange().InsertAfter(node, condSelNode);
        if (foundUse)
        {
            use.ReplaceWith(condSelNode);
        }
        else
        {
            node->ClearUnusedValue();
            condSelNode->SetUnusedValue();
        }

        LABELEDDISPTREERANGE("Embedded HWIntrinisic inside conditional select", BlockRange(), condSelNode);
    }

    ContainCheckHWIntrinsic(node);
    return node->gtNext;
}

//----------------------------------------------------------------------------------------------
// Lowering::IsValidConstForMovImm: Determines if the given node can be replaced by a mov/fmov immediate instruction
//
//  Arguments:
//     node - The hardware intrinsic node.
//
//  Returns:
//     true if the node can be replaced by a mov/fmov immediate instruction; otherwise, false
//
bool Lowering::IsValidConstForMovImm(GenTreeHWIntrinsic* node)
{
    assert(HWIntrinsicInfo::IsVectorCreate(node->GetHWIntrinsicId()) ||
           HWIntrinsicInfo::IsVectorCreateScalar(node->GetHWIntrinsicId()) ||
           HWIntrinsicInfo::IsVectorCreateScalarUnsafe(node->GetHWIntrinsicId()) ||
           (node->GetHWIntrinsicId() == NI_AdvSimd_DuplicateToVector64) ||
           (node->GetHWIntrinsicId() == NI_AdvSimd_DuplicateToVector128) ||
           (node->GetHWIntrinsicId() == NI_AdvSimd_Arm64_DuplicateToVector64) ||
           (node->GetHWIntrinsicId() == NI_AdvSimd_Arm64_DuplicateToVector128));
    assert(node->GetOperandCount() == 1);

    GenTree* const op1 = node->Op(1);

    if (op1->IsCnsIntOrI())
    {
        const ssize_t dataValue = op1->AsIntCon()->gtIconVal;
        return comp->GetEmitter()->emitIns_valid_imm_for_movi(dataValue, emitActualTypeSize(node->GetSimdBaseType()));
    }
    else if (op1->IsCnsFltOrDbl())
    {
        assert(varTypeIsFloating(node->GetSimdBaseType()));
        const double dataValue = op1->AsDblCon()->DconValue();
        return comp->GetEmitter()->emitIns_valid_imm_for_fmov(dataValue);
    }

    return false;
}

//----------------------------------------------------------------------------------------------
// Lowering::LowerHWIntrinsicCmpOp: Lowers a Vector128 or Vector256 comparison intrinsic
//
//  Arguments:
//     node  - The hardware intrinsic node.
//     cmpOp - The comparison operation, currently must be GT_EQ or GT_NE
//
GenTree* Lowering::LowerHWIntrinsicCmpOp(GenTreeHWIntrinsic* node, genTreeOps cmpOp)
{
    NamedIntrinsic intrinsicId     = node->GetHWIntrinsicId();
    CorInfoType    simdBaseJitType = node->GetSimdBaseJitType();
    var_types      simdBaseType    = node->GetSimdBaseType();
    unsigned       simdSize        = node->GetSimdSize();
    var_types      simdType        = Compiler::getSIMDTypeForSize(simdSize);

    assert((intrinsicId == NI_Vector64_op_Equality) || (intrinsicId == NI_Vector64_op_Inequality) ||
           (intrinsicId == NI_Vector128_op_Equality) || (intrinsicId == NI_Vector128_op_Inequality));

    assert(varTypeIsSIMD(simdType));
    assert(varTypeIsArithmetic(simdBaseType));
    assert(simdSize != 0);
    assert(node->TypeIs(TYP_INT));
    assert((cmpOp == GT_EQ) || (cmpOp == GT_NE));

    // We have the following (with the appropriate simd size and where the intrinsic could be op_Inequality):
    //          /--*  op2  simd
    //          /--*  op1  simd
    //   node = *  HWINTRINSIC   simd   T op_Equality

    GenTree* op1 = node->Op(1);
    GenTree* op2 = node->Op(2);

    // Optimize comparison against Vector64/128<>.Zero via UMAXV:
    //
    //   bool eq = v == Vector128<integer>.Zero
    //
    // to:
    //
    //   bool eq = AdvSimd.Arm64.MaxPairwise(v.AsUInt16(), v.AsUInt16()).GetElement(0) == 0;
    //
    GenTree* op     = nullptr;
    GenTree* opZero = nullptr;
    if (op1->IsVectorZero())
    {
        op     = op2;
        opZero = op1;
    }
    else if (op2->IsVectorZero())
    {
        op     = op1;
        opZero = op2;
    }

    // Special case: "vec ==/!= zero_vector"
    if (!varTypeIsFloating(simdBaseType) && (op != nullptr) && (simdSize != 12))
    {
        GenTree* cmp = op;
        if (simdSize != 8) // we don't need compression for Vector64
        {
            node->Op(1) = op;
            LIR::Use tmp1Use(BlockRange(), &node->Op(1), node);
            ReplaceWithLclVar(tmp1Use);
            op               = node->Op(1);
            GenTree* opClone = comp->gtClone(op);
            BlockRange().InsertAfter(op, opClone);

            cmp = comp->gtNewSimdHWIntrinsicNode(simdType, op, opClone, NI_AdvSimd_Arm64_MaxPairwise, CORINFO_TYPE_UINT,
                                                 simdSize);
            BlockRange().InsertBefore(node, cmp);
            LowerNode(cmp);
        }

        BlockRange().Remove(opZero);

        GenTree* zroCns = comp->gtNewIconNode(0, TYP_INT);
        BlockRange().InsertAfter(cmp, zroCns);

        GenTree* val =
            comp->gtNewSimdHWIntrinsicNode(TYP_LONG, cmp, zroCns, NI_AdvSimd_Extract, CORINFO_TYPE_ULONG, simdSize);
        BlockRange().InsertAfter(zroCns, val);
        LowerNode(val);

        GenTree* cmpZeroCns = comp->gtNewIconNode(0, TYP_LONG);
        BlockRange().InsertAfter(val, cmpZeroCns);

        node->ChangeOper(cmpOp);
        node->gtType        = TYP_INT;
        node->AsOp()->gtOp1 = val;
        node->AsOp()->gtOp2 = cmpZeroCns;
        LowerNodeCC(node, (cmpOp == GT_EQ) ? GenCondition::EQ : GenCondition::NE);
        node->gtType = TYP_VOID;
        node->ClearUnusedValue();
        LowerNode(node);
        return node->gtNext;
    }

    NamedIntrinsic cmpIntrinsic;

    switch (simdBaseType)
    {
        case TYP_BYTE:
        case TYP_UBYTE:
        case TYP_SHORT:
        case TYP_USHORT:
        case TYP_INT:
        case TYP_UINT:
        case TYP_FLOAT:
        {
            cmpIntrinsic = NI_AdvSimd_CompareEqual;
            break;
        }

        case TYP_LONG:
        case TYP_ULONG:
        case TYP_DOUBLE:
        {
            cmpIntrinsic = (simdSize == 8) ? NI_AdvSimd_Arm64_CompareEqualScalar : NI_AdvSimd_Arm64_CompareEqual;
            break;
        }

        default:
        {
            unreached();
        }
    }

    GenTree* cmp = comp->gtNewSimdHWIntrinsicNode(simdType, op1, op2, cmpIntrinsic, simdBaseJitType, simdSize);
    BlockRange().InsertBefore(node, cmp);
    LowerNode(cmp);

    if ((simdBaseType == TYP_FLOAT) && (simdSize == 12))
    {
        // For TYP_SIMD12 we don't want the upper bits to participate in the comparison. So, we will insert all ones
        // into those bits of the result, "as if" the upper bits are equal. Then if all lower bits are equal, we get the
        // expected all-ones result, and will get the expected 0's only where there are non-matching bits.

        GenTree* idxCns = comp->gtNewIconNode(3, TYP_INT);
        BlockRange().InsertAfter(cmp, idxCns);

        GenTree* insCns = comp->gtNewIconNode(-1, TYP_INT);
        BlockRange().InsertAfter(idxCns, insCns);

        GenTree* tmp = comp->gtNewSimdHWIntrinsicNode(simdType, cmp, idxCns, insCns, NI_AdvSimd_Insert,
                                                      CORINFO_TYPE_INT, simdSize);
        BlockRange().InsertAfter(insCns, tmp);
        LowerNode(tmp);

        cmp = tmp;
    }

    if (simdSize != 8) // we don't need compression for Vector64
    {
        GenTree* msk;

        // Save cmp into a temp as we're going to need to pass it twice to MinPairwise
        node->Op(1) = cmp;
        LIR::Use tmp1Use(BlockRange(), &node->Op(1), node);
        ReplaceWithLclVar(tmp1Use);
        cmp               = node->Op(1);
        GenTree* cmpClone = comp->gtClone(cmp);
        BlockRange().InsertAfter(cmp, cmpClone);

        msk = comp->gtNewSimdHWIntrinsicNode(simdType, cmp, cmpClone, NI_AdvSimd_Arm64_MinPairwise, CORINFO_TYPE_UINT,
                                             simdSize);
        BlockRange().InsertAfter(cmpClone, msk);
        LowerNode(msk);

        cmp = msk;
    }

    GenTree* zroCns = comp->gtNewIconNode(0, TYP_INT);
    BlockRange().InsertAfter(cmp, zroCns);

    GenTree* val =
        comp->gtNewSimdHWIntrinsicNode(TYP_LONG, cmp, zroCns, NI_AdvSimd_Extract, CORINFO_TYPE_ULONG, simdSize);
    BlockRange().InsertAfter(zroCns, val);
    LowerNode(val);

    GenTree* bitMskCns = comp->gtNewIconNode(static_cast<ssize_t>(0xffffffffffffffff), TYP_LONG);
    BlockRange().InsertAfter(val, bitMskCns);

    node->ChangeOper(cmpOp);

    node->gtType        = TYP_LONG;
    node->AsOp()->gtOp1 = val;
    node->AsOp()->gtOp2 = bitMskCns;

    // The CompareEqual will set (condition is true) or clear (condition is false) all bits of the respective element
    // The MinAcross then ensures we get either all bits set (all conditions are true) or clear (any condition is false)
    // So, we need to invert the condition from the operation since we compare against zero

    GenCondition cmpCnd = (cmpOp == GT_EQ) ? GenCondition::EQ : GenCondition::NE;
    LowerNodeCC(node, cmpCnd);

    node->gtType = TYP_VOID;
    node->ClearUnusedValue();

    LowerNode(node);
    return node->gtNext;
}

//----------------------------------------------------------------------------------------------
// Lowering::LowerHWIntrinsicCreate: Lowers a Vector64 or Vector128 Create call
//
// Performs the following transformations:
//  1. If all the arguments are constant (including the broadcast case), the vector
//     will be loaded from the data section, or turned into Zero/AllBitsSet, if possible.
//  2. Non-constant broadcasts (argCnt == 1) are turned into DuplicateToVector intrinsics.
//  3. Remaining cases get a chain of "Insert"s, from the second element to the last, where
//     the vector to be inserted into is created with CreateUnsafeScalar from the first element.
//
//  Arguments:
//     node - The hardware intrinsic node.
//
GenTree* Lowering::LowerHWIntrinsicCreate(GenTreeHWIntrinsic* node)
{
    NamedIntrinsic intrinsicId     = node->GetHWIntrinsicId();
    var_types      simdType        = node->TypeGet();
    CorInfoType    simdBaseJitType = node->GetSimdBaseJitType();
    var_types      simdBaseType    = node->GetSimdBaseType();
    unsigned       simdSize        = node->GetSimdSize();
    simd_t         simdVal         = {};

    if ((simdSize == 8) && (simdType == TYP_DOUBLE))
    {
        // TODO-Cleanup: Struct retyping means we have the wrong type here. We need to
        //               manually fix it up so the simdType checks below are correct.
        simdType = TYP_SIMD8;
    }

    assert(varTypeIsSIMD(simdType));
    assert(varTypeIsArithmetic(simdBaseType));
    assert(simdSize != 0);

    bool   isConstant     = GenTreeVecCon::IsHWIntrinsicCreateConstant<simd_t>(node, simdVal);
    bool   isCreateScalar = HWIntrinsicInfo::IsVectorCreateScalar(intrinsicId);
    size_t argCnt         = node->GetOperandCount();

    // Check if we have a cast that we can remove. Note that "IsValidConstForMovImm"
    // will reset Op(1) if it finds such a cast, so we do not need to handle it here.
    // TODO-Casts: why are casts from constants checked for here?
    if (isConstant && (argCnt == 1) && IsValidConstForMovImm(node))
    {
        // Set isConstant to false so we get lowered to a DuplicateToVector
        // intrinsic, which will itself mark the node as contained.
        isConstant = false;
    }

    if (isConstant)
    {
        assert((simdSize == 8) || (simdSize == 12) || (simdSize == 16));

        for (GenTree* arg : node->Operands())
        {
            BlockRange().Remove(arg);
        }

        GenTreeVecCon* vecCon = comp->gtNewVconNode(simdType);

        vecCon->gtSimdVal = simdVal;
        BlockRange().InsertBefore(node, vecCon);

        LIR::Use use;
        if (BlockRange().TryGetUse(node, &use))
        {
            use.ReplaceWith(vecCon);
        }
        else
        {
            vecCon->SetUnusedValue();
        }

        BlockRange().Remove(node);

        return LowerNode(vecCon);
    }
    else if (argCnt == 1)
    {
        if (isCreateScalar)
        {
            GenTree* op1 = node->Op(1);

            GenTree* tmp = comp->gtNewZeroConNode(simdType);
            BlockRange().InsertBefore(op1, tmp);
            LowerNode(tmp);

            GenTree* idx = comp->gtNewIconNode(0);
            BlockRange().InsertAfter(tmp, idx);
            LowerNode(idx);

            node->ResetHWIntrinsicId(NI_AdvSimd_Insert, comp, tmp, idx, op1);
            return LowerNode(node);
        }

        // We have the following (where simd is simd8 or simd16):
        //          /--*  op1  T
        //   node = *  HWINTRINSIC   simd   T Create

        // We will be constructing the following parts:
        //           /--*  op1  T
        //   node  = *  HWINTRINSIC   simd   T DuplicateToVector

        // This is roughly the following managed code:
        //   return AdvSimd.Arm64.DuplicateToVector(op1);

        if (varTypeIsLong(simdBaseType) || (simdBaseType == TYP_DOUBLE))
        {
            node->ChangeHWIntrinsicId((simdType == TYP_SIMD8) ? NI_AdvSimd_Arm64_DuplicateToVector64
                                                              : NI_AdvSimd_Arm64_DuplicateToVector128);
        }
        else
        {
            node->ChangeHWIntrinsicId((simdType == TYP_SIMD8) ? NI_AdvSimd_DuplicateToVector64
                                                              : NI_AdvSimd_DuplicateToVector128);
        }

        return LowerNode(node);
    }

    // We have the following (where simd is simd8 or simd16):
    //          /--*  op1 T
    //          +--*  ... T
    //          +--*  opN T
    //   node = *  HWINTRINSIC   simd   T Create

    // We will be constructing the following parts:
    //          /--*  op1  T
    //   tmp1 = *  HWINTRINSIC   simd8  T CreateScalarUnsafe
    //   ...

    // This is roughly the following managed code:
    //   var tmp1 = Vector64.CreateScalarUnsafe(op1);
    //   ...

    GenTree* tmp1 = InsertNewSimdCreateScalarUnsafeNode(simdType, node->Op(1), simdBaseJitType, simdSize);
    LowerNode(tmp1);

    // We will be constructing the following parts:
    //   ...
    //   idx  =    CNS_INT       int    N
    //          /--*  tmp1 simd
    //          +--*  idx  int
    //          +--*  opN  T
    //   tmp1 = *  HWINTRINSIC   simd   T Insert
    //   ...

    // This is roughly the following managed code:
    //   ...
    //   tmp1 = AdvSimd.Insert(tmp1, N, opN);
    //   ...

    unsigned N   = 0;
    GenTree* opN = nullptr;
    GenTree* idx = nullptr;

    for (N = 1; N < argCnt - 1; N++)
    {
        opN = node->Op(N + 1);

        // Place the insert as early as possible to avoid creating a lot of long lifetimes.
        GenTree* insertionPoint = LIR::LastNode(tmp1, opN);
        idx                     = comp->gtNewIconNode(N);
        tmp1 = comp->gtNewSimdHWIntrinsicNode(simdType, tmp1, idx, opN, NI_AdvSimd_Insert, simdBaseJitType, simdSize);
        BlockRange().InsertAfter(insertionPoint, idx, tmp1);
        LowerNode(tmp1);
    }

    assert(N == (argCnt - 1));

    // For the last insert, we will reuse the existing node and so handle it here, outside the loop.
    opN = node->Op(argCnt);
    idx = comp->gtNewIconNode(N);
    BlockRange().InsertBefore(opN, idx);

    node->ResetHWIntrinsicId(NI_AdvSimd_Insert, comp, tmp1, idx, opN);

    return LowerNode(node);
}

//----------------------------------------------------------------------------------------------
// Lowering::LowerHWIntrinsicDot: Lowers a Vector64 or Vector128 Dot call
//
//  Arguments:
//     node - The hardware intrinsic node.
//
GenTree* Lowering::LowerHWIntrinsicDot(GenTreeHWIntrinsic* node)
{
    NamedIntrinsic intrinsicId     = node->GetHWIntrinsicId();
    CorInfoType    simdBaseJitType = node->GetSimdBaseJitType();
    var_types      simdBaseType    = node->GetSimdBaseType();
    unsigned       simdSize        = node->GetSimdSize();
    var_types      simdType        = Compiler::getSIMDTypeForSize(simdSize);

    assert((intrinsicId == NI_Vector64_Dot) || (intrinsicId == NI_Vector128_Dot));
    assert(varTypeIsSIMD(simdType));
    assert(varTypeIsArithmetic(simdBaseType));
    assert(simdSize != 0);
    assert(varTypeIsSIMD(node));

    GenTree* op1 = node->Op(1);
    GenTree* op2 = node->Op(2);

    // Spare GenTrees to be used for the lowering logic below
    // Defined upfront to avoid naming conflicts, etc...
    GenTree* idx  = nullptr;
    GenTree* tmp1 = nullptr;
    GenTree* tmp2 = nullptr;

    if (simdSize == 12)
    {
        assert(simdBaseType == TYP_FLOAT);

        // For 12 byte SIMD, we need to clear the upper 4 bytes:
        //   idx  =    CNS_INT       int    0x03
        //   tmp1 = *  CNS_DBL       float  0.0
        //          /--*  op1  simd16
        //          +--*  idx  int
        //          +--*  tmp1 simd16
        //   op1  = *  HWINTRINSIC   simd16 T Insert
        //   ...

        // This is roughly the following managed code:
        //    op1 = AdvSimd.Insert(op1, 0x03, 0.0f);
        //    ...

        idx = comp->gtNewIconNode(0x03, TYP_INT);
        BlockRange().InsertAfter(op1, idx);

        tmp1 = comp->gtNewZeroConNode(TYP_FLOAT);
        BlockRange().InsertAfter(idx, tmp1);
        LowerNode(tmp1);

        op1 = comp->gtNewSimdHWIntrinsicNode(simdType, op1, idx, tmp1, NI_AdvSimd_Insert, simdBaseJitType, simdSize);
        BlockRange().InsertAfter(tmp1, op1);
        LowerNode(op1);

        idx = comp->gtNewIconNode(0x03, TYP_INT);
        BlockRange().InsertAfter(op2, idx);

        tmp2 = comp->gtNewZeroConNode(TYP_FLOAT);
        BlockRange().InsertAfter(idx, tmp2);
        LowerNode(tmp2);

        op2 = comp->gtNewSimdHWIntrinsicNode(simdType, op2, idx, tmp2, NI_AdvSimd_Insert, simdBaseJitType, simdSize);
        BlockRange().InsertAfter(tmp2, op2);
        LowerNode(op2);
    }

    // We will be constructing the following parts:
    //   ...
    //          /--*  op1  simd16
    //          +--*  op2  simd16
    //   tmp1 = *  HWINTRINSIC   simd16 T Multiply
    //   ...

    // This is roughly the following managed code:
    //   ...
    //   var tmp1 = AdvSimd.Multiply(op1, op2);
    //   ...

    NamedIntrinsic multiply = NI_AdvSimd_Multiply;

    if (simdBaseType == TYP_DOUBLE)
    {
        multiply = (simdSize == 8) ? NI_AdvSimd_MultiplyScalar : NI_AdvSimd_Arm64_Multiply;
    }
    assert(!varTypeIsLong(simdBaseType));

    tmp1 = comp->gtNewSimdHWIntrinsicNode(simdType, op1, op2, multiply, simdBaseJitType, simdSize);
    BlockRange().InsertBefore(node, tmp1);
    LowerNode(tmp1);

    if (varTypeIsFloating(simdBaseType))
    {
        if ((simdSize != 8) || (simdBaseType == TYP_FLOAT))
        {
            // We will be constructing the following parts:
            //   ...
            //          /--*  tmp1 simd16
            //          *  STORE_LCL_VAR simd16
            //   tmp1 =    LCL_VAR       simd16
            //   tmp2 =    LCL_VAR       simd16
            //   ...

            // This is roughly the following managed code:
            //   ...
            //   var tmp2 = tmp1;
            //   ...

            node->Op(1) = tmp1;
            LIR::Use tmp1Use(BlockRange(), &node->Op(1), node);
            ReplaceWithLclVar(tmp1Use);
            tmp1 = node->Op(1);

            tmp2 = comp->gtClone(tmp1);
            BlockRange().InsertAfter(tmp1, tmp2);
        }

        if (simdSize == 8)
        {
            if (simdBaseType == TYP_FLOAT)
            {
                // We will be constructing the following parts:
                //   ...
                //          /--*  tmp1 simd8
                //          +--*  tmp2 simd8
                //   tmp1 = *  HWINTRINSIC   simd8  T AddPairwise
                //   ...

                // This is roughly the following managed code:
                //   ...
                //   var tmp1 = AdvSimd.AddPairwise(tmp1, tmp2);
                //   ...

                tmp1 = comp->gtNewSimdHWIntrinsicNode(simdType, tmp1, tmp2, NI_AdvSimd_AddPairwise, simdBaseJitType,
                                                      simdSize);
                BlockRange().InsertAfter(tmp2, tmp1);
                LowerNode(tmp1);
            }
            else
            {
                // No pairs to add for double, as its a single element
            }
        }
        else
        {
            assert((simdSize == 12) || (simdSize == 16));

            // We will be constructing the following parts:
            //   ...
            //          /--*  tmp1 simd16
            //          +--*  tmp2 simd16
            //   tmp2 = *  HWINTRINSIC   simd16 T AddPairwise
            //   ...

            // This is roughly the following managed code:
            //   ...
            //   var tmp1 = AdvSimd.Arm64.AddPairwise(tmp1, tmp2);
            //   ...

            tmp1 = comp->gtNewSimdHWIntrinsicNode(simdType, tmp1, tmp2, NI_AdvSimd_Arm64_AddPairwise, simdBaseJitType,
                                                  simdSize);
            BlockRange().InsertAfter(tmp2, tmp1);
            LowerNode(tmp1);

            if (simdBaseType == TYP_FLOAT)
            {
                // Float needs an additional pairwise add to finish summing the parts
                // The first will have summed e0 with e1 and e2 with e3 and then repeats that for the upper half
                // So, we will have a vector that looks like this:
                //    < e0 + e1, e2 + e3, e0 + e1, e2 + e3>
                // Doing a second horizontal add with itself will then give us
                //    e0 + e1 + e2 + e3 in all elements of the vector

                // We will be constructing the following parts:
                //   ...
                //          /--*  tmp1 simd16
                //          *  STORE_LCL_VAR simd16
                //   tmp1 =    LCL_VAR       simd16
                //   tmp2 =    LCL_VAR       simd16
                //          /--*  tmp1 simd16
                //          +--*  tmp2 simd16
                //   tmp2 = *  HWINTRINSIC   simd16 T AddPairwise
                //   ...

                // This is roughly the following managed code:
                //   ...
                //   var tmp2 = tmp1;
                //   var tmp1 = AdvSimd.Arm64.AddPairwise(tmp1, tmp2);
                //   ...

                node->Op(1) = tmp1;
                LIR::Use tmp1Use(BlockRange(), &node->Op(1), node);
                ReplaceWithLclVar(tmp1Use);
                tmp1 = node->Op(1);

                tmp2 = comp->gtClone(tmp1);
                BlockRange().InsertAfter(tmp1, tmp2);

                tmp1 = comp->gtNewSimdHWIntrinsicNode(simdType, tmp1, tmp2, NI_AdvSimd_Arm64_AddPairwise,
                                                      simdBaseJitType, simdSize);
                BlockRange().InsertAfter(tmp2, tmp1);
                LowerNode(tmp1);
            }
        }

        tmp2 = tmp1;
    }
    else
    {
        assert(varTypeIsIntegral(simdBaseType));

        if ((simdSize == 8) && ((simdBaseType == TYP_INT) || (simdBaseType == TYP_UINT)))
        {
            // We will be constructing the following parts:
            //   ...
            //          /--*  tmp1 simd16
            //          *  STORE_LCL_VAR simd16
            //   tmp1 =    LCL_VAR       simd16
            //   tmp2 =    LCL_VAR       simd16
            //   ...

            // This is roughly the following managed code:
            //   ...
            //   var tmp2 = tmp1;
            //   ...

            node->Op(1) = tmp1;
            LIR::Use tmp1Use(BlockRange(), &node->Op(1), node);
            ReplaceWithLclVar(tmp1Use);
            tmp1 = node->Op(1);

            tmp2 = comp->gtClone(tmp1);
            BlockRange().InsertAfter(tmp1, tmp2);

            // We will be constructing the following parts:
            //   ...
            //          /--*  tmp1 simd16
            //          /--*  tmp2 simd16
            //   tmp2 = *  HWINTRINSIC   simd8 T AddPairwise
            //   ...

            // This is roughly the following managed code:
            //   ...
            //   var tmp2 = AdvSimd.AddPairwise(tmp1, tmp2);
            //   ...

            tmp1 =
                comp->gtNewSimdHWIntrinsicNode(simdType, tmp1, tmp2, NI_AdvSimd_AddPairwise, simdBaseJitType, simdSize);
            BlockRange().InsertAfter(tmp2, tmp1);
            LowerNode(tmp1);

            tmp2 = tmp1;
        }
        else
        {
            // We will be constructing the following parts:
            //   ...
            //          /--*  tmp1 simd16
            //   tmp2 = *  HWINTRINSIC   simd16 T AddAcross
            //   ...

            // This is roughly the following managed code:
            //   ...
            //   var tmp2 = AdvSimd.Arm64.AddAcross(tmp1);
            //   ...

            tmp2 =
                comp->gtNewSimdHWIntrinsicNode(TYP_SIMD8, tmp1, NI_AdvSimd_Arm64_AddAcross, simdBaseJitType, simdSize);
            BlockRange().InsertAfter(tmp1, tmp2);
            LowerNode(tmp2);
        }
    }

    // We're producing a vector result, so just return the result directly
    LIR::Use use;

    if (BlockRange().TryGetUse(node, &use))
    {
        use.ReplaceWith(tmp2);
    }
    else
    {
        tmp2->SetUnusedValue();
    }

    BlockRange().Remove(node);
    return tmp2->gtNext;
}
#endif // FEATURE_HW_INTRINSICS

//------------------------------------------------------------------------
// Containment analysis
//------------------------------------------------------------------------

//------------------------------------------------------------------------
// ContainCheckCallOperands: Determine whether operands of a call should be contained.
//
// Arguments:
//    call       - The call node of interest
//
// Return Value:
//    None.
//
void Lowering::ContainCheckCallOperands(GenTreeCall* call)
{
    // There are no contained operands for arm.
}

//------------------------------------------------------------------------
// ContainCheckStoreIndir: determine whether the sources of a STOREIND node should be contained.
//
// Arguments:
//    node - pointer to the node
//
void Lowering::ContainCheckStoreIndir(GenTreeStoreInd* node)
{
#ifdef TARGET_ARM64
    GenTree* src = node->Data();
    if (src->IsIntegralConst(0))
    {
        // an integer zero for 'src' can be contained.
        MakeSrcContained(node, src);
    }
#endif // TARGET_ARM64
    ContainCheckIndir(node);
}

//------------------------------------------------------------------------
// ContainCheckIndir: Determine whether operands of an indir should be contained.
//
// Arguments:
//    indirNode - The indirection node of interest
//
// Notes:
//    This is called for both store and load indirections.
//
// Return Value:
//    None.
//
void Lowering::ContainCheckIndir(GenTreeIndir* indirNode)
{
    // If this is the rhs of a block copy it will be handled when we handle the store.
    if (indirNode->TypeIs(TYP_STRUCT))
    {
        return;
    }

#ifdef FEATURE_SIMD
    // If indirTree is of TYP_SIMD12, don't mark addr as contained
    // so that it always get computed to a register.  This would
    // mean codegen side logic doesn't need to handle all possible
    // addr expressions that could be contained.
    //
    // TODO-ARM64-CQ: handle other addr mode expressions that could be marked
    // as contained.
    if (indirNode->TypeIs(TYP_SIMD12))
    {
        return;
    }
#endif // FEATURE_SIMD

    GenTree* addr = indirNode->Addr();

    if (addr->OperIs(GT_LEA) && IsInvariantInRange(addr, indirNode))
    {
        bool makeContained = true;

#ifdef TARGET_ARM
        // ARM floating-point load/store doesn't support a form similar to integer
        // ldr Rdst, [Rbase + Roffset] with offset in a register. The only supported
        // form is vldr Rdst, [Rbase + imm] with a more limited constraint on the imm.
        GenTreeAddrMode* lea = addr->AsAddrMode();
        int              cns = lea->Offset();
        if (lea->HasIndex() || !emitter::emitIns_valid_imm_for_vldst_offset(cns))
        {
            if (indirNode->OperIs(GT_STOREIND))
            {
                if (varTypeIsFloating(indirNode->AsStoreInd()->Data()))
                {
                    makeContained = false;
                }
            }
            else if (indirNode->OperIs(GT_IND))
            {
                if (varTypeIsFloating(indirNode))
                {
                    makeContained = false;
                }
            }
        }
#endif // TARGET_ARM

        if (makeContained)
        {
            MakeSrcContained(indirNode, addr);
        }
    }
    else if (addr->OperIs(GT_LCL_ADDR) && !indirNode->OperIs(GT_NULLCHECK) &&
             IsContainableLclAddr(addr->AsLclFld(), indirNode->Size()))
    {
        // These nodes go into an addr mode:
        // - GT_LCL_ADDR is a stack addr mode.
        MakeSrcContained(indirNode, addr);
    }
#ifdef TARGET_ARM64
    else if (addr->IsIconHandle(GTF_ICON_TLS_HDL))
    {
        MakeSrcContained(indirNode, addr);
    }
#endif // TARGET_ARM64
}

//------------------------------------------------------------------------
// ContainCheckBinary: Determine whether a binary op's operands should be contained.
//
// Arguments:
//    node - the node we care about
//
void Lowering::ContainCheckBinary(GenTreeOp* node)
{
    GenTree* op1 = node->gtGetOp1();
    GenTree* op2 = node->gtGetOp2();

    if (CheckImmedAndMakeContained(node, op2))
    {
        return;
    }

    if (node->OperIsCommutative() && CheckImmedAndMakeContained(node, op1))
    {
        MakeSrcContained(node, op1);
        std::swap(node->gtOp1, node->gtOp2);
        return;
    }

#ifdef TARGET_ARM64
    if (comp->opts.OptimizationEnabled())
    {
        if (IsContainableUnaryOrBinaryOp(node, op2))
        {
            if (node->OperIs(GT_ADD, GT_SUB, GT_CMP) && op2->OperIs(GT_CAST))
            {
                // We want to prefer the combined op here over containment of the cast op
                op2->AsCast()->CastOp()->ClearContained();
            }

            MakeSrcContained(node, op2);
            return;
        }
        if (node->OperIsCommutative() && IsContainableUnaryOrBinaryOp(node, op1))
        {
            if (node->OperIs(GT_ADD, GT_SUB, GT_CMP) && op1->OperIs(GT_CAST))
            {
                // We want to prefer the combined op here over containment of the cast op
                op1->AsCast()->CastOp()->ClearContained();
            }

            MakeSrcContained(node, op1);
            std::swap(node->gtOp1, node->gtOp2);
            return;
        }
    }
#endif
}

//------------------------------------------------------------------------
// ContainCheckMul: Determine whether a mul op's operands should be contained.
//
// Arguments:
//    node - the node we care about
//
void Lowering::ContainCheckMul(GenTreeOp* node)
{
    ContainCheckBinary(node);
}

//------------------------------------------------------------------------
// ContainCheckDivOrMod: determine which operands of a div/mod should be contained.
//
// Arguments:
//    node - the node we care about
//
void Lowering::ContainCheckDivOrMod(GenTreeOp* node)
{
    assert(node->OperIs(GT_DIV, GT_UDIV, GT_MOD));

    // ARM doesn't have a div instruction with an immediate operand
}

//------------------------------------------------------------------------
// ContainCheckShiftRotate: Determine whether a mul op's operands should be contained.
//
// Arguments:
//    node - the node we care about
//
void Lowering::ContainCheckShiftRotate(GenTreeOp* node)
{
    GenTree* shiftBy = node->gtOp2;
    assert(node->OperIsShiftOrRotate());

#ifdef TARGET_ARM
    GenTree* source = node->gtOp1;
    if (node->OperIs(GT_LSH_HI, GT_RSH_LO))
    {
        assert(source->OperIs(GT_LONG));
        MakeSrcContained(node, source);
    }
#endif // TARGET_ARM

    if (shiftBy->IsCnsIntOrI())
    {
        MakeSrcContained(node, shiftBy);
    }
}

//------------------------------------------------------------------------
// ContainCheckStoreLoc: determine whether the source of a STORE_LCL* should be contained.
//
// Arguments:
//    node - pointer to the node
//
void Lowering::ContainCheckStoreLoc(GenTreeLclVarCommon* storeLoc) const
{
    assert(storeLoc->OperIsLocalStore());
    GenTree* op1 = storeLoc->gtGetOp1();

    if (op1->OperIs(GT_BITCAST))
    {
        // If we know that the source of the bitcast will be in a register, then we can make
        // the bitcast itself contained. This will allow us to store directly from the other
        // type if this node doesn't get a register.
        GenTree* bitCastSrc = op1->gtGetOp1();
        if (!bitCastSrc->isContained() && !bitCastSrc->IsRegOptional())
        {
            op1->SetContained();
            return;
        }
    }

    const LclVarDsc* varDsc = comp->lvaGetDesc(storeLoc);

#ifdef FEATURE_SIMD
    if (storeLoc->TypeIs(TYP_SIMD8, TYP_SIMD12))
    {
        // If this is a store to memory, we can initialize a zero vector in memory from REG_ZR.
        if ((op1->IsIntegralConst(0) || op1->IsVectorZero()) && varDsc->lvDoNotEnregister)
        {
            MakeSrcContained(storeLoc, op1);
        }
        return;
    }
#endif // FEATURE_SIMD

#ifdef TARGET_ARM64
    if (IsContainableImmed(storeLoc, op1))
    {
        MakeSrcContained(storeLoc, op1);
    }
#else
    // If the source is a containable immediate, make it contained, unless it is
    // an int-size or larger store of zero to memory, because we can generate smaller code
    // by zeroing a register and then storing it.
    var_types type = varDsc->GetRegisterType(storeLoc);
    if (IsContainableImmed(storeLoc, op1) && (!op1->IsIntegralConst(0) || varTypeIsSmall(type)))
    {
        MakeSrcContained(storeLoc, op1);
    }
    else if (op1->OperIs(GT_LONG))
    {
        MakeSrcContained(storeLoc, op1);
    }
#endif // TARGET_ARM
}

//------------------------------------------------------------------------
// ContainCheckCast: determine whether the source of a CAST node should be contained.
//
// Arguments:
//    node - pointer to the node
//
void Lowering::ContainCheckCast(GenTreeCast* node)
{
    GenTree*  castOp     = node->CastOp();
    var_types castToType = node->CastToType();

    if (comp->opts.OptimizationEnabled() && !node->gtOverflow() && varTypeIsIntegral(castOp) &&
        varTypeIsIntegral(castToType))
    {
        // Most integral casts can be re-expressed as loads, except those that would be changing the sign.
        if (!varTypeIsSmall(castOp) || (varTypeIsUnsigned(castOp) == node->IsZeroExtending()))
        {
            bool srcIsContainable = false;

            // Make sure to only contain indirections codegen can handle.
            if (castOp->OperIs(GT_IND))
            {
                GenTreeIndir* indir = castOp->AsIndir();

                if (!indir->IsVolatile() && !indir->IsUnaligned())
                {
                    GenTree* addr = indir->Addr();

                    if (!addr->isContained())
                    {
                        srcIsContainable = true;
                    }
                    else if (addr->OperIs(GT_LEA) && !addr->AsAddrMode()->HasIndex())
                    {
                        var_types loadType = varTypeIsSmall(castToType) ? castToType : castOp->TypeGet();

                        if (emitter::emitIns_valid_imm_for_ldst_offset(addr->AsAddrMode()->Offset(),
                                                                       emitTypeSize(loadType)))
                        {
                            srcIsContainable = true;
                        }
                    }
                }
            }
            else
            {
                assert(castOp->OperIsLocalRead() || !IsContainableMemoryOp(castOp));
                srcIsContainable = true;
            }

            if (srcIsContainable)
            {
                if (IsContainableMemoryOp(castOp) && IsSafeToContainMem(node, castOp))
                {
                    MakeSrcContained(node, castOp);
                }
                else if (IsSafeToMarkRegOptional(node, castOp))
                {
                    castOp->SetRegOptional();
                }
            }
        }
    }

#ifdef TARGET_ARM
    if (varTypeIsLong(castOp))
    {
        assert(castOp->OperIs(GT_LONG));
        MakeSrcContained(node, castOp);
    }
#endif // TARGET_ARM
}

//------------------------------------------------------------------------
// ContainCheckCompare: determine whether the sources of a compare node should be contained.
//
// Arguments:
//    node - pointer to the node
//
void Lowering::ContainCheckCompare(GenTreeOp* cmp)
{
    GenTree* op1 = cmp->gtGetOp1();
    GenTree* op2 = cmp->gtGetOp2();

    if (CheckImmedAndMakeContained(cmp, op2))
        return;

    if (cmp->OperIsCompare() && CheckImmedAndMakeContained(cmp, op1))
    {
        std::swap(cmp->gtOp1, cmp->gtOp2);
        cmp->SetOper(cmp->SwapRelop(cmp->gtOper));
        return;
    }

#ifdef TARGET_ARM64
    if (comp->opts.OptimizationEnabled() && (cmp->OperIsCompare() || cmp->OperIs(GT_CMP)))
    {
        auto forceCastOpInRegister = [](GenTree* op) {
            // If the compare contains a cast, make sure that cast node definitely does not become
            // a memory operation, as we won't be able to contain it in CodeGen if this happens.
            // The node being cast must have a register assigned.
            GenTree* cast = nullptr;
            if (op->OperIs(GT_CAST))
            {
                // cmp (extended-register): GT_EQ -> GT_CAST -> ...
                cast = op;
            }
            else if (op->OperIs(GT_NEG) && op->gtGetOp1()->OperIs(GT_CAST))
            {
                // cmn (extended-register): GT_EQ -> GT_NEG -> GT_CAST -> ...
                cast = op->gtGetOp1();
            }
            if (cast)
            {
                cast->AsCast()->CastOp()->ClearRegOptional();
            }
        };

        if (IsContainableUnaryOrBinaryOp(cmp, op2))
        {
            if (cmp->OperIsCmpCompare())
            {
                forceCastOpInRegister(op2);
            }

            MakeSrcContained(cmp, op2);
            return;
        }

        if (IsContainableUnaryOrBinaryOp(cmp, op1))
        {
            if (cmp->OperIsCmpCompare())
            {
                forceCastOpInRegister(op1);
            }

            MakeSrcContained(cmp, op1);
            std::swap(cmp->gtOp1, cmp->gtOp2);
            if (cmp->OperIsCompare())
            {
                cmp->SetOper(cmp->SwapRelop(cmp->gtOper));
            }
            return;
        }
    }
#endif
}

#ifdef TARGET_ARM64
//------------------------------------------------------------------------
// TruthifyingFlags: Get a flags immediate that will make a specified condition true.
//
// Arguments:
//    condition - the condition.
//
// Returns:
//    A flags immediate that, if those flags were set, would cause the specified condition to be true.
//
insCflags Lowering::TruthifyingFlags(GenCondition condition)
{
    switch (condition.GetCode())
    {
        case GenCondition::EQ:
            return INS_FLAGS_Z;
        case GenCondition::NE:
            return INS_FLAGS_NONE;
        case GenCondition::SGE:
            return INS_FLAGS_Z;
        case GenCondition::SGT:
            return INS_FLAGS_NONE;
        case GenCondition::SLT:
            return INS_FLAGS_NC;
        case GenCondition::SLE:
            return INS_FLAGS_NZC;
        case GenCondition::UGE:
            return INS_FLAGS_C;
        case GenCondition::UGT:
            return INS_FLAGS_C;
        case GenCondition::ULT:
            return INS_FLAGS_NONE;
        case GenCondition::ULE:
            return INS_FLAGS_Z;
        default:
            NO_WAY("unexpected condition type");
            return INS_FLAGS_NONE;
    }
}
#endif // TARGET_ARM64

//------------------------------------------------------------------------
// ContainCheckSelect : determine whether the source of a select should be contained.
//
// Arguments:
//    node - pointer to the node
//
void Lowering::ContainCheckSelect(GenTreeOp* node)
{
#ifdef TARGET_ARM
    noway_assert(!"GT_SELECT nodes are not supported on arm32");
#else
    GenTree* op1 = node->gtOp1;
    GenTree* op2 = node->gtOp2;

    if (op1->IsIntegralConst(0))
    {
        MakeSrcContained(node, op1);
    }
    if (op2->IsIntegralConst(0))
    {
        MakeSrcContained(node, op2);
    }
#endif
}

#ifdef TARGET_ARM64
//------------------------------------------------------------------------
// ContainCheckNeg : determine whether the source of a neg should be contained.
//
// Arguments:
//    node - pointer to the node
//
void Lowering::ContainCheckNeg(GenTreeOp* neg)
{
    if (neg->isContained())
        return;

    if (!varTypeIsIntegral(neg))
        return;

    if ((neg->gtFlags & GTF_SET_FLAGS))
        return;

    GenTree* childNode = neg->gtGetOp1();
    if (childNode->OperIs(GT_MUL))
    {
        // Find - (a * b)
        if (childNode->gtGetOp1()->isContained() || childNode->gtGetOp2()->isContained())
            return;

        if (childNode->gtOverflow())
            return;

        if (!varTypeIsIntegral(childNode))
            return;

        if ((childNode->gtFlags & GTF_SET_FLAGS))
            return;

        if (IsInvariantInRange(childNode, neg))
        {
            MakeSrcContained(neg, childNode);
        }
    }
    else if (comp->opts.OptimizationEnabled() && childNode->OperIs(GT_LSH, GT_RSH, GT_RSZ) &&
             IsContainableUnaryOrBinaryOp(neg, childNode))
    {
        MakeSrcContained(neg, childNode);
    }
}

//------------------------------------------------------------------------
// ContainCheckNot : determine whether the source of a not should be contained.
//
// Arguments:
//    notOp - pointer to the node
//
void Lowering::ContainCheckNot(GenTreeOp* notOp)
{
    if (notOp->isContained())
        return;

    if (!varTypeIsIntegral(notOp))
        return;

    if ((notOp->gtFlags & GTF_SET_FLAGS))
        return;

    GenTree* childNode = notOp->gtGetOp1();
    if (comp->opts.OptimizationEnabled() && childNode->OperIs(GT_LSH, GT_RSH, GT_RSZ) &&
        IsContainableUnaryOrBinaryOp(notOp, childNode))
    {
        MakeSrcContained(notOp, childNode);
    }
}

//----------------------------------------------------------------------------------------------
// TryLowerCselToCSOp: Try converting SELECT/SELECTCC to SELECT_?/SELECT_?CC. Conversion is possible only if
// one of the operands of the select node is one of GT_NEG, GT_NOT or GT_ADD.
//
// Arguments:
//     select - The select node that is now SELECT or SELECTCC
//     cond   - The condition node that SELECT or SELECTCC uses
//
void Lowering::TryLowerCselToCSOp(GenTreeOp* select, GenTree* cond)
{
    assert(select->OperIs(GT_SELECT, GT_SELECTCC));

    bool     shouldReverseCondition;
    GenTree* operatedVal;
    GenTree* nonOperatedVal;
    GenTree* nodeToRemove;
    GenTree* trueVal  = select->gtOp1;
    GenTree* falseVal = select->gtOp2;

    // Determine the resulting operation type.
    genTreeOps resultingOp;
    if (trueVal->OperIs(GT_NEG) || falseVal->OperIs(GT_NEG))
    {
        resultingOp            = GT_SELECT_NEG;
        shouldReverseCondition = trueVal->OperIs(GT_NEG);
    }
    else if (trueVal->OperIs(GT_NOT) || falseVal->OperIs(GT_NOT))
    {
        resultingOp            = GT_SELECT_INV;
        shouldReverseCondition = trueVal->OperIs(GT_NOT);
    }
    else
    {
        assert(trueVal->OperIs(GT_ADD) || falseVal->OperIs(GT_ADD));
        resultingOp            = GT_SELECT_INC;
        shouldReverseCondition = trueVal->OperIs(GT_ADD);
    }

    // Values to which the operation are applied must come last.
    if (shouldReverseCondition)
    {
        operatedVal    = trueVal->gtGetOp1();
        nonOperatedVal = falseVal;
        nodeToRemove   = trueVal;
    }
    else
    {
        operatedVal    = falseVal->gtGetOp1();
        nonOperatedVal = trueVal;
        nodeToRemove   = falseVal;
    }

    if (shouldReverseCondition && !cond->OperIsCompare() && select->OperIs(GT_SELECT))
    {
        // Non-compare nodes add additional GT_NOT node after reversing.
        // This would remove gains from this optimisation so don't proceed.
        return;
    }

    // For Csinc candidates, the second argument of the GT_ADD must be +1 (increment).
    if (resultingOp == GT_SELECT_INC &&
        !(nodeToRemove->gtGetOp2()->IsCnsIntOrI() && nodeToRemove->gtGetOp2()->AsIntCon()->IconValue() == 1))
    {
        return;
    }

    // Check that we are safe to move both values.
    if (!(IsInvariantInRange(operatedVal, select) && IsInvariantInRange(nonOperatedVal, select)))
    {
        return;
    }

    // Passed all checks, move on to block modification.
    // If this is a Cinc candidate, we must remove the dangling second argument node.
    if (resultingOp == GT_SELECT_INC)
    {
        BlockRange().Remove(nodeToRemove->gtGetOp2());
        nodeToRemove->AsOp()->gtOp2 = nullptr;
    }

    // As the select node would handle the operation, the op is not required.
    // If a value is contained in the negate/invert/increment op, it cannot be contained anymore.
    BlockRange().Remove(nodeToRemove);
    operatedVal->ClearContained();
    select->gtOp1 = nonOperatedVal;
    select->gtOp2 = operatedVal;

    if (select->OperIs(GT_SELECT))
    {
        if (shouldReverseCondition)
        {
            GenTree* revCond = comp->gtReverseCond(cond);
            assert(cond == revCond); // Ensure `gtReverseCond` did not create a new node.
        }
        select->SetOper(resultingOp);
    }
    else
    {
        GenTreeOpCC* selectcc   = select->AsOpCC();
        GenCondition selectCond = selectcc->gtCondition;
        if (shouldReverseCondition)
        {
            // Reverse the condition so that op2 will be selected
            selectcc->gtCondition = GenCondition::Reverse(selectCond);
        }

        // Convert the resulting operation into the equivalent CC form.
        switch (resultingOp)
        {
            case GT_SELECT_NEG:
                resultingOp = GT_SELECT_NEGCC;
                break;
            case GT_SELECT_INV:
                resultingOp = GT_SELECT_INVCC;
                break;
            case GT_SELECT_INC:
                resultingOp = GT_SELECT_INCCC;
                break;
            default:
                assert(false);
        }
        selectcc->SetOper(resultingOp);
    }

#ifdef DEBUG
    JITDUMP("Converted to ");
    if (comp->verbose)
        comp->gtDispNodeName(select);
    JITDUMP(":\n");
    DISPTREERANGE(BlockRange(), select);
    JITDUMP("\n");
#endif
}

//----------------------------------------------------------------------------------------------
// TryLowerCnsIntCselToCinc: Try converting SELECT/SELECTCC to SELECT_INC/SELECT_INCCC.
// Conversion is possible only if both the trueVal and falseVal are integer constants and abs(trueVal - falseVal) = 1.
//
// Arguments:
//     select - The select node that is now SELECT or SELECTCC
//     cond   - The condition node that SELECT or SELECTCC uses
//
void Lowering::TryLowerCnsIntCselToCinc(GenTreeOp* select, GenTree* cond)
{
    assert(select->OperIs(GT_SELECT, GT_SELECTCC));

    GenTree* trueVal  = select->gtOp1;
    GenTree* falseVal = select->gtOp2;
    size_t   op1Val   = (size_t)trueVal->AsIntCon()->IconValue();
    size_t   op2Val   = (size_t)falseVal->AsIntCon()->IconValue();

    if ((op1Val + 1 == op2Val) || (op2Val + 1 == op1Val))
    {
        const bool shouldReverseCondition = (op1Val + 1 == op2Val);

        if (select->OperIs(GT_SELECT))
        {
            if (shouldReverseCondition)
            {
                // Reverse the condition so that op2 will be selected
                if (!cond->OperIsCompare())
                {
                    // Non-compare nodes add additional GT_NOT node after reversing.
                    // This would remove gains from this optimisation so don't proceed.
                    return;
                }
                GenTree* revCond = comp->gtReverseCond(cond);
                assert(cond == revCond); // Ensure `gtReverseCond` did not create a new node.
            }
            BlockRange().Remove(select->gtOp2, true);
            select->gtOp2 = nullptr;
            select->SetOper(GT_SELECT_INC);
            JITDUMP("Converted to: GT_SELECT_INC\n");
            DISPTREERANGE(BlockRange(), select);
            JITDUMP("\n");
        }
        else
        {
            GenTreeOpCC* selectcc   = select->AsOpCC();
            GenCondition selectCond = selectcc->gtCondition;

            if (shouldReverseCondition)
            {
                // Reverse the condition so that op2 will be selected
                selectcc->gtCondition = GenCondition::Reverse(selectCond);
            }
            else
            {
                std::swap(selectcc->gtOp1, selectcc->gtOp2);
            }

            BlockRange().Remove(selectcc->gtOp2, true);
            selectcc->gtOp2 = nullptr;
            selectcc->SetOper(GT_SELECT_INCCC);
            JITDUMP("Converted to: GT_SELECT_INCCC\n");
            DISPTREERANGE(BlockRange(), selectcc);
            JITDUMP("\n");
        }
    }
}

//----------------------------------------------------------------------------------------------
// TryLowerAddSubToCombinedMulOp: Attempt to convert ADD and SUB nodes to a combined multiply
// and add/sub operation. Conversion can only happen if the operands to the
// operation meet the following criteria:
// - One op is a MUL_LONG containing two integer operands, and the other is a long.
//
// Arguments:
//    op   - The ADD or SUB node to attempt an optimisation on.
//    next - [out] Next node to lower if this function returns true
//
// Return Value:
//    false if no changes were made
//
bool Lowering::TryLowerAddSubToMulLongOp(GenTreeOp* op, GenTree** next)
{
    assert(op->OperIs(GT_ADD, GT_SUB));

    if (!comp->opts.OptimizationEnabled())
        return false;

    if (op->isContained())
        return false;

    if (!varTypeIsIntegral(op))
        return false;

    if ((op->gtFlags & GTF_SET_FLAGS) != 0)
        return false;

    if (op->gtOverflow())
        return false;

    GenTree* op1 = op->gtGetOp1();
    GenTree* op2 = op->gtGetOp2();

    // Select which operation is the MUL_LONG and which is the add value.
    GenTreeOp* mul;
    GenTree*   addVal;
    if (op1->OperIs(GT_MUL_LONG))
    {
        // For subtractions, the multiply must be second, as [u/s]msubl performs:
        // addValue - (mulValue1 * mulValue2)
        if (op->OperIs(GT_SUB))
        {
            return false;
        }

        mul    = op1->AsOp();
        addVal = op2;
    }
    else if (op2->OperIs(GT_MUL_LONG))
    {
        mul    = op2->AsOp();
        addVal = op1;
    }
    else
    {
        // Exit if neither operation are GT_MUL_LONG.
        return false;
    }

    // Additional value must be of long size.
    if (!addVal->TypeIs(TYP_LONG))
        return false;

    // Mul values must both be integers.
    if (!genActualTypeIsInt(mul->gtOp1) || !genActualTypeIsInt(mul->gtOp2))
        return false;

    // The multiply must evaluate to the same thing if moved.
    if (!IsInvariantInRange(mul, op))
        return false;

    // Create the new node and replace the original.
    NamedIntrinsic intrinsicId =
        op->OperIs(GT_ADD) ? NI_ArmBase_Arm64_MultiplyLongAdd : NI_ArmBase_Arm64_MultiplyLongSub;
    GenTreeHWIntrinsic* outOp = comp->gtNewScalarHWIntrinsicNode(TYP_LONG, mul->gtOp1, mul->gtOp2, addVal, intrinsicId);
    outOp->SetSimdBaseJitType(mul->IsUnsigned() ? CORINFO_TYPE_ULONG : CORINFO_TYPE_LONG);

    BlockRange().InsertAfter(op, outOp);

    LIR::Use use;
    if (BlockRange().TryGetUse(op, &use))
    {
        use.ReplaceWith(outOp);
    }
    else
    {
        outOp->SetUnusedValue();
    }

    BlockRange().Remove(mul);
    BlockRange().Remove(op);

    JITDUMP("Converted to HW_INTRINSIC 'NI_ArmBase_Arm64_MultiplyLong[Add/Sub]'.\n");
    JITDUMP(":\n");
    DISPTREERANGE(BlockRange(), outOp);
    JITDUMP("\n");

    *next = outOp;
    return true;
}

//----------------------------------------------------------------------------------------------
// TryLowerNegToCombinedMulOp: Attempt to convert NEG nodes to a combined multiply
// and negate operation. Conversion can only happen if the operands to the
// operation meet one of the following criteria:
// - op1 is a MUL_LONG containing two integer operands.
//
// Arguments:
//    op   - The NEG node to attempt an optimisation on.
//    next - [out] Next node to lower if this function returns true
//
// Return Value:
//    false if no changes were made
//
bool Lowering::TryLowerNegToMulLongOp(GenTreeOp* op, GenTree** next)
{
    assert(op->OperIs(GT_NEG));

    if (!comp->opts.OptimizationEnabled())
        return false;

    if (op->isContained())
        return false;

    if (!varTypeIsIntegral(op))
        return false;

    if ((op->gtFlags & GTF_SET_FLAGS) != 0)
        return false;

    GenTree* op1 = op->gtGetOp1();

    // Ensure the negated operand is a MUL_LONG.
    if (!op1->OperIs(GT_MUL_LONG))
        return false;

    // Ensure the MUL_LONG contains two integer parameters.
    GenTreeOp* mul = op1->AsOp();
    if (!genActualTypeIsInt(mul->gtOp1) || !genActualTypeIsInt(mul->gtOp2))
        return false;

    // The multiply must evaluate to the same thing if evaluated at 'op'.
    if (!IsInvariantInRange(mul, op))
        return false;

    // Able to optimise, create the new node and replace the original.
    GenTreeHWIntrinsic* outOp =
        comp->gtNewScalarHWIntrinsicNode(TYP_LONG, mul->gtOp1, mul->gtOp2, NI_ArmBase_Arm64_MultiplyLongNeg);
    outOp->SetSimdBaseJitType(mul->IsUnsigned() ? CORINFO_TYPE_ULONG : CORINFO_TYPE_LONG);

    BlockRange().InsertAfter(op, outOp);

    LIR::Use use;
    if (BlockRange().TryGetUse(op, &use))
    {
        use.ReplaceWith(outOp);
    }
    else
    {
        outOp->SetUnusedValue();
    }

    BlockRange().Remove(mul);
    BlockRange().Remove(op);

#ifdef DEBUG
    JITDUMP("Converted to HW_INTRINSIC 'NI_ArmBase_Arm64_MultiplyLongNeg'.\n");
    JITDUMP(":\n");
    DISPTREERANGE(BlockRange(), outOp);
    JITDUMP("\n");
#endif

    *next = outOp;
    return true;
}

//----------------------------------------------------------------------------------------------
// TryContainingCselOp: Attempt contain a condition select node in a parent node.
//
// Arguments:
//    parentNode   - The node to attempt an optimisation on.
//    childNode   - The child conditional select node that we try to contain.
//
// Return Value:
//    true if the conditional select can be contained
//
bool Lowering::TryContainingCselOp(GenTreeHWIntrinsic* parentNode, GenTreeHWIntrinsic* childNode)
{
    assert(childNode->GetHWIntrinsicId() == NI_Sve_ConditionalSelect);

    if (childNode->Op(2)->IsEmbMaskOp())
    {
        // Do not optimise if the conditional select node is added to embed the mask for its op2. Such conditional
        // select nodes are optimised out while emitting the embedded variant of an instruction associated with op2.
        assert(childNode->Op(2)->isContained());
        return false;
    }

    bool canContain = false;

    var_types simdBaseType = parentNode->GetSimdBaseType();
    if (childNode->Op(3)->IsVectorZero())
    {
        switch (parentNode->GetHWIntrinsicId())
        {
            case NI_Sve_AddAcross:
            case NI_Sve_OrAcross:
            case NI_Sve_XorAcross:
            {
                canContain = true;
                break;
            }

            case NI_Sve_MaxAcross:
            {
                canContain = varTypeIsUnsigned(simdBaseType);
                break;
            }

            default:
                break;
        }

        if (canContain)
        {
            // Contain the zero op in conditional select
            MakeSrcContained(childNode, childNode->Op(3));
            // Contain the embedded conditional select in the parent
            MakeSrcContained(parentNode, childNode);
        }
    }

    return canContain;
}

#endif // TARGET_ARM64

//------------------------------------------------------------------------
// ContainCheckBoundsChk: determine whether any source of a bounds check node should be contained.
//
// Arguments:
//    node - pointer to the node
//
void Lowering::ContainCheckBoundsChk(GenTreeBoundsChk* node)
{
    assert(node->OperIs(GT_BOUNDS_CHECK));
    if (!CheckImmedAndMakeContained(node, node->GetIndex()))
    {
        CheckImmedAndMakeContained(node, node->GetArrayLength());
    }
}

#ifdef FEATURE_HW_INTRINSICS

//----------------------------------------------------------------------------------------------
// ContainCheckHWIntrinsic: Perform containment analysis for a hardware intrinsic node.
//
//  Arguments:
//     node - The hardware intrinsic node.
//
void Lowering::ContainCheckHWIntrinsic(GenTreeHWIntrinsic* node)
{
    const HWIntrinsic intrin(node);

    const bool hasImmediateOperand = HWIntrinsicInfo::HasImmediateOperand(intrin.id);

    if ((intrin.category == HW_Category_ShiftLeftByImmediate) ||
        (intrin.category == HW_Category_ShiftRightByImmediate) ||
        ((intrin.category == HW_Category_SIMDByIndexedElement) && hasImmediateOperand))
    {
        switch (intrin.numOperands)
        {
            case 4:
                assert(varTypeIsIntegral(intrin.op4));
                if (intrin.op4->IsCnsIntOrI())
                {
                    MakeSrcContained(node, intrin.op4);
                }
                break;

            case 3:
                assert(varTypeIsIntegral(intrin.op3));
                if (intrin.op3->IsCnsIntOrI())
                {
                    MakeSrcContained(node, intrin.op3);
                }
                break;

            case 2:
                assert(varTypeIsIntegral(intrin.op2));
                if (intrin.op2->IsCnsIntOrI())
                {
                    MakeSrcContained(node, intrin.op2);
                }
                break;

            default:
                unreached();
        }
    }
    else if (hasImmediateOperand || HWIntrinsicInfo::SupportsContainment(intrin.id))
    {
        switch (intrin.id)
        {
            case NI_AdvSimd_DuplicateSelectedScalarToVector64:
            case NI_AdvSimd_DuplicateSelectedScalarToVector128:
            case NI_AdvSimd_Extract:
            case NI_AdvSimd_InsertScalar:
            case NI_AdvSimd_LoadAndInsertScalar:
            case NI_AdvSimd_LoadAndInsertScalarVector64x2:
            case NI_AdvSimd_LoadAndInsertScalarVector64x3:
            case NI_AdvSimd_LoadAndInsertScalarVector64x4:
            case NI_AdvSimd_Arm64_LoadAndInsertScalarVector128x2:
            case NI_AdvSimd_Arm64_LoadAndInsertScalarVector128x3:
            case NI_AdvSimd_Arm64_LoadAndInsertScalarVector128x4:
            case NI_AdvSimd_Arm64_DuplicateSelectedScalarToVector128:
            case NI_Sve_DuplicateSelectedScalarToVector:
                assert(hasImmediateOperand);
                assert(varTypeIsIntegral(intrin.op2));
                if (intrin.op2->IsCnsIntOrI())
                {
                    MakeSrcContained(node, intrin.op2);
                }
                break;

            case NI_AdvSimd_ExtractVector64:
            case NI_AdvSimd_ExtractVector128:
            case NI_AdvSimd_StoreSelectedScalar:
            case NI_AdvSimd_Arm64_StoreSelectedScalar:
            case NI_Sve_Prefetch16Bit:
            case NI_Sve_Prefetch32Bit:
            case NI_Sve_Prefetch64Bit:
            case NI_Sve_Prefetch8Bit:
            case NI_Sve_ExtractVector:
            case NI_Sve_AddRotateComplex:
            case NI_Sve_TrigonometricMultiplyAddCoefficient:
            case NI_Sve2_ShiftLeftAndInsert:
                assert(hasImmediateOperand);
                assert(varTypeIsIntegral(intrin.op3));
                if (intrin.op3->IsCnsIntOrI())
                {
                    MakeSrcContained(node, intrin.op3);
                }
                break;

            case NI_AdvSimd_Insert:
                assert(hasImmediateOperand);
                assert(varTypeIsIntegral(intrin.op2));

                if (intrin.op2->IsCnsIntOrI())
                {
                    MakeSrcContained(node, intrin.op2);
                }
                break;

            case NI_AdvSimd_Arm64_InsertSelectedScalar:
                assert(hasImmediateOperand);
                assert(intrin.op2->IsCnsIntOrI());
                assert(intrin.op4->IsCnsIntOrI());

                MakeSrcContained(node, intrin.op2);
                MakeSrcContained(node, intrin.op4);
                break;

            case NI_AdvSimd_CompareEqual:
            case NI_AdvSimd_Arm64_CompareEqual:
            case NI_AdvSimd_Arm64_CompareEqualScalar:
            {
                if (intrin.op1->IsVectorZero())
                {
                    GenTree* op1 = intrin.op1;
                    GenTree* op2 = intrin.op2;

                    assert(HWIntrinsicInfo::IsCommutative(intrin.id));
                    MakeSrcContained(node, op1);

                    // Swap the operands here to make the containment checks in codegen simpler
                    node->Op(1) = op2;
                    node->Op(2) = op1;
                }
                else if (intrin.op2->IsVectorZero())
                {
                    MakeSrcContained(node, intrin.op2);
                }
                break;
            }

            case NI_AdvSimd_CompareGreaterThan:
            case NI_AdvSimd_CompareGreaterThanOrEqual:
            case NI_AdvSimd_Arm64_CompareGreaterThan:
            case NI_AdvSimd_Arm64_CompareGreaterThanOrEqual:
            case NI_AdvSimd_Arm64_CompareGreaterThanScalar:
            case NI_AdvSimd_Arm64_CompareGreaterThanOrEqualScalar:
            {
                // Containment is not supported for unsigned base types as the corresponding instructions:
                //    - cmhi
                //    - cmhs
                // require both operands; they do not have a 'with zero'.
                if (intrin.op2->IsVectorZero() && !varTypeIsUnsigned(intrin.baseType))
                {
                    MakeSrcContained(node, intrin.op2);
                }
                break;
            }

            case NI_Vector64_CreateScalarUnsafe:
            case NI_Vector128_CreateScalarUnsafe:
            case NI_AdvSimd_DuplicateToVector64:
            case NI_AdvSimd_DuplicateToVector128:
            case NI_AdvSimd_Arm64_DuplicateToVector64:
            case NI_AdvSimd_Arm64_DuplicateToVector128:
                if (IsValidConstForMovImm(node))
                {
                    MakeSrcContained(node, node->Op(1));
                }
                break;

            case NI_Vector64_GetElement:
            case NI_Vector128_GetElement:
            {
                assert(!IsContainableMemoryOp(intrin.op1) || !IsSafeToContainMem(node, intrin.op1));
                assert(intrin.op2->OperIsConst());

                // Loading a constant index from register
                MakeSrcContained(node, intrin.op2);
                break;
            }

            case NI_Sve_CreateTrueMaskByte:
            case NI_Sve_CreateTrueMaskDouble:
            case NI_Sve_CreateTrueMaskInt16:
            case NI_Sve_CreateTrueMaskInt32:
            case NI_Sve_CreateTrueMaskInt64:
            case NI_Sve_CreateTrueMaskSByte:
            case NI_Sve_CreateTrueMaskSingle:
            case NI_Sve_CreateTrueMaskUInt16:
            case NI_Sve_CreateTrueMaskUInt32:
            case NI_Sve_CreateTrueMaskUInt64:
            case NI_Sve_Count16BitElements:
            case NI_Sve_Count32BitElements:
            case NI_Sve_Count64BitElements:
            case NI_Sve_Count8BitElements:
                assert(hasImmediateOperand);
                assert(varTypeIsIntegral(intrin.op1));
                if (intrin.op1->IsCnsIntOrI())
                {
                    MakeSrcContained(node, intrin.op1);
                }
                break;

            case NI_Sve_ConditionalSelect:
            {
                assert(intrin.numOperands == 3);
                GenTree* op1 = intrin.op1;
                GenTree* op2 = intrin.op2;
                GenTree* op3 = intrin.op3;

                // Handle op1
                if (op1->IsMaskZero())
                {
                    // When we are merging with zero, we can specialize
                    // and avoid instantiating the vector constant.
                    MakeSrcContained(node, op1);
                    LABELEDDISPTREERANGE("Contained false mask op1 in ConditionalSelect", BlockRange(), op1);
                }

                // Handle op2
                if (op2->OperIsHWIntrinsic() && !op2->IsEmbMaskOp())
                {
                    const GenTreeHWIntrinsic* embOp = op2->AsHWIntrinsic();

                    if (IsInvariantInRange(op2, node) && op2->isEmbeddedMaskingCompatible())
                    {
                        bool     contain  = false;
                        uint32_t maskSize = genTypeSize(node->GetSimdBaseType());
                        uint32_t operSize = genTypeSize(op2->AsHWIntrinsic()->GetSimdBaseType());

                        if (maskSize == operSize)
                        {
                            // If the size of baseType of operation matches that of maskType, then contain
                            // the operation
                            contain = true;
                        }
                        else
                        {
                            // Else check if this operation has an auxiliary type that matches the
                            // mask size.

                            // For now, make sure that we get here only for intrinsics that we are
                            // sure about to rely on auxiliary type's size.
                            assert((embOp->GetHWIntrinsicId() == NI_Sve_ConvertToDouble) ||
                                   (embOp->GetHWIntrinsicId() == NI_Sve_ConvertToInt32) ||
                                   (embOp->GetHWIntrinsicId() == NI_Sve_ConvertToInt64) ||
                                   (embOp->GetHWIntrinsicId() == NI_Sve_ConvertToSingle) ||
                                   (embOp->GetHWIntrinsicId() == NI_Sve_ConvertToUInt32) ||
                                   (embOp->GetHWIntrinsicId() == NI_Sve_ConvertToUInt64));

                            uint32_t auxSize = genTypeSize(embOp->GetAuxiliaryType());
                            if (maskSize == auxSize)
                            {
                                contain = true;
                            }
                        }

                        if (contain)
                        {
                            MakeSrcContained(node, op2);
                            op2->MakeEmbMaskOp();
                            LABELEDDISPTREERANGE("Contained op2 in ConditionalSelect", BlockRange(), node);
                        }
                    }

                    // Handle intrinsics with embedded masks and immediate operands
                    // (For now, just handle ShiftRightArithmeticForDivide specifically)
                    if (embOp->GetHWIntrinsicId() == NI_Sve_ShiftRightArithmeticForDivide)
                    {
                        assert(embOp->GetOperandCount() == 2);
                        if (embOp->Op(2)->IsCnsIntOrI())
                        {
                            MakeSrcContained(op2, embOp->Op(2));
                            LABELEDDISPTREERANGE("Contained ShiftRight in ConditionalSelect", BlockRange(), op2);
                        }
                    }
                }

                // Handle op3
                if (op3->IsVectorZero() && op1->IsTrueMask(node->GetSimdBaseType()) && op2->IsEmbMaskOp())
                {
                    // When we are merging with zero, we can specialize
                    // and avoid instantiating the vector constant.
                    // Do this only if op1 was AllTrueMask
                    MakeSrcContained(node, op3);
                    LABELEDDISPTREERANGE("Contained false mask op3 in ConditionalSelect", BlockRange(), op3);
                }

                break;
            }

            case NI_Sve_FusedMultiplyAddBySelectedScalar:
            case NI_Sve_FusedMultiplySubtractBySelectedScalar:
            case NI_Sve_MultiplyAddRotateComplex:
            case NI_Sve2_DotProductRotateComplex:
                assert(hasImmediateOperand);
                assert(varTypeIsIntegral(intrin.op4));
                if (intrin.op4->IsCnsIntOrI())
                {
                    MakeSrcContained(node, intrin.op4);
                }
                break;

            case NI_Sve_GatherPrefetch8Bit:
            case NI_Sve_GatherPrefetch16Bit:
            case NI_Sve_GatherPrefetch32Bit:
            case NI_Sve_GatherPrefetch64Bit:
                assert(hasImmediateOperand);
                if (!varTypeIsSIMD(intrin.op2->gtType))
                {
                    assert(varTypeIsIntegral(intrin.op4));
                    if (intrin.op4->IsCnsIntOrI())
                    {
                        MakeSrcContained(node, intrin.op4);
                    }
                }
                else
                {
                    assert(varTypeIsIntegral(intrin.op3));
                    if (intrin.op3->IsCnsIntOrI())
                    {
                        MakeSrcContained(node, intrin.op3);
                    }
                }
                break;

            case NI_Sve_SaturatingDecrementBy16BitElementCount:
            case NI_Sve_SaturatingDecrementBy32BitElementCount:
            case NI_Sve_SaturatingDecrementBy64BitElementCount:
            case NI_Sve_SaturatingDecrementBy8BitElementCount:
            case NI_Sve_SaturatingIncrementBy16BitElementCount:
            case NI_Sve_SaturatingIncrementBy32BitElementCount:
            case NI_Sve_SaturatingIncrementBy64BitElementCount:
            case NI_Sve_SaturatingIncrementBy8BitElementCount:
            case NI_Sve_SaturatingDecrementBy16BitElementCountScalar:
            case NI_Sve_SaturatingDecrementBy32BitElementCountScalar:
            case NI_Sve_SaturatingDecrementBy64BitElementCountScalar:
            case NI_Sve_SaturatingIncrementBy16BitElementCountScalar:
            case NI_Sve_SaturatingIncrementBy32BitElementCountScalar:
            case NI_Sve_SaturatingIncrementBy64BitElementCountScalar:
                assert(hasImmediateOperand);
                assert(varTypeIsIntegral(intrin.op2));
                assert(varTypeIsIntegral(intrin.op3));
                // Can only avoid generating a table if both immediates are constant.
                if (intrin.op2->IsCnsIntOrI() && intrin.op3->IsCnsIntOrI())
                {
                    MakeSrcContained(node, intrin.op2);
                    MakeSrcContained(node, intrin.op3);
                }
                break;

            case NI_Sve_MultiplyAddRotateComplexBySelectedScalar:
            case NI_Sve2_DotProductRotateComplexBySelectedIndex:
                assert(hasImmediateOperand);
                assert(varTypeIsIntegral(intrin.op4));
                assert(varTypeIsIntegral(intrin.op5));
                // Can only avoid generating a table if both immediates are constant.
                if (intrin.op4->IsCnsIntOrI() && intrin.op5->IsCnsIntOrI())
                {
                    MakeSrcContained(node, intrin.op4);
                    MakeSrcContained(node, intrin.op5);
                }
                break;

            default:
                unreached();
        }
    }
}
//----------------------------------------------------------------------------------------------
// Lowering::LowerHWIntrinsicCndSel: Lowers a Sve ConditionalSelect call
//
//  Arguments:
//     node - The hardware intrinsic node of the form
//            ConditionalSelect(mask, trueValue, falseValue)
//
//  Returns:
//    Next node to lower.
//
GenTree* Lowering::LowerHWIntrinsicCndSel(GenTreeHWIntrinsic* cndSelNode)
{
    assert(cndSelNode->OperIsHWIntrinsic(NI_Sve_ConditionalSelect));

    GenTree* op1         = cndSelNode->Op(1);
    GenTree* op2         = cndSelNode->Op(2);
    GenTree* op3         = cndSelNode->Op(3);
    GenTree* lowerCndSel = cndSelNode;

    if (op2->OperIsHWIntrinsic(NI_Sve_ConditionalSelect))
    {
        // Handle cases where there is a nested ConditionalSelect for `trueValue`
        GenTreeHWIntrinsic* nestedCndSel = op2->AsHWIntrinsic();
        GenTree*            nestedOp1    = nestedCndSel->Op(1);
        GenTree*            nestedOp2    = nestedCndSel->Op(2);
        assert(varTypeIsMask(nestedOp1));

        if (nestedOp2->OperIsHWIntrinsic())
        {
            NamedIntrinsic nestedOp2Id = nestedOp2->AsHWIntrinsic()->GetHWIntrinsicId();

            // If the nested op uses Pg/Z, then inactive lanes will result in zeros, so can only transform if
            // op3 is all zeros. Such a Csel operation is absorbed into the instruction when emitted. Skip this
            // optimisation when the nestedOp is a reduce operation.

            if (nestedOp1->IsTrueMask(cndSelNode->GetSimdBaseType()) &&
                !HWIntrinsicInfo::IsReduceOperation(nestedOp2Id) &&
                (!HWIntrinsicInfo::IsZeroingMaskedOperation(nestedOp2Id) || op3->IsVectorZero()))
            {
                GenTree* nestedOp2 = nestedCndSel->Op(2);
                GenTree* nestedOp3 = nestedCndSel->Op(3);

                LABELEDDISPTREERANGE("Removed nested conditionalselect (before)", BlockRange(), cndSelNode);

                // Transform:
                //
                // CndSel1(mask, CndSel2(AllTrue, embedded(), trueValOp3), op3) to
                // CndSel1(mask, embedded(), op3)
                //
                cndSelNode->Op(2) = nestedCndSel->Op(2);
                nestedOp3->SetUnusedValue();

                BlockRange().Remove(nestedOp1);
                BlockRange().Remove(nestedCndSel);

                LABELEDDISPTREERANGE("Removed nested conditionalselect (after)", BlockRange(), cndSelNode);
                return cndSelNode;
            }
        }
    }
    else if (op1->IsTrueMask(cndSelNode->GetSimdBaseType()))
    {
        // Any case where op2 is not an embedded HWIntrinsic
        if (!op2->OperIsHWIntrinsic() ||
            !HWIntrinsicInfo::IsEmbeddedMaskedOperation(op2->AsHWIntrinsic()->GetHWIntrinsicId()))
        {
            LABELEDDISPTREERANGE("Lowered ConditionalSelect(True, op2, op3) to op2 (before)", BlockRange(), cndSelNode);

            // Transform
            // CndSel(AllTrue, op2, op3) to
            // op2

            LIR::Use use;
            if (BlockRange().TryGetUse(cndSelNode, &use))
            {
                use.ReplaceWith(op2);
            }
            else
            {
                op2->SetUnusedValue();
            }

            op3->SetUnusedValue();
            op1->SetUnusedValue();

            GenTree* next = cndSelNode->gtNext;
            BlockRange().Remove(cndSelNode);

            LABELEDDISPTREERANGE("Lowered ConditionalSelect(True, op2, op3) to op2 (after)", BlockRange(), op2);
            return next;
        }
    }

    ContainCheckHWIntrinsic(cndSelNode);
    return cndSelNode->gtNext;
}

#if defined(TARGET_ARM64)
//----------------------------------------------------------------------------------------------
// StoreFFRValue: For hwintrinsic that produce a first faulting register (FFR) value, create
// nodes to save its value to a local variable.
//
// Arguments:
//     node - The node before which the pseudo definition is needed
//
void Lowering::StoreFFRValue(GenTreeHWIntrinsic* node)
{
#ifdef DEBUG
    switch (node->GetHWIntrinsicId())
    {
        case NI_Sve_GatherVectorByteZeroExtendFirstFaulting:
        case NI_Sve_GatherVectorFirstFaulting:
        case NI_Sve_GatherVectorInt16SignExtendFirstFaulting:
        case NI_Sve_GatherVectorInt16WithByteOffsetsSignExtendFirstFaulting:
        case NI_Sve_GatherVectorInt32SignExtendFirstFaulting:
        case NI_Sve_GatherVectorInt32WithByteOffsetsSignExtendFirstFaulting:
        case NI_Sve_GatherVectorSByteSignExtendFirstFaulting:
        case NI_Sve_GatherVectorUInt16WithByteOffsetsZeroExtendFirstFaulting:
        case NI_Sve_GatherVectorUInt16ZeroExtendFirstFaulting:
        case NI_Sve_GatherVectorUInt32WithByteOffsetsZeroExtendFirstFaulting:
        case NI_Sve_GatherVectorUInt32ZeroExtendFirstFaulting:
        case NI_Sve_GatherVectorWithByteOffsetFirstFaulting:
        case NI_Sve_LoadVectorByteZeroExtendFirstFaulting:
        case NI_Sve_LoadVectorFirstFaulting:
        case NI_Sve_LoadVectorInt16SignExtendFirstFaulting:
        case NI_Sve_LoadVectorInt32SignExtendFirstFaulting:
        case NI_Sve_LoadVectorSByteSignExtendFirstFaulting:
        case NI_Sve_LoadVectorUInt16ZeroExtendFirstFaulting:
        case NI_Sve_LoadVectorUInt32ZeroExtendFirstFaulting:
        case NI_Sve_LoadVectorByteNonFaultingZeroExtendToInt16:
        case NI_Sve_LoadVectorByteNonFaultingZeroExtendToInt32:
        case NI_Sve_LoadVectorByteNonFaultingZeroExtendToInt64:
        case NI_Sve_LoadVectorByteNonFaultingZeroExtendToUInt16:
        case NI_Sve_LoadVectorByteNonFaultingZeroExtendToUInt32:
        case NI_Sve_LoadVectorByteNonFaultingZeroExtendToUInt64:
        case NI_Sve_LoadVectorInt16NonFaultingSignExtendToInt32:
        case NI_Sve_LoadVectorInt16NonFaultingSignExtendToInt64:
        case NI_Sve_LoadVectorInt16NonFaultingSignExtendToUInt32:
        case NI_Sve_LoadVectorInt16NonFaultingSignExtendToUInt64:
        case NI_Sve_LoadVectorInt32NonFaultingSignExtendToInt64:
        case NI_Sve_LoadVectorInt32NonFaultingSignExtendToUInt64:
        case NI_Sve_LoadVectorNonFaulting:
        case NI_Sve_LoadVectorSByteNonFaultingSignExtendToInt16:
        case NI_Sve_LoadVectorSByteNonFaultingSignExtendToInt32:
        case NI_Sve_LoadVectorSByteNonFaultingSignExtendToInt64:
        case NI_Sve_LoadVectorSByteNonFaultingSignExtendToUInt16:
        case NI_Sve_LoadVectorSByteNonFaultingSignExtendToUInt32:
        case NI_Sve_LoadVectorSByteNonFaultingSignExtendToUInt64:
        case NI_Sve_LoadVectorUInt16NonFaultingZeroExtendToInt32:
        case NI_Sve_LoadVectorUInt16NonFaultingZeroExtendToInt64:
        case NI_Sve_LoadVectorUInt16NonFaultingZeroExtendToUInt32:
        case NI_Sve_LoadVectorUInt16NonFaultingZeroExtendToUInt64:
        case NI_Sve_LoadVectorUInt32NonFaultingZeroExtendToInt64:
        case NI_Sve_LoadVectorUInt32NonFaultingZeroExtendToUInt64:
        case NI_Sve_SetFfr:
            break;

        default:
            assert(!"Unexpected HWIntrinsicId");
    }
#endif

    // Create physReg FFR definition to store FFR register.
    unsigned lclNum      = comp->getFFRegisterVarNum();
    GenTree* ffrReg      = comp->gtNewPhysRegNode(REG_FFR, TYP_MASK);
    GenTree* storeLclVar = comp->gtNewStoreLclVarNode(lclNum, ffrReg);
    BlockRange().InsertAfter(node, ffrReg, storeLclVar);
    m_ffrTrashed = false;
}
#endif // TARGET_ARM64

#endif // FEATURE_HW_INTRINSICS

#endif // TARGET_ARMARCH
