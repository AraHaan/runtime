// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

/*XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XX                                                                           XX
XX                          AssertionProp                                    XX
XX                                                                           XX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
*/

#include "jitpch.h"
#include "rangecheck.h"
#ifdef _MSC_VER
#pragma hdrstop
#endif

//------------------------------------------------------------------------
// Contains: Whether the range contains a given integral value, inclusive.
//
// Arguments:
//    value - the integral value in question
//
// Return Value:
//    "true" if the value is within the range's bounds, "false" otherwise.
//
bool IntegralRange::Contains(int64_t value) const
{
    int64_t lowerBound = SymbolicToRealValue(m_lowerBound);
    int64_t upperBound = SymbolicToRealValue(m_upperBound);

    return (lowerBound <= value) && (value <= upperBound);
}

//------------------------------------------------------------------------
// SymbolicToRealValue: Convert a symbolic value to a 64-bit signed integer.
//
// Arguments:
//    value - the symbolic value in question
//
// Return Value:
//    Integer corresponding to the symbolic value.
//
/* static */ int64_t IntegralRange::SymbolicToRealValue(SymbolicIntegerValue value)
{
    static const int64_t SymbolicToRealMap[]{
        INT64_MIN,               // SymbolicIntegerValue::LongMin
        INT32_MIN,               // SymbolicIntegerValue::IntMin
        INT16_MIN,               // SymbolicIntegerValue::ShortMin
        INT8_MIN,                // SymbolicIntegerValue::ByteMin
        0,                       // SymbolicIntegerValue::Zero
        1,                       // SymbolicIntegerValue::One
        INT8_MAX,                // SymbolicIntegerValue::ByteMax
        UINT8_MAX,               // SymbolicIntegerValue::UByteMax
        INT16_MAX,               // SymbolicIntegerValue::ShortMax
        UINT16_MAX,              // SymbolicIntegerValue::UShortMax
        CORINFO_Array_MaxLength, // SymbolicIntegerValue::ArrayLenMax
        INT32_MAX,               // SymbolicIntegerValue::IntMax
        UINT32_MAX,              // SymbolicIntegerValue::UIntMax
        INT64_MAX                // SymbolicIntegerValue::LongMax
    };

    assert(sizeof(SymbolicIntegerValue) == sizeof(int32_t));
    assert(SymbolicToRealMap[static_cast<int32_t>(SymbolicIntegerValue::LongMin)] == INT64_MIN);
    assert(SymbolicToRealMap[static_cast<int32_t>(SymbolicIntegerValue::Zero)] == 0);
    assert(SymbolicToRealMap[static_cast<int32_t>(SymbolicIntegerValue::LongMax)] == INT64_MAX);

    return SymbolicToRealMap[static_cast<int32_t>(value)];
}

//------------------------------------------------------------------------
// LowerBoundForType: Get the symbolic lower bound for a type.
//
// Arguments:
//    type - the integral type in question
//
// Return Value:
//    Symbolic value representing the smallest possible value "type" can represent.
//
/* static */ SymbolicIntegerValue IntegralRange::LowerBoundForType(var_types type)
{
    switch (type)
    {
        case TYP_UBYTE:
        case TYP_USHORT:
            return SymbolicIntegerValue::Zero;
        case TYP_BYTE:
            return SymbolicIntegerValue::ByteMin;
        case TYP_SHORT:
            return SymbolicIntegerValue::ShortMin;
        case TYP_INT:
            return SymbolicIntegerValue::IntMin;
        case TYP_LONG:
            return SymbolicIntegerValue::LongMin;
        default:
            unreached();
    }
}

//------------------------------------------------------------------------
// UpperBoundForType: Get the symbolic upper bound for a type.
//
// Arguments:
//    type - the integral type in question
//
// Return Value:
//    Symbolic value representing the largest possible value "type" can represent.
//
/* static */ SymbolicIntegerValue IntegralRange::UpperBoundForType(var_types type)
{
    switch (type)
    {
        case TYP_BYTE:
            return SymbolicIntegerValue::ByteMax;
        case TYP_UBYTE:
            return SymbolicIntegerValue::UByteMax;
        case TYP_SHORT:
            return SymbolicIntegerValue::ShortMax;
        case TYP_USHORT:
            return SymbolicIntegerValue::UShortMax;
        case TYP_INT:
            return SymbolicIntegerValue::IntMax;
        case TYP_UINT:
            return SymbolicIntegerValue::UIntMax;
        case TYP_LONG:
            return SymbolicIntegerValue::LongMax;
        default:
            unreached();
    }
}

//------------------------------------------------------------------------
// ForNode: Compute the integral range for a node.
//
// Arguments:
//    node     - the node, of an integral type, in question
//    compiler - the Compiler, used to retrieve additional info
//
// Return Value:
//    The integral range this node produces.
//
/* static */ IntegralRange IntegralRange::ForNode(GenTree* node, Compiler* compiler)
{
    assert(varTypeIsIntegral(node));

    var_types rangeType = node->TypeGet();

    switch (node->OperGet())
    {
        case GT_EQ:
        case GT_NE:
        case GT_LT:
        case GT_LE:
        case GT_GE:
        case GT_GT:
            return {SymbolicIntegerValue::Zero, SymbolicIntegerValue::One};

        case GT_ARR_LENGTH:
        case GT_MDARR_LENGTH:
            return {SymbolicIntegerValue::Zero, SymbolicIntegerValue::ArrayLenMax};

        case GT_CALL:
            if (node->AsCall()->NormalizesSmallTypesOnReturn())
            {
                rangeType = static_cast<var_types>(node->AsCall()->gtReturnType);
            }
            break;

        case GT_IND:
        {
            GenTree* const addr = node->AsIndir()->Addr();

            if (node->TypeIs(TYP_INT) && addr->OperIs(GT_ADD) && addr->gtGetOp1()->OperIs(GT_LCL_VAR) &&
                addr->gtGetOp2()->IsIntegralConst(OFFSETOF__CORINFO_Span__length))
            {
                GenTreeLclVar* const lclVar = addr->gtGetOp1()->AsLclVar();

                if (compiler->lvaGetDesc(lclVar->GetLclNum())->IsSpan())
                {
                    assert(compiler->lvaIsImplicitByRefLocal(lclVar->GetLclNum()));
                    return {SymbolicIntegerValue::Zero, UpperBoundForType(rangeType)};
                }
            }
            break;
        }

        case GT_LCL_FLD:
        {
            GenTreeLclFld* const lclFld = node->AsLclFld();
            LclVarDsc* const     varDsc = compiler->lvaGetDesc(lclFld);

            if (node->TypeIs(TYP_INT) && varDsc->IsSpan() && lclFld->GetLclOffs() == OFFSETOF__CORINFO_Span__length)
            {
                return {SymbolicIntegerValue::Zero, UpperBoundForType(rangeType)};
            }

            break;
        }

        case GT_LCL_VAR:
        {
            LclVarDsc* const varDsc = compiler->lvaGetDesc(node->AsLclVar());

            if (varDsc->lvNormalizeOnStore())
            {
                rangeType = compiler->lvaGetDesc(node->AsLclVar())->TypeGet();
            }

            if (varDsc->IsNeverNegative())
            {
                return {SymbolicIntegerValue::Zero, UpperBoundForType(rangeType)};
            }
            break;
        }

        case GT_CNS_INT:
            if (node->IsIntegralConst(0) || node->IsIntegralConst(1))
            {
                return {SymbolicIntegerValue::Zero, SymbolicIntegerValue::One};
            }
            break;

        case GT_QMARK:
            return Union(ForNode(node->AsQmark()->ThenNode(), compiler),
                         ForNode(node->AsQmark()->ElseNode(), compiler));

        case GT_CAST:
            return ForCastOutput(node->AsCast(), compiler);

#if defined(FEATURE_HW_INTRINSICS)
        case GT_HWINTRINSIC:
            switch (node->AsHWIntrinsic()->GetHWIntrinsicId())
            {
#if defined(TARGET_XARCH)
                case NI_Vector128_op_Equality:
                case NI_Vector128_op_Inequality:
                case NI_Vector256_op_Equality:
                case NI_Vector256_op_Inequality:
                case NI_Vector512_op_Equality:
                case NI_Vector512_op_Inequality:
                case NI_X86Base_CompareScalarOrderedEqual:
                case NI_X86Base_CompareScalarOrderedNotEqual:
                case NI_X86Base_CompareScalarOrderedLessThan:
                case NI_X86Base_CompareScalarOrderedLessThanOrEqual:
                case NI_X86Base_CompareScalarOrderedGreaterThan:
                case NI_X86Base_CompareScalarOrderedGreaterThanOrEqual:
                case NI_X86Base_CompareScalarUnorderedEqual:
                case NI_X86Base_CompareScalarUnorderedNotEqual:
                case NI_X86Base_CompareScalarUnorderedLessThanOrEqual:
                case NI_X86Base_CompareScalarUnorderedLessThan:
                case NI_X86Base_CompareScalarUnorderedGreaterThanOrEqual:
                case NI_X86Base_CompareScalarUnorderedGreaterThan:
                case NI_SSE42_TestC:
                case NI_SSE42_TestZ:
                case NI_SSE42_TestNotZAndNotC:
                case NI_AVX_TestC:
                case NI_AVX_TestZ:
                case NI_AVX_TestNotZAndNotC:
                    return {SymbolicIntegerValue::Zero, SymbolicIntegerValue::One};

                case NI_X86Base_Extract:
                case NI_SSE42_Extract:
                case NI_SSE42_X64_Extract:
                case NI_Vector128_ToScalar:
                case NI_Vector256_ToScalar:
                case NI_Vector512_ToScalar:
                case NI_Vector128_GetElement:
                case NI_Vector256_GetElement:
                case NI_Vector512_GetElement:
                    if (varTypeIsSmall(node->AsHWIntrinsic()->GetSimdBaseType()))
                    {
                        return ForType(node->AsHWIntrinsic()->GetSimdBaseType());
                    }
                    break;

                case NI_AVX2_LeadingZeroCount:
                case NI_AVX2_TrailingZeroCount:
                case NI_AVX2_X64_LeadingZeroCount:
                case NI_AVX2_X64_TrailingZeroCount:
                case NI_SSE42_PopCount:
                case NI_SSE42_X64_PopCount:
                    // Note: No advantage in using a precise range for IntegralRange.
                    // Example: IntCns = 42 gives [0..127] with a non -precise range, [42,42] with a precise range.
                    return {SymbolicIntegerValue::Zero, SymbolicIntegerValue::ByteMax};
#elif defined(TARGET_ARM64)
                case NI_Vector64_op_Equality:
                case NI_Vector64_op_Inequality:
                case NI_Vector128_op_Equality:
                case NI_Vector128_op_Inequality:
                    return {SymbolicIntegerValue::Zero, SymbolicIntegerValue::One};

                case NI_AdvSimd_Extract:
                case NI_Vector64_ToScalar:
                case NI_Vector128_ToScalar:
                case NI_Vector64_GetElement:
                case NI_Vector128_GetElement:
                    if (varTypeIsSmall(node->AsHWIntrinsic()->GetSimdBaseType()))
                    {
                        return ForType(node->AsHWIntrinsic()->GetSimdBaseType());
                    }
                    break;

                case NI_AdvSimd_PopCount:
                case NI_AdvSimd_LeadingZeroCount:
                case NI_AdvSimd_LeadingSignCount:
                case NI_ArmBase_LeadingZeroCount:
                case NI_ArmBase_Arm64_LeadingZeroCount:
                case NI_ArmBase_Arm64_LeadingSignCount:
                    // Note: No advantage in using a precise range for IntegralRange.
                    // Example: IntCns = 42 gives [0..127] with a non -precise range, [42,42] with a precise range.
                    return {SymbolicIntegerValue::Zero, SymbolicIntegerValue::ByteMax};
#else
#error Unsupported platform
#endif
                default:
                    break;
            }
            break;
#endif // defined(FEATURE_HW_INTRINSICS)

        default:
            break;
    }

    return ForType(rangeType);
}

//------------------------------------------------------------------------
// ForCastInput: Get the non-overflowing input range for a cast.
//
// This routine computes the input range for a cast from
// an integer to an integer for which it will not overflow.
// See also the specification comment for IntegralRange.
//
// Arguments:
//    cast - the cast node for which the range will be computed
//
// Return Value:
//    The range this cast consumes without overflowing - see description.
//
/* static */ IntegralRange IntegralRange::ForCastInput(GenTreeCast* cast)
{
    var_types fromType     = genActualType(cast->CastOp());
    var_types toType       = cast->CastToType();
    bool      fromUnsigned = cast->IsUnsigned();

    assert((fromType == TYP_INT) || (fromType == TYP_LONG) || varTypeIsGC(fromType));
    assert(varTypeIsIntegral(toType));

    // Cast from a GC type is the same as a cast from TYP_I_IMPL for our purposes.
    if (varTypeIsGC(fromType))
    {
        fromType = TYP_I_IMPL;
    }

    if (!cast->gtOverflow())
    {
        // CAST(small type <- uint/int/ulong/long) - [TO_TYPE_MIN..TO_TYPE_MAX]
        if (varTypeIsSmall(toType))
        {
            return {LowerBoundForType(toType), UpperBoundForType(toType)};
        }

        // We choose to say here that representation-changing casts never overflow.
        // It does not really matter what we do here because representation-changing
        // non-overflowing casts cannot be deleted from the IR in any case.
        // CAST(uint/int <- uint/int)     - [INT_MIN..INT_MAX]
        // CAST(uint/int <- ulong/long)   - [LONG_MIN..LONG_MAX]
        // CAST(ulong/long <- uint/int)   - [INT_MIN..INT_MAX]
        // CAST(ulong/long <- ulong/long) - [LONG_MIN..LONG_MAX]
        return ForType(fromType);
    }

    SymbolicIntegerValue lowerBound;
    SymbolicIntegerValue upperBound;

    // CAST_OVF(small type <- int/long)   - [TO_TYPE_MIN..TO_TYPE_MAX]
    // CAST_OVF(small type <- uint/ulong) - [0..TO_TYPE_MAX]
    if (varTypeIsSmall(toType))
    {
        lowerBound = fromUnsigned ? SymbolicIntegerValue::Zero : LowerBoundForType(toType);
        upperBound = UpperBoundForType(toType);
    }
    else
    {
        switch (toType)
        {
            // CAST_OVF(uint <- uint)       - [INT_MIN..INT_MAX]
            // CAST_OVF(uint <- int)        - [0..INT_MAX]
            // CAST_OVF(uint <- ulong/long) - [0..UINT_MAX]
            case TYP_UINT:
                if (fromType == TYP_LONG)
                {
                    lowerBound = SymbolicIntegerValue::Zero;
                    upperBound = SymbolicIntegerValue::UIntMax;
                }
                else
                {
                    lowerBound = fromUnsigned ? SymbolicIntegerValue::IntMin : SymbolicIntegerValue::Zero;
                    upperBound = SymbolicIntegerValue::IntMax;
                }
                break;

            // CAST_OVF(int <- uint/ulong) - [0..INT_MAX]
            // CAST_OVF(int <- int/long)   - [INT_MIN..INT_MAX]
            case TYP_INT:
                lowerBound = fromUnsigned ? SymbolicIntegerValue::Zero : SymbolicIntegerValue::IntMin;
                upperBound = SymbolicIntegerValue::IntMax;
                break;

            // CAST_OVF(ulong <- uint)  - [INT_MIN..INT_MAX]
            // CAST_OVF(ulong <- int)   - [0..INT_MAX]
            // CAST_OVF(ulong <- ulong) - [LONG_MIN..LONG_MAX]
            // CAST_OVF(ulong <- long)  - [0..LONG_MAX]
            case TYP_ULONG:
                lowerBound = fromUnsigned ? LowerBoundForType(fromType) : SymbolicIntegerValue::Zero;
                upperBound = UpperBoundForType(fromType);
                break;

            // CAST_OVF(long <- uint/int) - [INT_MIN..INT_MAX]
            // CAST_OVF(long <- ulong)    - [0..LONG_MAX]
            // CAST_OVF(long <- long)     - [LONG_MIN..LONG_MAX]
            case TYP_LONG:
                if (fromUnsigned && (fromType == TYP_LONG))
                {
                    lowerBound = SymbolicIntegerValue::Zero;
                }
                else
                {
                    lowerBound = LowerBoundForType(fromType);
                }
                upperBound = UpperBoundForType(fromType);
                break;

            default:
                unreached();
        }
    }

    return {lowerBound, upperBound};
}

//------------------------------------------------------------------------
// ForCastOutput: Get the output range for a cast.
//
// This method is the "output" counterpart to ForCastInput, it returns
// a range produced by a cast (by definition, non-overflowing one).
// The output range is the same for representation-preserving casts, but
// can be different for others. One example is CAST_OVF(uint <- long).
// The input range is [0..UINT_MAX], while the output is [INT_MIN..INT_MAX].
// Unlike ForCastInput, this method supports casts from floating point types.
//
// Arguments:
//   cast     - the cast node for which the range will be computed
//   compiler - Compiler object
//
// Return Value:
//   The range this cast produces - see description.
//
/* static */ IntegralRange IntegralRange::ForCastOutput(GenTreeCast* cast, Compiler* compiler)
{
    var_types fromType     = genActualType(cast->CastOp());
    var_types toType       = cast->CastToType();
    bool      fromUnsigned = cast->IsUnsigned();

    assert((fromType == TYP_INT) || (fromType == TYP_LONG) || varTypeIsFloating(fromType) || varTypeIsGC(fromType));
    assert(varTypeIsIntegral(toType));

    // CAST/CAST_OVF(small type <- float/double) - [TO_TYPE_MIN..TO_TYPE_MAX]
    // CAST/CAST_OVF(uint/int <- float/double)   - [INT_MIN..INT_MAX]
    // CAST/CAST_OVF(ulong/long <- float/double) - [LONG_MIN..LONG_MAX]
    if (varTypeIsFloating(fromType))
    {
        if (!varTypeIsSmall(toType))
        {
            toType = genActualType(toType);
        }

        return IntegralRange::ForType(toType);
    }

    // Cast from a GC type is the same as a cast from TYP_I_IMPL for our purposes.
    if (varTypeIsGC(fromType))
    {
        fromType = TYP_I_IMPL;
    }

    if (varTypeIsSmall(toType) || (genActualType(toType) == fromType))
    {
        return ForCastInput(cast);
    }

    // if we're upcasting and the cast op is a known non-negative - consider
    // this cast unsigned
    if (!fromUnsigned && (genTypeSize(toType) >= genTypeSize(fromType)))
    {
        fromUnsigned = cast->CastOp()->IsNeverNegative(compiler);
    }

    // CAST(uint/int <- ulong/long) - [INT_MIN..INT_MAX]
    // CAST(ulong/long <- uint)     - [0..UINT_MAX]
    // CAST(ulong/long <- int)      - [INT_MIN..INT_MAX]
    if (!cast->gtOverflow())
    {
        if ((fromType == TYP_INT) && fromUnsigned)
        {
            return {SymbolicIntegerValue::Zero, SymbolicIntegerValue::UIntMax};
        }

        return {SymbolicIntegerValue::IntMin, SymbolicIntegerValue::IntMax};
    }

    SymbolicIntegerValue lowerBound;
    SymbolicIntegerValue upperBound;
    switch (toType)
    {
        // CAST_OVF(uint <- ulong) - [INT_MIN..INT_MAX]
        // CAST_OVF(uint <- long)  - [INT_MIN..INT_MAX]
        case TYP_UINT:
            lowerBound = SymbolicIntegerValue::IntMin;
            upperBound = SymbolicIntegerValue::IntMax;
            break;

        // CAST_OVF(int <- ulong) - [0..INT_MAX]
        // CAST_OVF(int <- long)  - [INT_MIN..INT_MAX]
        case TYP_INT:
            lowerBound = fromUnsigned ? SymbolicIntegerValue::Zero : SymbolicIntegerValue::IntMin;
            upperBound = SymbolicIntegerValue::IntMax;
            break;

        // CAST_OVF(ulong <- uint) - [0..UINT_MAX]
        // CAST_OVF(ulong <- int)  - [0..INT_MAX]
        case TYP_ULONG:
            lowerBound = SymbolicIntegerValue::Zero;
            upperBound = fromUnsigned ? SymbolicIntegerValue::UIntMax : SymbolicIntegerValue::IntMax;
            break;

        // CAST_OVF(long <- uint) - [0..UINT_MAX]
        // CAST_OVF(long <- int)  - [INT_MIN..INT_MAX]
        case TYP_LONG:
            lowerBound = fromUnsigned ? SymbolicIntegerValue::Zero : SymbolicIntegerValue::IntMin;
            upperBound = fromUnsigned ? SymbolicIntegerValue::UIntMax : SymbolicIntegerValue::IntMax;
            break;

        default:
            unreached();
    }

    return {lowerBound, upperBound};
}

/* static */ IntegralRange IntegralRange::Union(IntegralRange range1, IntegralRange range2)
{
    return IntegralRange(min(range1.GetLowerBound(), range2.GetLowerBound()),
                         max(range1.GetUpperBound(), range2.GetUpperBound()));
}

#ifdef DEBUG
/* static */ void IntegralRange::Print(IntegralRange range)
{
    printf("[%lld", SymbolicToRealValue(range.m_lowerBound));
    printf("..");
    printf("%lld]", SymbolicToRealValue(range.m_upperBound));
}
#endif // DEBUG

//------------------------------------------------------------------------------
// GetAssertionDep: Retrieve the assertions on this local variable
//
// Arguments:
//    lclNum - The local var id.
//
// Return Value:
//    The dependent assertions (assertions using the value of the local var)
//    of the local var.
//

ASSERT_TP& Compiler::GetAssertionDep(unsigned lclNum)
{
    JitExpandArray<ASSERT_TP>& dep = *optAssertionDep;
    if (dep[lclNum] == nullptr)
    {
        dep[lclNum] = BitVecOps::MakeEmpty(apTraits);
    }
    return dep[lclNum];
}

/*****************************************************************************
 *
 *  Initialize the assertion prop bitset traits and the default bitsets.
 */

void Compiler::optAssertionTraitsInit(AssertionIndex assertionCount)
{
    apTraits = new (this, CMK_AssertionProp) BitVecTraits(assertionCount, this);
    apFull   = BitVecOps::MakeFull(apTraits);
}

/*****************************************************************************
 *
 *  Initialize the assertion prop tracking logic.
 */

void Compiler::optAssertionInit(bool isLocalProp)
{
    assert(NO_ASSERTION_INDEX == 0);
    const unsigned maxTrackedLocals = (unsigned)JitConfig.JitMaxLocalsToTrack();

    // We initialize differently for local prop / global prop
    //
    if (isLocalProp)
    {
        optLocalAssertionProp           = true;
        optCrossBlockLocalAssertionProp = true;

        // Disable via config
        //
        if (JitConfig.JitEnableCrossBlockLocalAssertionProp() == 0)
        {
            JITDUMP("Disabling cross-block assertion prop by config setting\n");
            optCrossBlockLocalAssertionProp = false;
        }

#ifdef DEBUG
        // Disable per method via range
        //
        static ConfigMethodRange s_range;
        s_range.EnsureInit(JitConfig.JitEnableCrossBlockLocalAssertionPropRange());
        if (!s_range.Contains(info.compMethodHash()))
        {
            JITDUMP("Disabling cross-block assertion prop by config range\n");
            optCrossBlockLocalAssertionProp = false;
        }
#endif

        // Disable if too many locals
        //
        // The typical number of local assertions is roughly proportional
        // to the number of locals. So when we have huge numbers of locals,
        // just do within-block local assertion prop.
        //
        if (lvaCount > maxTrackedLocals)
        {
            JITDUMP("Disabling cross-block assertion prop: too many locals\n");
            optCrossBlockLocalAssertionProp = false;
        }

        if (optCrossBlockLocalAssertionProp)
        {
            // We may need a fairly large table. Keep size a multiple of 64.
            // Empirical studies show about 1.16 asserions/ tracked local.
            //
            if (lvaTrackedCount < 24)
            {
                optMaxAssertionCount = 64;
            }
            else if (lvaTrackedCount < 64)
            {
                optMaxAssertionCount = 128;
            }
            else
            {
                optMaxAssertionCount = (AssertionIndex)min(maxTrackedLocals, ((3 * lvaTrackedCount / 128) + 1) * 64);
            }

            JITDUMP("Cross-block table size %u (for %u tracked locals)\n", optMaxAssertionCount, lvaTrackedCount);
        }
        else
        {
            // The assertion table will be reset for each block, so it can be smaller.
            //
            optMaxAssertionCount = 64;
        }

        // Local assertion prop keeps mappings from each local var to the assertions about that var.
        //
        optAssertionDep =
            new (this, CMK_AssertionProp) JitExpandArray<ASSERT_TP>(getAllocator(CMK_AssertionProp), max(1u, lvaCount));

        if (optCrossBlockLocalAssertionProp)
        {
            optComplementaryAssertionMap = new (this, CMK_AssertionProp)
                AssertionIndex[optMaxAssertionCount + 1](); // zero-inited (NO_ASSERTION_INDEX)
        }
    }
    else
    {
        // General assertion prop.
        //
        optLocalAssertionProp           = false;
        optCrossBlockLocalAssertionProp = false;

        // Use a function countFunc to determine a proper maximum assertion count for the
        // method being compiled. The function is linear to the IL size for small and
        // moderate methods. For large methods, considering throughput impact, we track no
        // more than 64 assertions.
        // Note this tracks at most only 256 assertions.
        //
        static const AssertionIndex countFunc[] = {64, 128, 256, 128, 64};
        static const unsigned       upperBound  = ArrLen(countFunc) - 1;
        const unsigned              codeSize    = info.compILCodeSize / 512;
        optMaxAssertionCount                    = countFunc[min(upperBound, codeSize)];

        optValueNumToAsserts =
            new (getAllocator(CMK_AssertionProp)) ValueNumToAssertsMap(getAllocator(CMK_AssertionProp));
        optComplementaryAssertionMap = new (this, CMK_AssertionProp)
            AssertionIndex[optMaxAssertionCount + 1](); // zero-inited (NO_ASSERTION_INDEX)
    }

    optAssertionTabPrivate = new (this, CMK_AssertionProp) AssertionDsc[optMaxAssertionCount];
    optAssertionTraitsInit(optMaxAssertionCount);

    optAssertionCount      = 0;
    optAssertionOverflow   = 0;
    optAssertionPropagated = false;
    bbJtrueAssertionOut    = nullptr;
    optCanPropLclVar       = false;
    optCanPropEqual        = false;
    optCanPropNonNull      = false;
    optCanPropBndsChk      = false;
    optCanPropSubRange     = false;
}

#ifdef DEBUG
void Compiler::optPrintAssertion(AssertionDsc* curAssertion, AssertionIndex assertionIndex /* = 0 */)
{
    if (curAssertion->op1.kind == O1K_EXACT_TYPE)
    {
        printf("Type     ");
    }
    else if (curAssertion->op1.kind == O1K_ARR_BND)
    {
        printf("ArrBnds  ");
    }
    else if (curAssertion->op1.kind == O1K_VN)
    {
        printf("Vn  ");
    }
    else if (curAssertion->op1.kind == O1K_SUBTYPE)
    {
        printf("Subtype  ");
    }
    else if (curAssertion->op2.kind == O2K_LCLVAR_COPY)
    {
        printf("Copy     ");
    }
    else if ((curAssertion->op2.kind == O2K_CONST_INT) || (curAssertion->op2.kind == O2K_CONST_DOUBLE) ||
             (curAssertion->op2.kind == O2K_ZEROOBJ))
    {
        printf("Constant ");
    }
    else if (curAssertion->op2.kind == O2K_SUBRANGE)
    {
        printf("Subrange ");
    }
    else
    {
        printf("?assertion classification? ");
    }
    printf("Assertion: ");

    if (!optLocalAssertionProp)
    {
        printf("(" FMT_VN "," FMT_VN ") ", curAssertion->op1.vn, curAssertion->op2.vn);
    }

    if (curAssertion->op1.kind == O1K_LCLVAR)
    {
        if (!optLocalAssertionProp)
        {
            printf("LCLVAR");
            vnStore->vnDump(this, curAssertion->op1.vn);
        }
        else
        {
            printf("V%02u", curAssertion->op1.lclNum);
        }
    }
    else if (curAssertion->op1.kind == O1K_EXACT_TYPE)
    {
        printf("Exact_Type");
        vnStore->vnDump(this, curAssertion->op1.vn);
    }
    else if (curAssertion->op1.kind == O1K_SUBTYPE)
    {
        printf("Sub_Type");
        vnStore->vnDump(this, curAssertion->op1.vn);
    }
    else if (curAssertion->op1.kind == O1K_ARR_BND)
    {
        printf("[idx: " FMT_VN, curAssertion->op1.bnd.vnIdx);
        vnStore->vnDump(this, curAssertion->op1.bnd.vnIdx);
        printf("; len: " FMT_VN, curAssertion->op1.bnd.vnLen);
        vnStore->vnDump(this, curAssertion->op1.bnd.vnLen);
        printf("]");
    }
    else if (curAssertion->op1.kind == O1K_VN)
    {
        printf("[vn: " FMT_VN, curAssertion->op1.vn);
        vnStore->vnDump(this, curAssertion->op1.vn);
        printf("]");
    }
    else if (curAssertion->op1.kind == O1K_BOUND_OPER_BND)
    {
        printf("Oper_Bnd");
        vnStore->vnDump(this, curAssertion->op1.vn);
    }
    else if (curAssertion->op1.kind == O1K_BOUND_LOOP_BND)
    {
        printf("Loop_Bnd");
        vnStore->vnDump(this, curAssertion->op1.vn);
    }
    else if (curAssertion->op1.kind == O1K_CONSTANT_LOOP_BND)
    {
        printf("Const_Loop_Bnd");
        vnStore->vnDump(this, curAssertion->op1.vn);
    }
    else if (curAssertion->op1.kind == O1K_CONSTANT_LOOP_BND_UN)
    {
        printf("Const_Loop_Bnd_Un");
        vnStore->vnDump(this, curAssertion->op1.vn);
    }
    else
    {
        printf("?op1.kind?");
    }

    if (curAssertion->assertionKind == OAK_SUBRANGE)
    {
        printf(" in ");
    }
    else if (curAssertion->assertionKind == OAK_EQUAL)
    {
        if (curAssertion->op1.kind == O1K_LCLVAR)
        {
            printf(" == ");
        }
        else
        {
            printf(" is ");
        }
    }
    else if (curAssertion->assertionKind == OAK_NO_THROW)
    {
        printf(" in range ");
    }
    else if (curAssertion->assertionKind == OAK_NOT_EQUAL)
    {
        if (curAssertion->op1.kind == O1K_LCLVAR)
        {
            printf(" != ");
        }
        else
        {
            printf(" is not ");
        }
    }
    else
    {
        printf(" ?assertionKind? ");
    }

    if (curAssertion->op1.kind != O1K_ARR_BND)
    {
        switch (curAssertion->op2.kind)
        {
            case O2K_LCLVAR_COPY:
                printf("V%02u", curAssertion->op2.lclNum);
                break;

            case O2K_CONST_INT:
                if (curAssertion->op1.kind == O1K_EXACT_TYPE)
                {
                    ssize_t iconVal = curAssertion->op2.u1.iconVal;
                    if (IsAot())
                    {
                        printf("Exact Type MT(0x%p)", dspPtr(iconVal));
                    }
                    else
                    {
                        printf("Exact Type MT(0x%p %s)", dspPtr(iconVal),
                               eeGetClassName((CORINFO_CLASS_HANDLE)iconVal));
                    }

                    // We might want to assert:
                    //      assert(curAssertion->op2.HasIconFlag());
                    // However, if we run CSE with shared constant mode, we may end up with an expression instead
                    // of the original handle value. If we then use JitOptRepeat to re-build value numbers, we lose
                    // knowledge that the constant was ever a handle, as the expression creating the original value
                    // was not (and can't be) assigned a handle flag.
                }
                else if (curAssertion->op1.kind == O1K_SUBTYPE)
                {
                    ssize_t iconVal = curAssertion->op2.u1.iconVal;
                    if (IsAot())
                    {
                        printf("MT(0x%p)", dspPtr(iconVal));
                    }
                    else
                    {
                        printf("MT(0x%p %s)", dspPtr(iconVal), eeGetClassName((CORINFO_CLASS_HANDLE)iconVal));
                    }
                    assert(curAssertion->op2.HasIconFlag());
                }
                else if ((curAssertion->op1.kind == O1K_BOUND_OPER_BND) ||
                         (curAssertion->op1.kind == O1K_BOUND_LOOP_BND) ||
                         (curAssertion->op1.kind == O1K_CONSTANT_LOOP_BND) ||
                         (curAssertion->op1.kind == O1K_CONSTANT_LOOP_BND_UN))
                {
                    assert(!optLocalAssertionProp);
                    vnStore->vnDump(this, curAssertion->op2.vn);
                }
                else
                {
                    var_types op1Type = !optLocalAssertionProp ? vnStore->TypeOfVN(curAssertion->op1.vn)
                                                               : lvaGetRealType(curAssertion->op1.lclNum);
                    if (op1Type == TYP_REF)
                    {
                        if (curAssertion->op2.u1.iconVal == 0)
                        {
                            printf("null");
                        }
                        else
                        {
                            printf("[%08p]", dspPtr(curAssertion->op2.u1.iconVal));
                        }
                    }
                    else
                    {
                        if (curAssertion->op2.HasIconFlag())
                        {
                            printf("[%08p]", dspPtr(curAssertion->op2.u1.iconVal));
                        }
                        else
                        {
                            printf("%d", curAssertion->op2.u1.iconVal);
                        }
                    }
                }
                break;

            case O2K_CONST_DOUBLE:
                if (FloatingPointUtils::isNegativeZero(curAssertion->op2.dconVal))
                {
                    printf("-0.00000");
                }
                else
                {
                    printf("%#lg", curAssertion->op2.dconVal);
                }
                break;

            case O2K_ZEROOBJ:
                printf("ZeroObj");
                break;

            case O2K_SUBRANGE:
                IntegralRange::Print(curAssertion->op2.u2);
                break;

            default:
                printf("?op2.kind?");
                break;
        }
    }

    if (assertionIndex > 0)
    {
        printf(", index = ");
        optPrintAssertionIndex(assertionIndex);
    }
    printf("\n");
}

void Compiler::optPrintAssertionIndex(AssertionIndex index)
{
    if (index == NO_ASSERTION_INDEX)
    {
        printf("#NA");
        return;
    }

    printf("#%02u", index);
}

void Compiler::optPrintAssertionIndices(ASSERT_TP assertions)
{
    if (BitVecOps::IsEmpty(apTraits, assertions))
    {
        optPrintAssertionIndex(NO_ASSERTION_INDEX);
        return;
    }

    BitVecOps::Iter iter(apTraits, assertions);
    unsigned        bitIndex = 0;
    if (iter.NextElem(&bitIndex))
    {
        optPrintAssertionIndex(static_cast<AssertionIndex>(bitIndex + 1));
        while (iter.NextElem(&bitIndex))
        {
            printf(" ");
            optPrintAssertionIndex(static_cast<AssertionIndex>(bitIndex + 1));
        }
    }
}
#endif // DEBUG

/* static */
void Compiler::optDumpAssertionIndices(const char* header, ASSERT_TP assertions, const char* footer /* = nullptr */)
{
#ifdef DEBUG
    Compiler* compiler = JitTls::GetCompiler();
    if (compiler->verbose)
    {
        printf(header);
        compiler->optPrintAssertionIndices(assertions);
        if (footer != nullptr)
        {
            printf(footer);
        }
    }
#endif // DEBUG
}

/* static */
void Compiler::optDumpAssertionIndices(ASSERT_TP assertions, const char* footer /* = nullptr */)
{
    optDumpAssertionIndices("", assertions, footer);
}

/******************************************************************************
 *
 * Helper to retrieve the "assertIndex" assertion. Note that assertIndex 0
 * is NO_ASSERTION_INDEX and "optAssertionCount" is the last valid index.
 *
 */
Compiler::AssertionDsc* Compiler::optGetAssertion(AssertionIndex assertIndex)
{
    assert(NO_ASSERTION_INDEX == 0);
    assert(assertIndex != NO_ASSERTION_INDEX);
    assert(assertIndex <= optAssertionCount);
    AssertionDsc* assertion = &optAssertionTabPrivate[assertIndex - 1];
#ifdef DEBUG
    optDebugCheckAssertion(assertion);
#endif

    return assertion;
}

ValueNum Compiler::optConservativeNormalVN(GenTree* tree)
{
    if (optLocalAssertionProp)
    {
        return ValueNumStore::NoVN;
    }

    assert(vnStore != nullptr);
    return vnStore->VNConservativeNormalValue(tree->gtVNPair);
}

//------------------------------------------------------------------------
// optCastConstantSmall: Cast a constant to a small type.
//
// Parameters:
//   iconVal - the integer constant
//   smallType - the small type to cast to
//
// Returns:
//   The cast constant after sign/zero extension.
//
ssize_t Compiler::optCastConstantSmall(ssize_t iconVal, var_types smallType)
{
    switch (smallType)
    {
        case TYP_BYTE:
            return int8_t(iconVal);

        case TYP_SHORT:
            return int16_t(iconVal);

        case TYP_USHORT:
            return uint16_t(iconVal);

        case TYP_UBYTE:
            return uint8_t(iconVal);

        default:
            assert(!"Unexpected type to truncate to");
            return iconVal;
    }
}

//------------------------------------------------------------------------
// optCreateAssertion: Create an (op1 assertionKind op2) assertion.
//
// Arguments:
//    op1 - the first assertion operand
//    op2 - the second assertion operand
//    assertionKind - the assertion kind
//    helperCallArgs - when true this indicates that the assertion operands
//                     are the arguments of a type cast helper call such as
//                     CORINFO_HELP_ISINSTANCEOFCLASS
// Return Value:
//    The new assertion index or NO_ASSERTION_INDEX if a new assertion
//    was not created.
//
// Notes:
//    Assertion creation may fail either because the provided assertion
//    operands aren't supported or because the assertion table is full.
//
AssertionIndex Compiler::optCreateAssertion(GenTree* op1, GenTree* op2, optAssertionKind assertionKind)
{
    assert(op1 != nullptr);

    AssertionDsc assertion = {OAK_INVALID};
    assert(assertion.assertionKind == OAK_INVALID);

    if (op1->OperIs(GT_BOUNDS_CHECK) && (assertionKind == OAK_NO_THROW))
    {
        GenTreeBoundsChk* arrBndsChk = op1->AsBoundsChk();
        assertion.assertionKind      = assertionKind;
        assertion.op1.kind           = O1K_ARR_BND;
        assertion.op1.bnd.vnIdx      = optConservativeNormalVN(arrBndsChk->GetIndex());
        assertion.op1.bnd.vnLen      = optConservativeNormalVN(arrBndsChk->GetArrayLength());
    }
    //
    // Are we trying to make a non-null assertion?
    // (note we now do this for all indirs, regardless of address type)
    //
    else if (op2 == nullptr)
    {
        // Must be an OAK_NOT_EQUAL assertion
        assert(assertionKind == OAK_NOT_EQUAL);

        // Set op1 to the instance pointer of the indirection
        op1 = op1->gtEffectiveVal();

        ssize_t offset = 0;
        while (op1->OperIs(GT_ADD) && op1->TypeIs(TYP_BYREF))
        {
            if (op1->gtGetOp2()->IsCnsIntOrI())
            {
                offset += op1->gtGetOp2()->AsIntCon()->gtIconVal;
                op1 = op1->gtGetOp1()->gtEffectiveVal();
            }
            else if (op1->gtGetOp1()->IsCnsIntOrI())
            {
                offset += op1->gtGetOp1()->AsIntCon()->gtIconVal;
                op1 = op1->gtGetOp2()->gtEffectiveVal();
            }
            else
            {
                break;
            }
        }

        if (!fgIsBigOffset(offset) && op1->OperIs(GT_LCL_VAR) && !lvaVarAddrExposed(op1->AsLclVar()->GetLclNum()))
        {
            assertion.op1.kind       = O1K_LCLVAR;
            assertion.op1.lclNum     = op1->AsLclVarCommon()->GetLclNum();
            assertion.op1.vn         = optConservativeNormalVN(op1);
            assertion.assertionKind  = assertionKind;
            assertion.op2.kind       = O2K_CONST_INT;
            assertion.op2.vn         = ValueNumStore::VNForNull();
            assertion.op2.u1.iconVal = 0;
            assertion.op2.SetIconFlag(GTF_EMPTY);
        }
    }
    //
    // Are we making an assertion about a local variable?
    //
    else if (op1->OperIsScalarLocal())
    {
        unsigned const   lclNum = op1->AsLclVarCommon()->GetLclNum();
        LclVarDsc* const lclVar = lvaGetDesc(lclNum);

        // If the local variable has its address exposed then bail
        //
        if (lclVar->IsAddressExposed())
        {
            goto DONE_ASSERTION; // Don't make an assertion
        }

        {
            /* Skip over a GT_COMMA node(s), if necessary */
            while (op2->OperIs(GT_COMMA))
            {
                op2 = op2->AsOp()->gtOp2;
            }

            assertion.op1.kind   = O1K_LCLVAR;
            assertion.op1.lclNum = lclNum;
            assertion.op1.vn     = optConservativeNormalVN(op1);

            switch (op2->gtOper)
            {
                optOp2Kind op2Kind;

                //
                //  Constant Assertions
                //
                case GT_CNS_INT:
                    if (op1->TypeIs(TYP_STRUCT))
                    {
                        assert(op2->IsIntegralConst(0));
                        op2Kind = O2K_ZEROOBJ;
                    }
                    else
                    {
                        op2Kind = O2K_CONST_INT;
                    }
                    goto CNS_COMMON;

                case GT_CNS_DBL:
                    op2Kind = O2K_CONST_DOUBLE;
                    goto CNS_COMMON;

                CNS_COMMON:
                {
                    //
                    // Must either be an OAK_EQUAL or an OAK_NOT_EQUAL assertion
                    //
                    if ((assertionKind != OAK_EQUAL) && (assertionKind != OAK_NOT_EQUAL))
                    {
                        goto DONE_ASSERTION; // Don't make an assertion
                    }

                    assertion.op2.kind = op2Kind;
                    assertion.op2.vn   = optConservativeNormalVN(op2);

                    if (op2->OperIs(GT_CNS_INT))
                    {
                        ssize_t iconVal = op2->AsIntCon()->IconValue();
                        if (varTypeIsSmall(lclVar) && op1->OperIs(GT_STORE_LCL_VAR))
                        {
                            iconVal = optCastConstantSmall(iconVal, lclVar->TypeGet());
                            if (!optLocalAssertionProp)
                            {
                                assertion.op2.vn = vnStore->VNForIntCon(static_cast<int>(iconVal));
                            }
                        }
                        assertion.op2.u1.iconVal = iconVal;
                        assertion.op2.SetIconFlag(op2->GetIconHandleFlag(), op2->AsIntCon()->gtFieldSeq);
                    }
                    else
                    {
                        noway_assert(op2->OperIs(GT_CNS_DBL));
                        /* If we have an NaN value then don't record it */
                        if (FloatingPointUtils::isNaN(op2->AsDblCon()->DconValue()))
                        {
                            goto DONE_ASSERTION; // Don't make an assertion
                        }
                        assertion.op2.dconVal = op2->AsDblCon()->DconValue();
                    }

                    //
                    // Ok everything has been set and the assertion looks good
                    //
                    assertion.assertionKind = assertionKind;

                    goto DONE_ASSERTION;
                }

                case GT_LCL_VAR:
                {
                    if (!optLocalAssertionProp)
                    {
                        // O2K_LCLVAR_COPY is local assertion prop only
                        goto DONE_ASSERTION;
                    }

                    // Must either be an OAK_EQUAL or an OAK_NOT_EQUAL assertion
                    if ((assertionKind != OAK_EQUAL) && (assertionKind != OAK_NOT_EQUAL))
                    {
                        goto DONE_ASSERTION; // Don't make an assertion
                    }

                    unsigned   lclNum2 = op2->AsLclVarCommon()->GetLclNum();
                    LclVarDsc* lclVar2 = lvaGetDesc(lclNum2);

                    // If the two locals are the same then bail
                    if (lclNum == lclNum2)
                    {
                        goto DONE_ASSERTION; // Don't make an assertion
                    }

                    // If the types are different then bail */
                    if (lclVar->lvType != lclVar2->lvType)
                    {
                        goto DONE_ASSERTION; // Don't make an assertion
                    }

                    // If we're making a copy of a "normalize on load" lclvar then the destination
                    // has to be "normalize on load" as well, otherwise we risk skipping normalization.
                    if (lclVar2->lvNormalizeOnLoad() && !lclVar->lvNormalizeOnLoad())
                    {
                        goto DONE_ASSERTION; // Don't make an assertion
                    }

                    //  If the local variable has its address exposed then bail
                    if (lclVar2->IsAddressExposed())
                    {
                        goto DONE_ASSERTION; // Don't make an assertion
                    }

                    // We process locals when we see the LCL_VAR node instead
                    // of at its actual use point (its parent). That opens us
                    // up to problems in a case like the following, assuming we
                    // allowed creating an assertion like V10 = V35:
                    //
                    // └──▌  ADD       int
                    //    ├──▌  LCL_VAR   int    V10 tmp6        -> copy propagated to [V35 tmp31]
                    //    └──▌  COMMA     int
                    //       ├──▌  STORE_LCL_VAR int    V35 tmp31
                    //       │  └──▌  LCL_FLD   int    V03 loc1         [+4]
                    if (lclVar2->lvRedefinedInEmbeddedStatement)
                    {
                        goto DONE_ASSERTION; // Don't make an assertion
                    }

                    assertion.op2.kind   = O2K_LCLVAR_COPY;
                    assertion.op2.vn     = optConservativeNormalVN(op2);
                    assertion.op2.lclNum = lclNum2;

                    // Ok everything has been set and the assertion looks good
                    assertion.assertionKind = assertionKind;

                    goto DONE_ASSERTION;
                }

                case GT_CALL:
                {
                    if (optLocalAssertionProp)
                    {
                        GenTreeCall* const call = op2->AsCall();
                        if (call->IsHelperCall() && s_helperCallProperties.NonNullReturn(call->GetHelperNum()))
                        {
                            assertion.assertionKind  = OAK_NOT_EQUAL;
                            assertion.op2.kind       = O2K_CONST_INT;
                            assertion.op2.u1.iconVal = 0;
                            goto DONE_ASSERTION;
                        }
                    }
                    break;
                }

                default:
                    break;
            }

            // Try and see if we can make a subrange assertion.
            if (((assertionKind == OAK_SUBRANGE) || (assertionKind == OAK_EQUAL)) && varTypeIsIntegral(op2))
            {
                IntegralRange nodeRange = IntegralRange::ForNode(op2, this);
                IntegralRange typeRange = IntegralRange::ForType(genActualType(op2));
                assert(typeRange.Contains(nodeRange));

                if (!typeRange.Equals(nodeRange))
                {
                    assertion.op2.kind      = O2K_SUBRANGE;
                    assertion.assertionKind = OAK_SUBRANGE;
                    assertion.op2.u2        = nodeRange;
                }
            }
        }
    }
    else
    {
        // Currently, O1K_VN serves as a backup for O1K_LCLVAR (where it's not a local),
        // but long term we should keep O1K_LCLVAR for local assertions only.
        if (!optLocalAssertionProp)
        {
            ValueNum op1VN = optConservativeNormalVN(op1);
            ValueNum op2VN = optConservativeNormalVN(op2);

            // For TP reasons, limited to 32-bit constants on the op2 side.
            if (vnStore->IsVNInt32Constant(op2VN) && !vnStore->IsVNHandle(op2VN))
            {
                assert(assertionKind == OAK_EQUAL || assertionKind == OAK_NOT_EQUAL);
                assertion.assertionKind  = assertionKind;
                assertion.op1.vn         = op1VN;
                assertion.op1.kind       = O1K_VN;
                assertion.op2.vn         = op2VN;
                assertion.op2.kind       = O2K_CONST_INT;
                assertion.op2.u1.iconVal = vnStore->ConstantValue<int>(op2VN);
                assertion.op2.SetIconFlag(GTF_EMPTY);
                return optAddAssertion(&assertion);
            }
        }
    }

DONE_ASSERTION:
    return optFinalizeCreatingAssertion(&assertion);
}

//------------------------------------------------------------------------
// optFinalizeCreatingAssertion: Add the assertion, if well-formed, to the table.
//
// Checks that in global assertion propagation assertions do not have missing
// value and SSA numbers.
//
// Arguments:
//    assertion - assertion to check and add to the table
//
// Return Value:
//    Index of the assertion if it was successfully created, NO_ASSERTION_INDEX otherwise.
//
AssertionIndex Compiler::optFinalizeCreatingAssertion(AssertionDsc* assertion)
{
    if (assertion->assertionKind == OAK_INVALID)
    {
        return NO_ASSERTION_INDEX;
    }

    if (!optLocalAssertionProp)
    {
        if ((assertion->op1.vn == ValueNumStore::NoVN) || (assertion->op2.vn == ValueNumStore::NoVN) ||
            (assertion->op1.vn == ValueNumStore::VNForVoid()) || (assertion->op2.vn == ValueNumStore::VNForVoid()))
        {
            return NO_ASSERTION_INDEX;
        }
    }

    // Now add the assertion to our assertion table
    noway_assert(assertion->op1.kind != O1K_INVALID);
    noway_assert((assertion->op1.kind == O1K_ARR_BND) || (assertion->op2.kind != O2K_INVALID));

    return optAddAssertion(assertion);
}

/*****************************************************************************
 *
 * If tree is a constant node holding an integral value, retrieve the value in
 * pConstant. If the method returns true, pConstant holds the appropriate
 * constant. Set "vnBased" to true to indicate local or global assertion prop.
 * "pFlags" indicates if the constant is a handle marked by GTF_ICON_HDL_MASK.
 */
bool Compiler::optIsTreeKnownIntValue(bool vnBased, GenTree* tree, ssize_t* pConstant, GenTreeFlags* pFlags)
{
    // Is Local assertion prop?
    if (!vnBased)
    {
        if (tree->OperIs(GT_CNS_INT))
        {
            *pConstant = tree->AsIntCon()->IconValue();
            *pFlags    = tree->GetIconHandleFlag();
            return true;
        }
        return false;
    }

    // Global assertion prop
    ValueNum vn = vnStore->VNConservativeNormalValue(tree->gtVNPair);
    if (!vnStore->IsVNConstant(vn))
    {
        return false;
    }

    // ValueNumber 'vn' indicates that this node evaluates to a constant

    var_types vnType = vnStore->TypeOfVN(vn);
    if (vnType == TYP_INT)
    {
        *pConstant = vnStore->ConstantValue<int>(vn);
        *pFlags    = vnStore->IsVNHandle(vn) ? vnStore->GetHandleFlags(vn) : GTF_EMPTY;
        return true;
    }
#ifdef TARGET_64BIT
    else if (vnType == TYP_LONG)
    {
        *pConstant = vnStore->ConstantValue<INT64>(vn);
        *pFlags    = vnStore->IsVNHandle(vn) ? vnStore->GetHandleFlags(vn) : GTF_EMPTY;
        return true;
    }
#endif

    return false;
}

#ifdef DEBUG
/*****************************************************************************
 *
 * Print the assertions related to a VN for all VNs.
 *
 */
void Compiler::optPrintVnAssertionMapping()
{
    printf("\nVN Assertion Mapping\n");
    printf("---------------------\n");
    for (ValueNumToAssertsMap::Node* const iter : ValueNumToAssertsMap::KeyValueIteration(optValueNumToAsserts))
    {
        printf("(%d => %s)\n", iter->GetKey(), BitVecOps::ToString(apTraits, iter->GetValue()));
    }
}
#endif

/*****************************************************************************
 *
 * Maintain a map "optValueNumToAsserts" i.e., vn -> to set of assertions
 * about that VN. Given "assertions" about a "vn" add it to the previously
 * mapped assertions about that "vn."
 */
void Compiler::optAddVnAssertionMapping(ValueNum vn, AssertionIndex index)
{
    ASSERT_TP* cur = optValueNumToAsserts->LookupPointer(vn);
    if (cur == nullptr)
    {
        optValueNumToAsserts->Set(vn, BitVecOps::MakeSingleton(apTraits, index - 1));
    }
    else
    {
        BitVecOps::AddElemD(apTraits, *cur, index - 1);
    }
}

/*****************************************************************************
 * Statically if we know that this assertion's VN involves a NaN don't bother
 * wasting an assertion table slot.
 */
bool Compiler::optAssertionVnInvolvesNan(AssertionDsc* assertion)
{
    if (optLocalAssertionProp)
    {
        return false;
    }

    static const int SZ      = 2;
    ValueNum         vns[SZ] = {assertion->op1.vn, assertion->op2.vn};
    for (int i = 0; i < SZ; ++i)
    {
        if (vnStore->IsVNConstant(vns[i]))
        {
            var_types type = vnStore->TypeOfVN(vns[i]);
            if ((type == TYP_FLOAT && FloatingPointUtils::isNaN(vnStore->ConstantValue<float>(vns[i])) != 0) ||
                (type == TYP_DOUBLE && FloatingPointUtils::isNaN(vnStore->ConstantValue<double>(vns[i])) != 0))
            {
                return true;
            }
        }
    }
    return false;
}

/*****************************************************************************
 *
 *  Given an assertion add it to the assertion table
 *
 *  If it is already in the assertion table return the assertionIndex that
 *  we use to refer to this element.
 *  Otherwise add it to the assertion table and return the assertionIndex that
 *  we use to refer to this element.
 *  If we need to add to the table and the table is full return the value zero
 */
AssertionIndex Compiler::optAddAssertion(AssertionDsc* newAssertion)
{
    noway_assert(newAssertion->assertionKind != OAK_INVALID);

    // Even though the propagation step takes care of NaN, just a check
    // to make sure there is no slot involving a NaN.
    if (optAssertionVnInvolvesNan(newAssertion))
    {
        JITDUMP("Assertion involved Nan not adding\n");
        return NO_ASSERTION_INDEX;
    }

    if (!optLocalAssertionProp)
    {
        // Ignore VN-based assertions with NoVN
        switch (newAssertion->op1.kind)
        {
            case O1K_LCLVAR:
            case O1K_VN:
            case O1K_BOUND_OPER_BND:
            case O1K_BOUND_LOOP_BND:
            case O1K_CONSTANT_LOOP_BND:
            case O1K_CONSTANT_LOOP_BND_UN:
            case O1K_EXACT_TYPE:
            case O1K_SUBTYPE:
                if (newAssertion->op1.vn == ValueNumStore::NoVN)
                {
                    return NO_ASSERTION_INDEX;
                }
                break;
            case O1K_ARR_BND:
                if ((newAssertion->op1.bnd.vnIdx == ValueNumStore::NoVN) ||
                    (newAssertion->op1.bnd.vnLen == ValueNumStore::NoVN))
                {
                    return NO_ASSERTION_INDEX;
                }
                break;

            default:
                break;
        }
    }

    // See if we already have this assertion in the table.
    //
    // For local assertion prop we can speed things up by checking the dep vector.
    // Note we only need check the op1 vector; copies get indexed on both op1
    // and op2, so searching the first will find any existing match.
    //
    if (optLocalAssertionProp)
    {
        assert(newAssertion->op1.kind == O1K_LCLVAR);

        unsigned        lclNum = newAssertion->op1.lclNum;
        BitVecOps::Iter iter(apTraits, GetAssertionDep(lclNum));
        unsigned        bvIndex = 0;
        while (iter.NextElem(&bvIndex))
        {
            AssertionIndex const index        = GetAssertionIndex(bvIndex);
            AssertionDsc* const  curAssertion = optGetAssertion(index);

            if (curAssertion->Equals(newAssertion, /* vnBased */ false))
            {
                return index;
            }
        }
    }
    else
    {
        // For global prop we search the entire table.
        //
        // Check if exists already, so we can skip adding new one. Search backwards.
        for (AssertionIndex index = optAssertionCount; index >= 1; index--)
        {
            AssertionDsc* curAssertion = optGetAssertion(index);
            if (curAssertion->Equals(newAssertion, /* vnBased */ true))
            {
                return index;
            }
        }
    }

    // Check if we are within max count.
    if (optAssertionCount >= optMaxAssertionCount)
    {
        optAssertionOverflow++;
        return NO_ASSERTION_INDEX;
    }

    optAssertionTabPrivate[optAssertionCount] = *newAssertion;
    optAssertionCount++;

#ifdef DEBUG
    if (verbose)
    {
        printf("GenTreeNode creates assertion:\n");
        gtDispTree(optAssertionPropCurrentTree, nullptr, nullptr, true);
        printf(optLocalAssertionProp ? "In " FMT_BB " New Local " : "In " FMT_BB " New Global ", compCurBB->bbNum);
        optPrintAssertion(newAssertion, optAssertionCount);
    }
#endif // DEBUG

    // Track the short-circuit criteria
    optCanPropLclVar |= newAssertion->CanPropLclVar();
    optCanPropEqual |= newAssertion->CanPropEqualOrNotEqual();
    optCanPropNonNull |= newAssertion->CanPropNonNull();
    optCanPropSubRange |= newAssertion->CanPropSubRange();
    optCanPropBndsChk |= newAssertion->CanPropBndsCheck();

    // Assertion mask bits are [index + 1].
    if (optLocalAssertionProp)
    {
        assert(newAssertion->op1.kind == O1K_LCLVAR);

        // Mark the variables this index depends on
        unsigned lclNum = newAssertion->op1.lclNum;
        BitVecOps::AddElemD(apTraits, GetAssertionDep(lclNum), optAssertionCount - 1);
        if (newAssertion->op2.kind == O2K_LCLVAR_COPY)
        {
            lclNum = newAssertion->op2.lclNum;
            BitVecOps::AddElemD(apTraits, GetAssertionDep(lclNum), optAssertionCount - 1);
        }
    }
    else
    // If global assertion prop, then add it to the dependents map.
    {
        optAddVnAssertionMapping(newAssertion->op1.vn, optAssertionCount);
        if (newAssertion->op2.kind == O2K_LCLVAR_COPY)
        {
            optAddVnAssertionMapping(newAssertion->op2.vn, optAssertionCount);
        }
    }

#ifdef DEBUG
    optDebugCheckAssertions(optAssertionCount);
#endif
    return optAssertionCount;
}

#ifdef DEBUG
void Compiler::optDebugCheckAssertion(AssertionDsc* assertion)
{
    assert(assertion->assertionKind < OAK_COUNT);
    assert(assertion->op1.kind < O1K_COUNT);
    assert(assertion->op2.kind < O2K_COUNT);
    // It would be good to check that op1.vn and op2.vn are valid value numbers.

    switch (assertion->op1.kind)
    {
        case O1K_ARR_BND:
            // It would be good to check that bnd.vnIdx and bnd.vnLen are valid value numbers.
            assert(!optLocalAssertionProp);
            assert(assertion->assertionKind == OAK_NO_THROW);
            break;
        case O1K_EXACT_TYPE:
        case O1K_SUBTYPE:
        case O1K_VN:
        case O1K_BOUND_OPER_BND:
        case O1K_BOUND_LOOP_BND:
        case O1K_CONSTANT_LOOP_BND:
        case O1K_CONSTANT_LOOP_BND_UN:
            assert(!optLocalAssertionProp);
            break;
        default:
            break;
    }
    switch (assertion->op2.kind)
    {
        case O2K_SUBRANGE:
        case O2K_LCLVAR_COPY:
            assert(optLocalAssertionProp);
            break;

        case O2K_ZEROOBJ:
        {
            // We only make these assertion for stores (not control flow).
            assert(assertion->assertionKind == OAK_EQUAL);
            // We use "optLocalAssertionIsEqualOrNotEqual" to find these.
            assert(assertion->op2.u1.iconVal == 0);
        }
        break;

        default:
            // for all other 'assertion->op2.kind' values we don't check anything
            break;
    }
}

/*****************************************************************************
 *
 *  Verify that assertion prop related assumptions are valid. If "index"
 *  is 0 (i.e., NO_ASSERTION_INDEX) then verify all assertions in the table.
 *  If "index" is between 1 and optAssertionCount, then verify the assertion
 *  desc corresponding to "index."
 */
void Compiler::optDebugCheckAssertions(AssertionIndex index)
{
    AssertionIndex start = (index == NO_ASSERTION_INDEX) ? 1 : index;
    AssertionIndex end   = (index == NO_ASSERTION_INDEX) ? optAssertionCount : index;
    for (AssertionIndex ind = start; ind <= end; ++ind)
    {
        AssertionDsc* assertion = optGetAssertion(ind);
        optDebugCheckAssertion(assertion);
    }
}
#endif

//------------------------------------------------------------------------
// optCreateComplementaryAssertion: Create an assertion that is the complementary
//     of the specified assertion.
//
// Arguments:
//    assertionIndex - the index of the assertion
//    op1 - the first assertion operand
//    op2 - the second assertion operand
//
// Notes:
//    The created complementary assertion is associated with the original
//    assertion such that it can be found by optFindComplementary.
//
void Compiler::optCreateComplementaryAssertion(AssertionIndex assertionIndex, GenTree* op1, GenTree* op2)
{
    if (assertionIndex == NO_ASSERTION_INDEX)
    {
        return;
    }

    AssertionDsc& candidateAssertion = *optGetAssertion(assertionIndex);
    if ((candidateAssertion.op1.kind == O1K_BOUND_OPER_BND) || (candidateAssertion.op1.kind == O1K_BOUND_LOOP_BND) ||
        (candidateAssertion.op1.kind == O1K_CONSTANT_LOOP_BND) ||
        (candidateAssertion.op1.kind == O1K_CONSTANT_LOOP_BND_UN))
    {
        AssertionDsc dsc  = candidateAssertion;
        dsc.assertionKind = dsc.assertionKind == OAK_EQUAL ? OAK_NOT_EQUAL : OAK_EQUAL;
        optAddAssertion(&dsc);
        return;
    }

    if (candidateAssertion.assertionKind == OAK_EQUAL)
    {
        // Don't create useless OAK_NOT_EQUAL assertions

        if ((candidateAssertion.op1.kind == O1K_LCLVAR) || (candidateAssertion.op1.kind == O1K_VN))
        {
            // "LCLVAR != CNS" is not a useful assertion (unless CNS is 0/1)
            if (((candidateAssertion.op2.kind == O2K_CONST_INT)) && (candidateAssertion.op2.u1.iconVal != 0) &&
                (candidateAssertion.op2.u1.iconVal != 1))
            {
                return;
            }

            // "LCLVAR != LCLVAR_COPY"
            if (candidateAssertion.op2.kind == O2K_LCLVAR_COPY)
            {
                return;
            }
        }

        // "Object is not Class" is also not a useful assertion (at least for now)
        if ((candidateAssertion.op1.kind == O1K_EXACT_TYPE) || (candidateAssertion.op1.kind == O1K_SUBTYPE))
        {
            return;
        }

        AssertionIndex index = optCreateAssertion(op1, op2, OAK_NOT_EQUAL);
        optMapComplementary(index, assertionIndex);
    }
    else if (candidateAssertion.assertionKind == OAK_NOT_EQUAL)
    {
        AssertionIndex index = optCreateAssertion(op1, op2, OAK_EQUAL);
        optMapComplementary(index, assertionIndex);
    }
}

//------------------------------------------------------------------------
// optCreateJtrueAssertions: Create assertions about a JTRUE's relop operands.
//
// Arguments:
//    op1 - the first assertion operand
//    op2 - the second assertion operand
//    assertionKind - the assertion kind
//
// Return Value:
//    The new assertion index or NO_ASSERTION_INDEX if a new assertion
//    was not created.
//
// Notes:
//    Assertion creation may fail either because the provided assertion
//    operands aren't supported or because the assertion table is full.
//    If an assertion is created successfully then an attempt is made to also
//    create a second, complementary assertion. This may too fail, for the
//    same reasons as the first one.
//
AssertionIndex Compiler::optCreateJtrueAssertions(GenTree* op1, GenTree* op2, optAssertionKind assertionKind)
{
    AssertionIndex assertionIndex = optCreateAssertion(op1, op2, assertionKind);
    // Don't bother if we don't have an assertion on the JTrue False path. Current implementation
    // allows for a complementary only if there is an assertion on the False path (tree->HasAssertion()).
    if (assertionIndex != NO_ASSERTION_INDEX)
    {
        optCreateComplementaryAssertion(assertionIndex, op1, op2);
    }
    return assertionIndex;
}

AssertionInfo Compiler::optCreateJTrueBoundsAssertion(GenTree* tree)
{
    // These assertions are VN based, so not relevant for local prop
    //
    if (optLocalAssertionProp)
    {
        return NO_ASSERTION_INDEX;
    }

    GenTree* relop = tree->gtGetOp1();
    if (!relop->OperIsCompare())
    {
        return NO_ASSERTION_INDEX;
    }
    GenTree* op2     = relop->gtGetOp2();
    ValueNum relopVN = vnStore->VNConservativeNormalValue(relop->gtVNPair);

    ValueNumStore::UnsignedCompareCheckedBoundInfo unsignedCompareBnd;

    // Cases where op1 holds the lhs of the condition and op2 holds the bound arithmetic.
    // Loop condition like: "i < bnd +/-k"
    // Assertion: "i < bnd +/- k != 0"
    if (vnStore->IsVNCompareCheckedBoundArith(relopVN))
    {
        AssertionDsc dsc;
        dsc.assertionKind  = OAK_NOT_EQUAL;
        dsc.op1.kind       = O1K_BOUND_OPER_BND;
        dsc.op1.vn         = relopVN;
        dsc.op2.kind       = O2K_CONST_INT;
        dsc.op2.vn         = vnStore->VNZeroForType(op2->TypeGet());
        dsc.op2.u1.iconVal = 0;
        dsc.op2.SetIconFlag(GTF_EMPTY);
        AssertionIndex index = optAddAssertion(&dsc);
        optCreateComplementaryAssertion(index, nullptr, nullptr);
        return index;
    }
    // Cases where op1 holds the lhs of the condition op2 holds the bound.
    // Loop condition like "i < bnd"
    // Assertion: "i < bnd != 0"
    else if (vnStore->IsVNCompareCheckedBound(relopVN))
    {
        AssertionDsc dsc;
        dsc.assertionKind  = OAK_NOT_EQUAL;
        dsc.op1.kind       = O1K_BOUND_LOOP_BND;
        dsc.op1.vn         = relopVN;
        dsc.op2.kind       = O2K_CONST_INT;
        dsc.op2.vn         = vnStore->VNZeroForType(TYP_INT);
        dsc.op2.u1.iconVal = 0;
        dsc.op2.SetIconFlag(GTF_EMPTY);
        AssertionIndex index = optAddAssertion(&dsc);
        optCreateComplementaryAssertion(index, nullptr, nullptr);
        return index;
    }
    // Loop condition like "(uint)i < (uint)bnd" or equivalent
    // Assertion: "no throw" since this condition guarantees that i is both >= 0 and < bnd (on the appropriate edge)
    else if (vnStore->IsVNUnsignedCompareCheckedBound(relopVN, &unsignedCompareBnd))
    {
        assert(unsignedCompareBnd.vnIdx != ValueNumStore::NoVN);
        assert((unsignedCompareBnd.cmpOper == VNF_LT_UN) || (unsignedCompareBnd.cmpOper == VNF_GE_UN));
        assert(vnStore->IsVNCheckedBound(unsignedCompareBnd.vnBound));

        AssertionDsc dsc;
        dsc.assertionKind = OAK_NO_THROW;
        dsc.op1.kind      = O1K_ARR_BND;
        dsc.op1.vn        = relopVN;
        dsc.op1.bnd.vnIdx = unsignedCompareBnd.vnIdx;
        dsc.op1.bnd.vnLen = vnStore->VNNormalValue(unsignedCompareBnd.vnBound);
        dsc.op2.kind      = O2K_INVALID;
        dsc.op2.vn        = ValueNumStore::NoVN;

        if ((dsc.op1.bnd.vnIdx == ValueNumStore::NoVN) || (dsc.op1.bnd.vnLen == ValueNumStore::NoVN))
        {
            // Don't make an assertion if one of the operands has no VN
            return NO_ASSERTION_INDEX;
        }

        AssertionIndex index = optAddAssertion(&dsc);
        if (unsignedCompareBnd.cmpOper == VNF_GE_UN)
        {
            // By default JTRUE generated assertions hold on the "jump" edge. We have i >= bnd but we're really
            // after i < bnd so we need to change the assertion edge to "next".
            return AssertionInfo::ForNextEdge(index);
        }
        return index;
    }
    // Cases where op1 holds the lhs of the condition op2 holds rhs.
    // Loop condition like "i < 100"
    // Assertion: "i < 100 != 0"
    else if (vnStore->IsVNConstantBound(relopVN))
    {
        AssertionDsc dsc;
        dsc.assertionKind  = OAK_NOT_EQUAL;
        dsc.op1.kind       = O1K_CONSTANT_LOOP_BND;
        dsc.op1.vn         = relopVN;
        dsc.op2.kind       = O2K_CONST_INT;
        dsc.op2.vn         = vnStore->VNZeroForType(TYP_INT);
        dsc.op2.u1.iconVal = 0;
        dsc.op2.SetIconFlag(GTF_EMPTY);
        AssertionIndex index = optAddAssertion(&dsc);
        optCreateComplementaryAssertion(index, nullptr, nullptr);
        return index;
    }
    else if (vnStore->IsVNConstantBoundUnsigned(relopVN))
    {
        AssertionDsc dsc;
        dsc.assertionKind  = OAK_NOT_EQUAL;
        dsc.op1.kind       = O1K_CONSTANT_LOOP_BND_UN;
        dsc.op1.vn         = relopVN;
        dsc.op2.kind       = O2K_CONST_INT;
        dsc.op2.vn         = vnStore->VNZeroForType(TYP_INT);
        dsc.op2.u1.iconVal = 0;
        dsc.op2.SetIconFlag(GTF_EMPTY);
        AssertionIndex index = optAddAssertion(&dsc);
        optCreateComplementaryAssertion(index, nullptr, nullptr);
        return index;
    }
    return NO_ASSERTION_INDEX;
}

/*****************************************************************************
 *
 *  Compute assertions for the JTrue node.
 */
AssertionInfo Compiler::optAssertionGenJtrue(GenTree* tree)
{
    GenTree* const relop = tree->AsOp()->gtOp1;
    if (!relop->OperIsCompare())
    {
        return NO_ASSERTION_INDEX;
    }

    Compiler::optAssertionKind assertionKind = OAK_INVALID;

    AssertionInfo info = optCreateJTrueBoundsAssertion(tree);
    if (info.HasAssertion())
    {
        return info;
    }

    if (optLocalAssertionProp && !optCrossBlockLocalAssertionProp)
    {
        return NO_ASSERTION_INDEX;
    }

    // Find assertion kind.
    switch (relop->gtOper)
    {
        case GT_EQ:
            assertionKind = OAK_EQUAL;
            break;
        case GT_NE:
            assertionKind = OAK_NOT_EQUAL;
            break;
        default:
            // TODO-CQ: add other relop operands. Disabled for now to measure perf
            // and not occupy assertion table slots. We'll add them when used.
            return NO_ASSERTION_INDEX;
    }

    // Look through any CSEs so we see the actual trees providing values, if possible.
    // This is important for exact type assertions, which need to see the GT_IND.
    //
    GenTree* op1 = relop->AsOp()->gtOp1->gtCommaStoreVal();
    GenTree* op2 = relop->AsOp()->gtOp2->gtCommaStoreVal();

    // Avoid creating local assertions for float types.
    //
    if (optLocalAssertionProp && varTypeIsFloating(op1))
    {
        return NO_ASSERTION_INDEX;
    }

    // See if we have IND(obj) ==/!= TypeHandle
    //
    if (!optLocalAssertionProp && op1->OperIs(GT_IND) && op1->gtGetOp1()->TypeIs(TYP_REF))
    {
        ValueNum objVN     = optConservativeNormalVN(op1->gtGetOp1());
        ValueNum typeHndVN = optConservativeNormalVN(op2);

        if ((objVN != ValueNumStore::NoVN) && vnStore->IsVNTypeHandle(typeHndVN))
        {
            AssertionDsc assertion;
            assertion.assertionKind  = OAK_EQUAL;
            assertion.op1.kind       = O1K_EXACT_TYPE;
            assertion.op1.vn         = objVN;
            assertion.op2.kind       = O2K_CONST_INT;
            assertion.op2.u1.iconVal = vnStore->CoercedConstantValue<ssize_t>(typeHndVN);
            assertion.op2.vn         = typeHndVN;
            assertion.op2.SetIconFlag(GTF_ICON_CLASS_HDL);
            AssertionIndex index = optAddAssertion(&assertion);

            // We don't need to create a complementary assertion here. We're only interested
            // in the assertion that the object is of a certain type. The opposite assertion
            // (that the object is not of a certain type) is not useful (at least not yet).
            //
            // So if we have "if (obj->pMT != CNS) then create the assertion for the "else" edge.
            if (relop->OperIs(GT_NE))
            {
                return AssertionInfo::ForNextEdge(index);
            }
            return index;
        }
    }

    // Check for op1 or op2 to be lcl var and if so, keep it in op1.
    if (!op1->OperIs(GT_LCL_VAR) && op2->OperIs(GT_LCL_VAR))
    {
        std::swap(op1, op2);
    }

    // If op1 is lcl and op2 is const or lcl, create assertion.
    if (op1->OperIs(GT_LCL_VAR) && (op2->OperIsConst() || op2->OperIs(GT_LCL_VAR))) // Fix for Dev10 851483
    {
        // Watch out for cases where long local(s) are implicitly truncated.
        //
        LclVarDsc* const lcl1Dsc = lvaGetDesc(op1->AsLclVarCommon());
        if (lcl1Dsc->TypeIs(TYP_LONG) && !op1->TypeIs(TYP_LONG))
        {
            return NO_ASSERTION_INDEX;
        }
        if (op2->OperIs(GT_LCL_VAR))
        {
            LclVarDsc* const lcl2Dsc = lvaGetDesc(op2->AsLclVarCommon());
            if (lcl2Dsc->TypeIs(TYP_LONG) && !op2->TypeIs(TYP_LONG))
            {
                return NO_ASSERTION_INDEX;
            }
        }

        return optCreateJtrueAssertions(op1, op2, assertionKind);
    }
    else if (!optLocalAssertionProp)
    {
        ValueNum op1VN = vnStore->VNConservativeNormalValue(op1->gtVNPair);
        ValueNum op2VN = vnStore->VNConservativeNormalValue(op2->gtVNPair);

        if (vnStore->IsVNCheckedBound(op1VN) && vnStore->IsVNInt32Constant(op2VN))
        {
            assert(relop->OperIs(GT_EQ, GT_NE));
            return optCreateJtrueAssertions(op1, op2, assertionKind);
        }
    }

    // Check op1 and op2 for an indirection of a GT_LCL_VAR and keep it in op1.
    if ((!op1->OperIs(GT_IND) || !op1->AsOp()->gtOp1->OperIs(GT_LCL_VAR)) &&
        (op2->OperIs(GT_IND) && op2->AsOp()->gtOp1->OperIs(GT_LCL_VAR)))
    {
        std::swap(op1, op2);
    }
    // If op1 is ind, then extract op1's oper.
    if (op1->OperIs(GT_IND) && op1->AsOp()->gtOp1->OperIs(GT_LCL_VAR))
    {
        return optCreateJtrueAssertions(op1, op2, assertionKind);
    }

    // Look for a call to an IsInstanceOf helper compared to a nullptr
    if (!op2->OperIs(GT_CNS_INT) && op1->OperIs(GT_CNS_INT))
    {
        std::swap(op1, op2);
    }
    // Validate op1 and op2
    if (!op1->OperIs(GT_CALL) || !op1->AsCall()->IsHelperCall() || !op1->TypeIs(TYP_REF) || // op1
        !op2->OperIs(GT_CNS_INT) || (op2->AsIntCon()->gtIconVal != 0))                      // op2
    {
        return NO_ASSERTION_INDEX;
    }

    if (optLocalAssertionProp)
    {
        // O1K_SUBTYPE is Global Assertion Prop only
        return NO_ASSERTION_INDEX;
    }

    GenTreeCall* const call = op1->AsCall();

    // Note CORINFO_HELP_READYTORUN_ISINSTANCEOF does not have the same argument pattern.
    // In particular, it is not possible to deduce what class is being tested from its args.
    //
    // Also note The CASTCLASS helpers won't appear in predicates as they throw on failure.
    // So the helper list here is smaller than the one in optAssertionProp_Call.
    //
    if ((call->gtCallMethHnd == eeFindHelper(CORINFO_HELP_ISINSTANCEOFINTERFACE)) ||
        (call->gtCallMethHnd == eeFindHelper(CORINFO_HELP_ISINSTANCEOFARRAY)) ||
        (call->gtCallMethHnd == eeFindHelper(CORINFO_HELP_ISINSTANCEOFCLASS)) ||
        (call->gtCallMethHnd == eeFindHelper(CORINFO_HELP_ISINSTANCEOFANY)))
    {
        GenTree* objectNode      = call->gtArgs.GetUserArgByIndex(1)->GetNode();
        GenTree* methodTableNode = call->gtArgs.GetUserArgByIndex(0)->GetNode();

        // objectNode can be TYP_I_IMPL in case if it's a constant handle
        // (e.g. a string literal from frozen segments)
        //
        assert(objectNode->TypeIs(TYP_REF, TYP_I_IMPL));
        assert(methodTableNode->TypeIs(TYP_I_IMPL));

        ValueNum objVN     = optConservativeNormalVN(objectNode);
        ValueNum typeHndVN = optConservativeNormalVN(methodTableNode);

        if ((objVN != ValueNumStore::NoVN) && vnStore->IsVNTypeHandle(typeHndVN))
        {
            AssertionDsc assertion;
            assertion.op1.kind       = O1K_SUBTYPE;
            assertion.op1.vn         = objVN;
            assertion.op2.kind       = O2K_CONST_INT;
            assertion.op2.u1.iconVal = vnStore->CoercedConstantValue<ssize_t>(typeHndVN);
            assertion.op2.vn         = typeHndVN;
            assertion.op2.SetIconFlag(GTF_ICON_CLASS_HDL);
            assertion.assertionKind = OAK_EQUAL;
            AssertionIndex index    = optAddAssertion(&assertion);

            // We don't need to create a complementary assertion here. We're only interested
            // in the assertion that the object is of a certain type. The opposite assertion
            // (that the object is not of a certain type) is not useful (at least not yet).
            //
            // So if we have "if (ISINST(obj, pMT) == null) then create the assertion for the "else" edge.
            //
            if (relop->OperIs(GT_EQ))
            {
                return AssertionInfo::ForNextEdge(index);
            }
            return index;
        }
    }

    return NO_ASSERTION_INDEX;
}

/*****************************************************************************
 *
 *  If this node creates an assertion then assign an index to the assertion
 *  by adding it to the lookup table, if necessary.
 */
void Compiler::optAssertionGen(GenTree* tree)
{
    tree->ClearAssertion();

    // If there are QMARKs in the IR, we won't generate assertions
    // for conditionally executed code.
    //
    if (optLocalAssertionProp && ((tree->gtFlags & GTF_COLON_COND) != 0))
    {
        return;
    }

#ifdef DEBUG
    optAssertionPropCurrentTree = tree;
#endif

    AssertionInfo assertionInfo;
    switch (tree->OperGet())
    {
        case GT_STORE_LCL_VAR:
            // VN takes care of non local assertions for data flow.
            if (optLocalAssertionProp)
            {
                assertionInfo = optCreateAssertion(tree, tree->AsLclVar()->Data(), OAK_EQUAL);
            }
            break;

        case GT_IND:
        case GT_XAND:
        case GT_XORR:
        case GT_XADD:
        case GT_XCHG:
        case GT_CMPXCHG:
        case GT_BLK:
        case GT_STOREIND:
        case GT_STORE_BLK:
        case GT_NULLCHECK:
        case GT_ARR_LENGTH:
        case GT_MDARR_LENGTH:
        case GT_MDARR_LOWER_BOUND:
            // These indirs (esp. GT_IND and GT_STOREIND) are the most popular sources of assertions.
            if (tree->IndirMayFault(this))
            {
                assertionInfo = optCreateAssertion(tree->GetIndirOrArrMetaDataAddr(), nullptr, OAK_NOT_EQUAL);
            }
            break;

        case GT_INTRINSIC:
            if (tree->AsIntrinsic()->gtIntrinsicName == NI_System_Object_GetType)
            {
                assertionInfo = optCreateAssertion(tree->AsIntrinsic()->gtGetOp1(), nullptr, OAK_NOT_EQUAL);
            }
            break;

        case GT_BOUNDS_CHECK:
            if (!optLocalAssertionProp)
            {
                assertionInfo = optCreateAssertion(tree, nullptr, OAK_NO_THROW);
            }
            break;

        case GT_ARR_ELEM:
            // An array element reference can create a non-null assertion
            assertionInfo = optCreateAssertion(tree->AsArrElem()->gtArrObj, nullptr, OAK_NOT_EQUAL);
            break;

        case GT_CALL:
        {
            // A virtual call can create a non-null assertion. We transform some virtual calls into non-virtual calls
            // with a GTF_CALL_NULLCHECK flag set.
            // Ignore tail calls because they have 'this` pointer in the regular arg list and an implicit null check.
            GenTreeCall* const call = tree->AsCall();
            if (call->NeedsNullCheck() || (call->IsVirtual() && !call->IsTailCall()))
            {
                //  Retrieve the 'this' arg.
                GenTree* thisArg = call->gtArgs.GetThisArg()->GetNode();
                assert(thisArg != nullptr);
                assertionInfo = optCreateAssertion(thisArg, nullptr, OAK_NOT_EQUAL);
            }
        }
        break;

        case GT_JTRUE:
            assertionInfo = optAssertionGenJtrue(tree);
            break;

        default:
            // All other gtOper node kinds, leave 'assertionIndex' = NO_ASSERTION_INDEX
            break;
    }

    if (assertionInfo.HasAssertion())
    {
        tree->SetAssertionInfo(assertionInfo);
    }
}

/*****************************************************************************
 *
 * Maps a complementary assertion to its original assertion so it can be
 * retrieved faster.
 */
void Compiler::optMapComplementary(AssertionIndex assertionIndex, AssertionIndex index)
{
    if (assertionIndex == NO_ASSERTION_INDEX || index == NO_ASSERTION_INDEX)
    {
        return;
    }

    assert(assertionIndex <= optMaxAssertionCount);
    assert(index <= optMaxAssertionCount);

    optComplementaryAssertionMap[assertionIndex] = index;
    optComplementaryAssertionMap[index]          = assertionIndex;
}

/*****************************************************************************
 *
 *  Given an assertion index, return the assertion index of the complementary
 *  assertion or 0 if one does not exist.
 */
AssertionIndex Compiler::optFindComplementary(AssertionIndex assertIndex)
{
    if (assertIndex == NO_ASSERTION_INDEX)
    {
        return NO_ASSERTION_INDEX;
    }
    AssertionDsc* inputAssertion = optGetAssertion(assertIndex);

    // Must be an equal or not equal assertion.
    if (inputAssertion->assertionKind != OAK_EQUAL && inputAssertion->assertionKind != OAK_NOT_EQUAL)
    {
        return NO_ASSERTION_INDEX;
    }

    AssertionIndex index = optComplementaryAssertionMap[assertIndex];
    if (index != NO_ASSERTION_INDEX && index <= optAssertionCount)
    {
        return index;
    }

    for (AssertionIndex index = 1; index <= optAssertionCount; ++index)
    {
        // Make sure assertion kinds are complementary and op1, op2 kinds match.
        AssertionDsc* curAssertion = optGetAssertion(index);
        if (curAssertion->Complementary(inputAssertion, !optLocalAssertionProp))
        {
            optMapComplementary(assertIndex, index);
            return index;
        }
    }
    return NO_ASSERTION_INDEX;
}

//------------------------------------------------------------------------
// optAssertionIsSubrange: Find a subrange assertion for the given range and tree.
//
// This function will return the index of the first assertion in "assertions"
// which claims that the value of "tree" is within the bounds of the provided
// "range" (i. e. "range.Contains(assertedRange)").
//
// Arguments:
//    tree       - the tree for which to find the assertion
//    range      - range the subrange of which to look for
//    assertions - the set of assertions
//
// Return Value:
//    Index of the found assertion, NO_ASSERTION_INDEX otherwise.
//
AssertionIndex Compiler::optAssertionIsSubrange(GenTree* tree, IntegralRange range, ASSERT_VALARG_TP assertions)
{
    if (!optCanPropSubRange)
    {
        // (don't early out in checked, verify above)
        return NO_ASSERTION_INDEX;
    }

    BitVecOps::Iter iter(apTraits, assertions);
    unsigned        bvIndex = 0;
    while (iter.NextElem(&bvIndex))
    {
        AssertionIndex const index        = GetAssertionIndex(bvIndex);
        AssertionDsc* const  curAssertion = optGetAssertion(index);
        if (curAssertion->CanPropSubRange())
        {
            // For local assertion prop use comparison on locals, and use comparison on vns for global prop.
            bool isEqual = optLocalAssertionProp
                               ? (curAssertion->op1.lclNum == tree->AsLclVarCommon()->GetLclNum())
                               : (curAssertion->op1.vn == vnStore->VNConservativeNormalValue(tree->gtVNPair));
            if (!isEqual)
            {
                continue;
            }

            if (range.Contains(curAssertion->op2.u2))
            {
                return index;
            }
        }
    }

    return NO_ASSERTION_INDEX;
}

/**********************************************************************************
 *
 * Given a "tree" that is usually arg1 of a isinst/cast kind of GT_CALL (a class
 * handle), and "methodTableArg" which is a const int (a class handle), then search
 * if there is an assertion in "assertions", that asserts the equality of the two
 * class handles and then returns the index of the assertion. If one such assertion
 * could not be found, then it returns NO_ASSERTION_INDEX.
 *
 */
AssertionIndex Compiler::optAssertionIsSubtype(GenTree* tree, GenTree* methodTableArg, ASSERT_VALARG_TP assertions)
{
    BitVecOps::Iter iter(apTraits, assertions);
    unsigned        bvIndex = 0;
    while (iter.NextElem(&bvIndex))
    {
        AssertionIndex const index        = GetAssertionIndex(bvIndex);
        AssertionDsc*        curAssertion = optGetAssertion(index);
        if ((curAssertion->assertionKind != OAK_EQUAL) ||
            ((curAssertion->op1.kind != O1K_SUBTYPE) && (curAssertion->op1.kind != O1K_EXACT_TYPE)))
        {
            // TODO-CQ: We might benefit from OAK_NOT_EQUAL assertion as well, e.g.:
            // if (obj is not MyClass) // obj is known to be never of MyClass class
            // {
            //     if (obj is MyClass) // can be folded to false
            //     {
            //
            continue;
        }

        if ((curAssertion->op1.vn != vnStore->VNConservativeNormalValue(tree->gtVNPair) ||
             (curAssertion->op2.kind != O2K_CONST_INT)))
        {
            continue;
        }

        ssize_t      methodTableVal = 0;
        GenTreeFlags iconFlags      = GTF_EMPTY;
        if (!optIsTreeKnownIntValue(!optLocalAssertionProp, methodTableArg, &methodTableVal, &iconFlags))
        {
            continue;
        }

        if (curAssertion->op2.u1.iconVal == methodTableVal)
        {
            // TODO-CQ: if they don't match, we might still be able to prove that the result is foldable via
            // compareTypesForCast.
            return index;
        }
    }
    return NO_ASSERTION_INDEX;
}

//------------------------------------------------------------------------------
// optVNBasedFoldExpr_Call_Memset: Unrolls NI_System_SpanHelpers_Fill for constant length.
//
// Arguments:
//    call - NI_System_SpanHelpers_Fill call to unroll
//
// Return Value:
//    Returns a new tree or nullptr if nothing is changed.
//
GenTree* Compiler::optVNBasedFoldExpr_Call_Memset(GenTreeCall* call)
{
    assert(call->IsSpecialIntrinsic(this, NI_System_SpanHelpers_Fill));

    CallArg* dstArg = call->gtArgs.GetUserArgByIndex(0);
    CallArg* lenArg = call->gtArgs.GetUserArgByIndex(1);
    CallArg* valArg = call->gtArgs.GetUserArgByIndex(2);

    var_types valType     = valArg->GetSignatureType();
    unsigned  lengthScale = genTypeSize(valType);

    if (lengthScale == 1)
    {
        // Lower expands it slightly better.
        JITDUMP("...value's type is byte - leave it for lower to expand.\n");
        return nullptr;
    }

    if (varTypeIsStruct(valType) || varTypeIsGC(valType))
    {
        JITDUMP("...value's type is not supported - bail out.\n");
        return nullptr;
    }

    ValueNum lenVN = vnStore->VNConservativeNormalValue(lenArg->GetNode()->gtVNPair);
    if (!vnStore->IsVNConstant(lenVN))
    {
        JITDUMP("...length is not a constant - bail out.\n");
        return nullptr;
    }

    size_t len = vnStore->CoercedConstantValue<size_t>(lenVN);
    if ((len > getUnrollThreshold(Memset)) ||
        // The first condition prevents the overflow in the second condition.
        // since both len and lengthScale are expected to be small at this point.
        (len * lengthScale) > getUnrollThreshold(Memset))
    {
        JITDUMP("...length is too big to unroll - bail out.\n");
        return nullptr;
    }

    // Some arbitrary threshold if the value is not a constant,
    // since it is unlikely that we can optimize it further.
    if (!valArg->GetNode()->OperIsConst() && (len >= 8))
    {
        JITDUMP("...length is too big to unroll for non-constant value - bail out.\n");
        return nullptr;
    }

    // Spill the side effects directly in the args, we're going to
    // pick them up in the following gtExtractSideEffList
    GenTree* dst = fgMakeMultiUse(&dstArg->NodeRef());
    GenTree* val = fgMakeMultiUse(&valArg->NodeRef());

    GenTree* result = nullptr;
    gtExtractSideEffList(call, &result, GTF_ALL_EFFECT, true);

    for (size_t offset = 0; offset < len; offset++)
    {
        // Clone dst and add offset if necessary.
        GenTree*         offsetNode = gtNewIconNode((ssize_t)(offset * lengthScale), TYP_I_IMPL);
        GenTree*         currDst    = gtNewOperNode(GT_ADD, dst->TypeGet(), gtCloneExpr(dst), offsetNode);
        GenTreeStoreInd* storeInd =
            gtNewStoreIndNode(valType, currDst, gtCloneExpr(val), GTF_IND_UNALIGNED | GTF_IND_ALLOW_NON_ATOMIC);

        // Merge with the previous result.
        result = result == nullptr ? storeInd : gtNewOperNode(GT_COMMA, TYP_VOID, result, storeInd);
    }

    JITDUMP("...optimized into STOREIND(s):\n");
    DISPTREE(result);
    return result;
}

//------------------------------------------------------------------------------
// optVNBasedFoldExpr_Call_Memmove: Unrolls NI_System_SpanHelpers_Memmove/CORINFO_HELP_MEMCPY
//    if possible. This function effectively duplicates LowerCallMemmove.
//    However, unlike LowerCallMemmove, it is able to optimize src into constants with help of VN.
//
// Arguments:
//    call - NI_System_SpanHelpers_Memmove/CORINFO_HELP_MEMCPY call to unroll
//
// Return Value:
//    Returns a new tree or nullptr if nothing is changed.
//
GenTree* Compiler::optVNBasedFoldExpr_Call_Memmove(GenTreeCall* call)
{
    JITDUMP("See if we can optimize NI_System_SpanHelpers_Memmove with help of VN...\n")
    assert(call->IsSpecialIntrinsic(this, NI_System_SpanHelpers_Memmove) ||
           call->IsHelperCall(this, CORINFO_HELP_MEMCPY));

    CallArg* dstArg = call->gtArgs.GetUserArgByIndex(0);
    CallArg* srcArg = call->gtArgs.GetUserArgByIndex(1);
    CallArg* lenArg = call->gtArgs.GetUserArgByIndex(2);
    ValueNum lenVN  = vnStore->VNConservativeNormalValue(lenArg->GetNode()->gtVNPair);
    if (!vnStore->IsVNConstant(lenVN))
    {
        JITDUMP("...length is not a constant - bail out.\n");
        return nullptr;
    }

    size_t len = vnStore->CoercedConstantValue<size_t>(lenVN);
    if (len == 0)
    {
        // Memmove(dst, src, 0) -> no-op.
        // Memmove doesn't dereference src/dst pointers if length is 0.
        JITDUMP("...length is 0 -> optimize to no-op.\n");
        return gtWrapWithSideEffects(gtNewNothingNode(), call, GTF_ALL_EFFECT, true);
    }

    if (len > getUnrollThreshold(Memcpy))
    {
        JITDUMP("...length is too big to unroll - bail out.\n");
        return nullptr;
    }

    // if GetImmutableDataFromAddress returns true, it means that the src is a read-only constant.
    // Thus, dst and src do not overlap (if they do - it's an UB).
    uint8_t* buffer = new (this, CMK_AssertionProp) uint8_t[len];
    if (!GetImmutableDataFromAddress(srcArg->GetNode(), (int)len, buffer))
    {
        JITDUMP("...src is not a constant - fallback to LowerCallMemmove.\n");
        return nullptr;
    }

    // if dstArg is not simple, we replace the arg directly with a temp assignment and
    // continue using that temp - it allows us reliably extract all side effects.
    GenTree* dst = fgMakeMultiUse(&dstArg->NodeRef());

    // Now we're going to emit a chain of STOREIND via COMMA nodes.
    // the very first tree is expected to be side-effects from the original call (including all args)
    GenTree* result = nullptr;
    gtExtractSideEffList(call, &result, GTF_ALL_EFFECT, true);

    unsigned lenRemaining = (unsigned)len;
    while (lenRemaining > 0)
    {
        const ssize_t offset = (ssize_t)len - (ssize_t)lenRemaining;

        // Clone dst and add offset if necessary.
        GenTree* currDst = gtCloneExpr(dst);
        if (offset != 0)
        {
            currDst = gtNewOperNode(GT_ADD, dst->TypeGet(), currDst, gtNewIconNode(offset, TYP_I_IMPL));
        }

        // Create an unaligned STOREIND node using the largest possible word size.
        var_types        type     = roundDownMaxType(lenRemaining);
        GenTree*         srcCns   = gtNewGenericCon(type, buffer + offset);
        GenTreeStoreInd* storeInd = gtNewStoreIndNode(type, currDst, srcCns, GTF_IND_UNALIGNED);
        fgUpdateConstTreeValueNumber(srcCns);

        // Merge with the previous result.
        result = result == nullptr ? storeInd : gtNewOperNode(GT_COMMA, TYP_VOID, result, storeInd);

        lenRemaining -= genTypeSize(type);
    }

    JITDUMP("...optimized into STOREIND(s)!:\n");
    DISPTREE(result);
    return result;
}

//------------------------------------------------------------------------------
// optVNBasedFoldExpr_Call: Folds given call using VN to a simpler tree.
//
// Arguments:
//    block  -  The block containing the tree.
//    parent -  The parent node of the tree.
//    call   -  The call to fold
//
// Return Value:
//    Returns a new tree or nullptr if nothing is changed.
//
GenTree* Compiler::optVNBasedFoldExpr_Call(BasicBlock* block, GenTree* parent, GenTreeCall* call)
{
    switch (call->GetHelperNum())
    {
        case CORINFO_HELP_CHKCASTARRAY:
        case CORINFO_HELP_CHKCASTANY:
        case CORINFO_HELP_CHKCASTINTERFACE:
        case CORINFO_HELP_CHKCASTCLASS:
        case CORINFO_HELP_ISINSTANCEOFARRAY:
        case CORINFO_HELP_ISINSTANCEOFCLASS:
        case CORINFO_HELP_ISINSTANCEOFANY:
        case CORINFO_HELP_ISINSTANCEOFINTERFACE:
        {
            CallArg* castClsCallArg = call->gtArgs.GetUserArgByIndex(0);
            CallArg* castObjCallArg = call->gtArgs.GetUserArgByIndex(1);
            GenTree* castClsArg     = castClsCallArg->GetNode();
            GenTree* castObjArg     = castObjCallArg->GetNode();

            // If object has the same VN as the cast, then the cast is effectively a no-op.
            //
            if (castObjArg->gtVNPair == call->gtVNPair)
            {
                // if castObjArg is not simple, we replace the arg with a temp assignment and
                // continue using that temp - it allows us reliably extract all side effects
                castObjArg = fgMakeMultiUse(&castObjCallArg->NodeRef());
                return gtWrapWithSideEffects(castObjArg, call, GTF_ALL_EFFECT, true);
            }

            // Let's see if gtGetClassHandle may help us to fold the cast (since VNForCast did not).
            if (castClsArg->IsIconHandle(GTF_ICON_CLASS_HDL))
            {
                bool                 isExact;
                bool                 isNonNull;
                CORINFO_CLASS_HANDLE castFrom = gtGetClassHandle(castObjArg, &isExact, &isNonNull);
                if (castFrom != NO_CLASS_HANDLE)
                {
                    CORINFO_CLASS_HANDLE castTo = gtGetHelperArgClassHandle(castClsArg);
                    // Constant prop may fail to propagate compile time class handles, so verify we have
                    // a handle before invoking the runtime.
                    if ((castTo != NO_CLASS_HANDLE) &&
                        info.compCompHnd->compareTypesForCast(castFrom, castTo) == TypeCompareState::Must)
                    {
                        // if castObjArg is not simple, we replace the arg with a temp assignment and
                        // continue using that temp - it allows us reliably extract all side effects
                        castObjArg = fgMakeMultiUse(&castObjCallArg->NodeRef());
                        return gtWrapWithSideEffects(castObjArg, call, GTF_ALL_EFFECT, true);
                    }
                }
            }
        }
        break;

        default:
            break;
    }

    if (call->IsSpecialIntrinsic(this, NI_System_SpanHelpers_Memmove) || call->IsHelperCall(this, CORINFO_HELP_MEMCPY))
    {
        return optVNBasedFoldExpr_Call_Memmove(call);
    }

    if (call->IsSpecialIntrinsic(this, NI_System_SpanHelpers_Fill))
    {
        return optVNBasedFoldExpr_Call_Memset(call);
    }

    return nullptr;
}

//------------------------------------------------------------------------------
// optVNBasedFoldExpr: Folds given tree using VN to a constant or a simpler tree.
//
// Arguments:
//    block  -  The block containing the tree.
//    parent -  The parent node of the tree.
//    tree   -  The tree to fold.
//
// Return Value:
//    Returns a new tree or nullptr if nothing is changed.
//
GenTree* Compiler::optVNBasedFoldExpr(BasicBlock* block, GenTree* parent, GenTree* tree)
{
    // First, attempt to fold it to a constant if possible.
    GenTree* foldedToCns = optVNBasedFoldConstExpr(block, parent, tree);
    if (foldedToCns != nullptr)
    {
        return foldedToCns;
    }

    switch (tree->OperGet())
    {
        case GT_CALL:
            return optVNBasedFoldExpr_Call(block, parent, tree->AsCall());

            // We can add more VN-based foldings here.

        default:
            break;
    }
    return nullptr;
}

//------------------------------------------------------------------------------
// optVNBasedFoldConstExpr: Substitutes tree with an evaluated constant while
//                          managing side-effects.
//
// Arguments:
//    block  -  The block containing the tree.
//    parent -  The parent node of the tree.
//    tree   -  The tree node whose value is known at compile time.
//              The tree should have a constant value number.
//
// Return Value:
//    Returns a potentially new or a transformed tree node.
//    Returns nullptr when no transformation is possible.
//
// Description:
//    Transforms a tree node if its result evaluates to a constant. The
//    transformation can be a "ChangeOper" to a constant or a new constant node
//    with extracted side-effects.
//
//    Before replacing or substituting the "tree" with a constant, extracts any
//    side effects from the "tree" and creates a comma separated side effect list
//    and then appends the transformed node at the end of the list.
//    This comma separated list is then returned.
//
//    For JTrue nodes, side effects are not put into a comma separated list. If
//    the relop will evaluate to "true" or "false" statically, then the side-effects
//    will be put into new statements, presuming the JTrue will be folded away.
//
GenTree* Compiler::optVNBasedFoldConstExpr(BasicBlock* block, GenTree* parent, GenTree* tree)
{
    if (tree->OperIs(GT_JTRUE))
    {
        // Treat JTRUE separately to extract side effects into respective statements rather
        // than using a COMMA separated op1.
        return optVNConstantPropOnJTrue(block, tree);
    }
    // If relop is part of JTRUE, this should be optimized as part of the parent JTRUE.
    // Or if relop is part of QMARK or anything else, we simply bail here.
    else if (tree->OperIsCompare() && (tree->gtFlags & GTF_RELOP_JMP_USED))
    {
        return nullptr;
    }

    // We want to use the Normal ValueNumber when checking for constants.
    ValueNumPair vnPair = tree->gtVNPair;
    ValueNum     vnCns  = vnStore->VNConservativeNormalValue(vnPair);

    // Check if node evaluates to a constant
    if (!vnStore->IsVNConstant(vnCns))
    {
        // Last chance - propagate VNF_PtrToLoc(lcl, offset) as GT_LCL_ADDR node
        VNFuncApp funcApp;
        if (((tree->gtFlags & GTF_SIDE_EFFECT) == 0) && vnStore->GetVNFunc(vnCns, &funcApp) &&
            (funcApp.m_func == VNF_PtrToLoc))
        {
            unsigned lcl  = (unsigned)vnStore->CoercedConstantValue<size_t>(funcApp.m_args[0]);
            unsigned offs = (unsigned)vnStore->CoercedConstantValue<size_t>(funcApp.m_args[1]);
            return gtNewLclAddrNode(lcl, offs, tree->TypeGet());
        }

        return nullptr;
    }

    GenTree* conValTree = nullptr;
    switch (vnStore->TypeOfVN(vnCns))
    {
        case TYP_FLOAT:
        {
            float value = vnStore->ConstantValue<float>(vnCns);

            if (tree->TypeIs(TYP_INT))
            {
                // Same sized reinterpretation of bits to integer
                conValTree = gtNewIconNode(*(reinterpret_cast<int*>(&value)));
            }
            else
            {
                // Implicit conversion to float or double
                assert(varTypeIsFloating(tree->TypeGet()));
                conValTree = gtNewDconNode(FloatingPointUtils::convertToDouble(value), tree->TypeGet());
            }
            break;
        }

        case TYP_DOUBLE:
        {
            double value = vnStore->ConstantValue<double>(vnCns);

            if (tree->TypeIs(TYP_LONG))
            {
                conValTree = gtNewLconNode(*(reinterpret_cast<INT64*>(&value)));
            }
            else
            {
                // Implicit conversion to float or double
                assert(varTypeIsFloating(tree->TypeGet()));
                conValTree = gtNewDconNode(value, tree->TypeGet());
            }
            break;
        }

        case TYP_LONG:
        {
            INT64 value = vnStore->ConstantValue<INT64>(vnCns);

#ifdef TARGET_64BIT
            if (vnStore->IsVNHandle(vnCns))
            {
                // Don't perform constant folding that involves a handle that needs
                // to be recorded as a relocation with the VM.
                if (!opts.compReloc)
                {
                    conValTree = gtNewIconHandleNode(value, vnStore->GetHandleFlags(vnCns));
                }
            }
            else
#endif
            {
                switch (tree->TypeGet())
                {
                    case TYP_INT:
                        // Implicit conversion to smaller integer
                        conValTree = gtNewIconNode(static_cast<int>(value));
                        break;

                    case TYP_LONG:
                        // Same type no conversion required
                        conValTree = gtNewLconNode(value);
                        break;

                    case TYP_FLOAT:
                        // No implicit conversions from long to float and value numbering will
                        // not propagate through memory reinterpretations of different size.
                        unreached();
                        break;

                    case TYP_DOUBLE:
                        // Same sized reinterpretation of bits to double
                        conValTree = gtNewDconNodeD(*(reinterpret_cast<double*>(&value)));
                        break;

                    default:
                        // Do not support such optimization.
                        break;
                }
            }
        }
        break;

        case TYP_REF:
        {
            if (tree->TypeIs(TYP_REF))
            {
                const size_t value = vnStore->ConstantValue<size_t>(vnCns);
                if (value == 0)
                {
                    conValTree = gtNewNull();
                }
                else
                {
                    assert(vnStore->IsVNObjHandle(vnCns));
                    conValTree = gtNewIconHandleNode(value, GTF_ICON_OBJ_HDL);
                }
            }
        }
        break;

        case TYP_INT:
        {
            int value = vnStore->ConstantValue<int>(vnCns);
#ifndef TARGET_64BIT
            if (vnStore->IsVNHandle(vnCns))
            {
                // Don't perform constant folding that involves a handle that needs
                // to be recorded as a relocation with the VM.
                if (!opts.compReloc)
                {
                    conValTree = gtNewIconHandleNode(value, vnStore->GetHandleFlags(vnCns));
                }
            }
            else
#endif
            {
                switch (tree->TypeGet())
                {
                    case TYP_REF:
                    case TYP_INT:
                        // Same type no conversion required
                        conValTree = gtNewIconNode(value);
                        break;

                    case TYP_LONG:
                        // Implicit conversion to larger integer
                        conValTree = gtNewLconNode(value);
                        break;

                    case TYP_FLOAT:
                        // Same sized reinterpretation of bits to float
                        conValTree = gtNewDconNodeF(BitOperations::UInt32BitsToSingle((uint32_t)value));
                        break;

                    case TYP_DOUBLE:
                        // No implicit conversions from int to double and value numbering will
                        // not propagate through memory reinterpretations of different size.
                        unreached();
                        break;

                    case TYP_BYTE:
                    case TYP_UBYTE:
                    case TYP_SHORT:
                    case TYP_USHORT:
                        assert(FitsIn(tree->TypeGet(), value));
                        conValTree = gtNewIconNode(value);
                        break;

                    default:
                        // Do not support (e.g. byref(const int)).
                        break;
                }
            }
        }
        break;

#if defined(FEATURE_SIMD)
        case TYP_SIMD8:
        {
            simd8_t value = vnStore->ConstantValue<simd8_t>(vnCns);

            GenTreeVecCon* vecCon = gtNewVconNode(tree->TypeGet());
            memcpy(&vecCon->gtSimdVal, &value, sizeof(simd8_t));

            conValTree = vecCon;
            break;
        }

        case TYP_SIMD12:
        {
            simd12_t value = vnStore->ConstantValue<simd12_t>(vnCns);

            GenTreeVecCon* vecCon = gtNewVconNode(tree->TypeGet());
            memcpy(&vecCon->gtSimdVal, &value, sizeof(simd12_t));

            conValTree = vecCon;
            break;
        }

        case TYP_SIMD16:
        {
            simd16_t value = vnStore->ConstantValue<simd16_t>(vnCns);

            GenTreeVecCon* vecCon = gtNewVconNode(tree->TypeGet());
            memcpy(&vecCon->gtSimdVal, &value, sizeof(simd16_t));

            conValTree = vecCon;
            break;
        }

#if defined(TARGET_XARCH)
        case TYP_SIMD32:
        {
            simd32_t value = vnStore->ConstantValue<simd32_t>(vnCns);

            GenTreeVecCon* vecCon = gtNewVconNode(tree->TypeGet());
            memcpy(&vecCon->gtSimdVal, &value, sizeof(simd32_t));

            conValTree = vecCon;
            break;
        }

        case TYP_SIMD64:
        {
            simd64_t value = vnStore->ConstantValue<simd64_t>(vnCns);

            GenTreeVecCon* vecCon = gtNewVconNode(tree->TypeGet());
            memcpy(&vecCon->gtSimdVal, &value, sizeof(simd64_t));

            conValTree = vecCon;
            break;
        }
        break;

#endif // TARGET_XARCH
#endif // FEATURE_SIMD

#if defined(FEATURE_MASKED_HW_INTRINSICS)
        case TYP_MASK:
        {
            simdmask_t value = vnStore->ConstantValue<simdmask_t>(vnCns);

            GenTreeMskCon* mskCon = gtNewMskConNode(tree->TypeGet());
            memcpy(&mskCon->gtSimdMaskVal, &value, sizeof(simdmask_t));

            conValTree = mskCon;
            break;
        }
        break;
#endif // FEATURE_MASKED_HW_INTRINSICS

        case TYP_BYREF:
            // Do not support const byref optimization.
            break;

        default:
            // We do not record constants of other types.
            unreached();
            break;
    }

    if (conValTree != nullptr)
    {
        if (!optIsProfitableToSubstitute(tree, block, parent, conValTree))
        {
            // Not profitable to substitute
            return nullptr;
        }

        // Were able to optimize.
        conValTree->gtVNPair = vnPair;
        return gtWrapWithSideEffects(conValTree, tree, GTF_SIDE_EFFECT, true);
    }
    else
    {
        // Was not able to optimize.
        return nullptr;
    }
}

//------------------------------------------------------------------------------
// optIsProfitableToSubstitute: Checks if value worth substituting to dest
//
// Arguments:
//    dest       - destination to substitute value to
//    destBlock  - Basic block of destination
//    destParent - Parent of destination
//    value      - value we plan to substitute
//
// Returns:
//    False if it's likely not profitable to do substitution, True otherwise
//
bool Compiler::optIsProfitableToSubstitute(GenTree* dest, BasicBlock* destBlock, GenTree* destParent, GenTree* value)
{
    // Giving up on these kinds of handles demonstrated size improvements
    if (value->IsIconHandle(GTF_ICON_STATIC_HDL, GTF_ICON_CLASS_HDL))
    {
        return false;
    }

    // A simple heuristic: If the constant is defined outside of a loop (not far from its head)
    // and is used inside it - don't propagate.
    //
    // TODO: Extend on more kinds of trees

    if (!dest->OperIs(GT_LCL_VAR))
    {
        return true;
    }

    const GenTreeLclVar* lcl = dest->AsLclVar();

    if (value->IsCnsVec())
    {
#if defined(FEATURE_HW_INTRINSICS)
        // Many hwintrinsics can't benefit from constant prop because they don't support
        // constant folding nor do they support any specialized encodings. So, we want to
        // skip constant prop and preserve any user-defined locals in that scenario.
        //
        // However, if the local is only referenced once then we want to allow propagation
        // regardless since we can then contain the only actual usage and save a needless
        // instruction.
        //
        // To determine number of uses, we prefer checking SSA first since it is more exact
        // and can account for patterns where a local is reassigned later. However, if we
        // can't find an SSA then we fallback to the naive ref count of the local, noting
        // that we need to check for greater than 2 since it includes both the def and use.

        bool inspectIntrinsic = false;

        if ((destParent != nullptr) && destParent->OperIsHWIntrinsic())
        {
            LclVarDsc* varDsc = lvaGetDesc(lcl);

            if (lcl->HasSsaName())
            {
                inspectIntrinsic = varDsc->GetPerSsaData(lcl->GetSsaNum())->GetNumUses() > 1;
            }
            else
            {
                inspectIntrinsic = varDsc->lvRefCnt() > 2;
            }
        }

        if (inspectIntrinsic)
        {
            GenTreeHWIntrinsic* parent      = destParent->AsHWIntrinsic();
            NamedIntrinsic      intrinsicId = parent->GetHWIntrinsicId();

            if (!HWIntrinsicInfo::CanBenefitFromConstantProp(intrinsicId))
            {
                return false;
            }

            // For several of the scenarios we may skip the costing logic
            // since we know that the operand is always containable and therefore
            // is always cost effective to propagate.

            return parent->ShouldConstantProp(dest, value->AsVecCon());
        }
#endif // FEATURE_HW_INTRINSICS
    }
    else if (!value->IsCnsFltOrDbl() && !value->IsCnsMsk())
    {
        return true;
    }

    gtPrepareCost(value);

    if ((value->GetCostEx() > 1) && (value->GetCostSz() > 1))
    {
        // Try to find the block this constant was originally defined in
        if (lcl->HasSsaName())
        {
            BasicBlock* defBlock = lvaGetDesc(lcl)->GetPerSsaData(lcl->GetSsaNum())->GetBlock();
            if (defBlock != nullptr)
            {
                // Avoid propagating if the weighted use cost is significantly greater than the def cost.
                // NOTE: this currently does not take "a float living across a call" case into account
                // where we might end up with spill/restore on ABIs without callee-saved registers
                const weight_t defBlockWeight = defBlock->getBBWeight(this);
                const weight_t lclblockWeight = destBlock->getBBWeight(this);

                if ((defBlockWeight > 0) && ((lclblockWeight / defBlockWeight) >= BB_LOOP_WEIGHT_SCALE))
                {
                    JITDUMP("Constant propagation inside loop " FMT_BB " is not profitable\n", destBlock->bbNum);
                    return false;
                }
            }
        }
    }
    return true;
}

//------------------------------------------------------------------------------
// optConstantAssertionProp: Possibly substitute a constant for a local use
//
// Arguments:
//    curAssertion - assertion to propagate
//    tree         - tree to possibly modify
//    stmt         - statement containing the tree
//    index        - index of this assertion in the assertion table
//
// Returns:
//    Updated tree (may be the input tree, modified in place), or nullptr
//
// Notes:
//    stmt may be nullptr during local assertion prop
//
GenTree* Compiler::optConstantAssertionProp(AssertionDsc*        curAssertion,
                                            GenTreeLclVarCommon* tree,
                                            Statement* stmt      DEBUGARG(AssertionIndex index))
{
    const unsigned lclNum = tree->GetLclNum();

    if (lclNumIsCSE(lclNum))
    {
        // Ignore the CSE flag in Global Assertion Prop for checked bound as those usually
        // unlock more opportunities for BCE.
        if (optLocalAssertionProp || !vnStore->IsVNCheckedBound(optConservativeNormalVN(tree)))
        {
            return nullptr;
        }
    }

    GenTree* newTree = tree;

    // Update 'newTree' with the new value from our table
    // Typically newTree == tree and we are updating the node in place
    switch (curAssertion->op2.kind)
    {
        case O2K_CONST_DOUBLE:
            // There could be a positive zero and a negative zero, so don't propagate zeroes.
            if (curAssertion->op2.dconVal == 0.0)
            {
                return nullptr;
            }
            newTree->BashToConst(curAssertion->op2.dconVal, tree->TypeGet());
            break;

        case O2K_CONST_INT:

            // Don't propagate non-nulll non-static handles if we need to report relocs.
            if (opts.compReloc && curAssertion->op2.HasIconFlag() && (curAssertion->op2.u1.iconVal != 0))
            {
                if (curAssertion->op2.GetIconFlag() != GTF_ICON_STATIC_HDL)
                {
                    return nullptr;
                }
            }

            // We assume that we do not try to do assertion prop on mismatched
            // accesses (note that we widen normalize-on-load local accesses
            // and insert casts in morph, which would be problematic to track
            // here).
            assert(tree->TypeGet() == lvaGetDesc(lclNum)->TypeGet());

            if (curAssertion->op2.HasIconFlag())
            {
                // Here we have to allocate a new 'large' node to replace the old one
                newTree = gtNewIconHandleNode(curAssertion->op2.u1.iconVal, curAssertion->op2.GetIconFlag(),
                                              curAssertion->op2.u1.fieldSeq);

                // Make sure we don't retype const gc handles to TYP_I_IMPL
                // Although, it's possible for e.g. GTF_ICON_STATIC_HDL

                if (!newTree->IsIntegralConst(0) && newTree->IsIconHandle(GTF_ICON_OBJ_HDL) && !tree->TypeIs(TYP_REF))
                {
                    // If the tree is not a TYP_REF, we should not propagate an ICON TYP_REF
                    // into it, as it may lead to incorrect code generation.
                    return nullptr;
                }

                newTree->ChangeType(tree->TypeGet());
            }
            else
            {
                assert(varTypeIsIntegralOrI(tree));
                newTree->BashToConst(curAssertion->op2.u1.iconVal, genActualType(tree));
            }
            break;

        default:
            return nullptr;
    }

    if (!optLocalAssertionProp)
    {
        assert(newTree->OperIsConst());                      // We should have a simple Constant node for newTree
        assert(vnStore->IsVNConstant(curAssertion->op2.vn)); // The value number stored for op2 should be a valid
                                                             // VN representing the constant
        newTree->gtVNPair.SetBoth(curAssertion->op2.vn);     // Set the ValueNumPair to the constant VN from op2
                                                             // of the assertion
    }

#ifdef DEBUG
    if (verbose)
    {
        printf("\nAssertion prop in " FMT_BB ":\n", compCurBB->bbNum);
        optPrintAssertion(curAssertion, index);
        gtDispTree(newTree, nullptr, nullptr, true);
    }
#endif

    return optAssertionProp_Update(newTree, tree, stmt);
}

//------------------------------------------------------------------------------
// optZeroObjAssertionProp: Find and propagate a ZEROOBJ assertion for the given tree.
//
// Arguments:
//    assertions - set of live assertions
//    tree       - the tree to possibly replace, in-place, with a zero
//
// Returns:
//    Whether propagation took place.
//
// Notes:
//    Because not all users of struct nodes support "zero" operands, instead of
//    propagating ZEROOBJ on locals, we propagate it on their parents.
//
bool Compiler::optZeroObjAssertionProp(GenTree* tree, ASSERT_VALARG_TP assertions)
{
    // We only make ZEROOBJ assertions in local propagation.
    if (!optLocalAssertionProp)
    {
        return false;
    }

    // And only into local nodes
    if (!tree->OperIsLocal())
    {
        return false;
    }

    // No ZEROOBJ assertions for simd.
    //
    if (varTypeIsSIMD(tree))
    {
        return false;
    }

    LclVarDsc* const lclVarDsc = lvaGetDesc(tree->AsLclVarCommon());

    if (lclVarDsc->IsAddressExposed())
    {
        return false;
    }

    const unsigned lclNum         = tree->AsLclVarCommon()->GetLclNum();
    AssertionIndex assertionIndex = optLocalAssertionIsEqualOrNotEqual(O1K_LCLVAR, lclNum, O2K_ZEROOBJ, 0, assertions);
    if (assertionIndex == NO_ASSERTION_INDEX)
    {
        return false;
    }

    AssertionDsc* assertion = optGetAssertion(assertionIndex);
    JITDUMP("\nAssertion prop in " FMT_BB ":\n", compCurBB->bbNum);
    JITDUMPEXEC(optPrintAssertion(assertion, assertionIndex));
    DISPNODE(tree);

    tree->BashToZeroConst(TYP_INT);

    JITDUMP(" =>\n");
    DISPNODE(tree);

    return true;
}

//------------------------------------------------------------------------------
// optAssertionProp_LclVarTypeCheck: verify compatible types for copy prop
//
// Arguments:
//    tree         - tree to possibly modify
//    lclVarDsc    - local accessed by tree
//    copyVarDsc   - local to possibly copy prop into tree
//
// Returns:
//    True if copy prop is safe.
//
// Notes:
//    Before substituting copyVar for lclVar, make sure using copyVar doesn't widen access.
//
bool Compiler::optAssertionProp_LclVarTypeCheck(GenTree* tree, LclVarDsc* lclVarDsc, LclVarDsc* copyVarDsc)
{
    /*
        Small struct field locals are stored using the exact width and loaded widened
        (i.e. lvNormalizeOnStore==false   lvNormalizeOnLoad==true),
        because the field locals might end up embedded in the parent struct local with the exact width.

            In other words, a store to a short field local should always done using an exact width store

                [00254538] 0x0009 ------------               const     int    0x1234
            [002545B8] 0x000B -A--G--NR---               =         short
                [00254570] 0x000A D------N----               lclVar    short  V43 tmp40

            mov   word  ptr [L_043], 0x1234

        Now, if we copy prop, say a short field local V43, to another short local V34
        for the following tree:

                [04E18650] 0x0001 ------------               lclVar    int   V34 tmp31
            [04E19714] 0x0002 -A----------               =         int
                [04E196DC] 0x0001 D------N----               lclVar    int   V36 tmp33

        We will end with this tree:

                [04E18650] 0x0001 ------------               lclVar    int   V43 tmp40
            [04E19714] 0x0002 -A-----NR---               =         int
                [04E196DC] 0x0001 D------N----               lclVar    int   V36 tmp33    EAX

        And eventually causing a fetch of 4-byte out from [L_043] :(
            mov     EAX, dword ptr [L_043]

        The following check is to make sure we only perform the copy prop
        when we don't retrieve the wider value.
    */

    if (copyVarDsc->lvIsStructField)
    {
        var_types varType = (var_types)copyVarDsc->lvType;
        // Make sure we don't retrieve the wider value.
        return !varTypeIsSmall(varType) || (varType == tree->TypeGet());
    }
    // Called in the context of a single copy assertion, so the types should have been
    // taken care by the assertion gen logic for other cases. Just return true.
    return true;
}

//------------------------------------------------------------------------
// optCopyAssertionProp: copy prop use of one local with another
//
// Arguments:
//    curAssertion - assertion triggering the possible copy
//    tree         - tree use to consider replacing
//    stmt         - statement containing the tree
//    index        - index of the assertion
//
// Returns:
//    Updated tree, or nullptr
//
// Notes:
//    stmt may be nullptr during local assertion prop
//
GenTree* Compiler::optCopyAssertionProp(AssertionDsc*        curAssertion,
                                        GenTreeLclVarCommon* tree,
                                        Statement* stmt      DEBUGARG(AssertionIndex index))
{
    assert(optLocalAssertionProp);

    const AssertionDsc::AssertionDscOp1& op1 = curAssertion->op1;
    const AssertionDsc::AssertionDscOp2& op2 = curAssertion->op2;

    noway_assert(op1.lclNum != op2.lclNum);

    const unsigned lclNum = tree->GetLclNum();

    // Make sure one of the lclNum of the assertion matches with that of the tree.
    if (op1.lclNum != lclNum && op2.lclNum != lclNum)
    {
        return nullptr;
    }

    // Extract the matching lclNum and ssaNum, as well as the field sequence.
    unsigned copyLclNum;
    if (op1.lclNum == lclNum)
    {
        copyLclNum = op2.lclNum;
    }
    else
    {
        copyLclNum = op1.lclNum;
    }

    LclVarDsc* const copyVarDsc = lvaGetDesc(copyLclNum);
    LclVarDsc* const lclVarDsc  = lvaGetDesc(lclNum);

    // Make sure the types are compatible.
    if (!optAssertionProp_LclVarTypeCheck(tree, lclVarDsc, copyVarDsc))
    {
        return nullptr;
    }

    // Make sure we can perform this copy prop.
    if (optCopyProp_LclVarScore(lclVarDsc, copyVarDsc, curAssertion->op1.lclNum == lclNum) <= 0)
    {
        return nullptr;
    }

    // Heuristic: for LclFld prop, don't force the copy or its promoted fields to be in memory.
    //
    if (tree->OperIs(GT_LCL_FLD))
    {
        if (copyVarDsc->IsEnregisterableLcl() || copyVarDsc->lvPromoted)
        {
            return nullptr;
        }
        else
        {
            lvaSetVarDoNotEnregister(copyLclNum DEBUGARG(DoNotEnregisterReason::LocalField));
        }
    }

    tree->SetLclNum(copyLclNum);

    // Copy prop and last-use copy elision happens at the same time in morph.
    // This node may potentially not be a last use of the new local.
    //
    // TODO-CQ: It is probably better to avoid doing this propagation if we
    // would otherwise omit an implicit byref copy since this propagation will
    // force us to create another copy anyway.
    //
    tree->gtFlags &= ~GTF_VAR_DEATH;

#ifdef DEBUG
    if (verbose)
    {
        printf("\nAssertion prop in " FMT_BB ":\n", compCurBB->bbNum);
        optPrintAssertion(curAssertion, index);
        DISPNODE(tree);
    }
#endif

    // Update and morph the tree.
    return optAssertionProp_Update(tree, tree, stmt);
}

//------------------------------------------------------------------------
// optAssertionProp_LclVar: try and optimize a local var use via assertions
//
// Arguments:
//    assertions - set of live assertions
//    tree       - local use to optimize
//    stmt       - statement containing the tree
//
// Returns:
//    Updated tree, or nullptr
//
// Notes:
//   stmt may be nullptr during local assertion prop
//
GenTree* Compiler::optAssertionProp_LclVar(ASSERT_VALARG_TP assertions, GenTreeLclVarCommon* tree, Statement* stmt)
{
    // If we have a var definition then bail or
    // If this is the address of the var then it will have the GTF_DONT_CSE
    // flag set and we don't want to assertion prop on it.
    // TODO-ASG: delete.
    if (tree->gtFlags & (GTF_VAR_DEF | GTF_DONT_CSE))
    {
        return nullptr;
    }

    // There are no constant assertions for structs in global propagation.
    //
    if ((!optLocalAssertionProp && varTypeIsStruct(tree)) || !optCanPropLclVar)
    {
        return nullptr;
    }

    // For local assertion prop we can filter the assertion set down.
    //
    const unsigned lclNum = tree->GetLclNum();

    ASSERT_TP filteredAssertions = assertions;
    if (optLocalAssertionProp)
    {
        filteredAssertions = BitVecOps::Intersection(apTraits, GetAssertionDep(lclNum), filteredAssertions);
    }

    BitVecOps::Iter iter(apTraits, filteredAssertions);
    unsigned        index = 0;
    while (iter.NextElem(&index))
    {
        AssertionIndex assertionIndex = GetAssertionIndex(index);
        if (assertionIndex > optAssertionCount)
        {
            break;
        }
        // See if the variable is equal to a constant or another variable.
        AssertionDsc* curAssertion = optGetAssertion(assertionIndex);
        if (!curAssertion->CanPropLclVar())
        {
            continue;
        }

        // Copy prop.
        if (curAssertion->op2.kind == O2K_LCLVAR_COPY)
        {
            // Cannot do copy prop during global assertion prop because of no knowledge
            // of kill sets. We will still make a == b copy assertions during the global phase to allow
            // for any implied assertions that can be retrieved. Because implied assertions look for
            // matching SSA numbers (i.e., if a0 == b1 and b1 == c0 then a0 == c0) they don't need kill sets.
            if (optLocalAssertionProp)
            {
                // Perform copy assertion prop.
                GenTree* newTree = optCopyAssertionProp(curAssertion, tree, stmt DEBUGARG(assertionIndex));
                if (newTree != nullptr)
                {
                    return newTree;
                }
            }

            continue;
        }

        // There are no constant assertions for structs.
        //
        if (varTypeIsStruct(tree))
        {
            continue;
        }

        // Verify types match
        if (tree->TypeGet() != lvaGetRealType(lclNum))
        {
            continue;
        }

        if (optLocalAssertionProp)
        {
            // Check lclNum in Local Assertion Prop
            if (curAssertion->op1.lclNum == lclNum)
            {
                return optConstantAssertionProp(curAssertion, tree, stmt DEBUGARG(assertionIndex));
            }
        }
        else
        {
            // Check VN in Global Assertion Prop
            if (curAssertion->op1.vn == vnStore->VNConservativeNormalValue(tree->gtVNPair))
            {
                return optConstantAssertionProp(curAssertion, tree, stmt DEBUGARG(assertionIndex));
            }
        }
    }

    return nullptr;
}

//------------------------------------------------------------------------
// optAssertionProp_LclFld: try and optimize a local field use via assertions
//
// Arguments:
//    assertions - set of live assertions
//    tree       - local field use to optimize
//    stmt       - statement containing the tree
//
// Returns:
//    Updated tree, or nullptr
//
// Notes:
//   stmt may be nullptr during local assertion prop
//
GenTree* Compiler::optAssertionProp_LclFld(ASSERT_VALARG_TP assertions, GenTreeLclVarCommon* tree, Statement* stmt)
{
    // If we have a var definition then bail or
    // If this is the address of the var then it will have the GTF_DONT_CSE
    // flag set and we don't want to assertion prop on it.
    // TODO-ASG: delete.
    if (tree->gtFlags & (GTF_VAR_DEF | GTF_DONT_CSE))
    {
        return nullptr;
    }

    // Only run during local prop and if copies are available.
    //
    if (!optLocalAssertionProp || !optCanPropLclVar)
    {
        return nullptr;
    }

    const unsigned lclNum             = tree->GetLclNum();
    ASSERT_TP      filteredAssertions = BitVecOps::Intersection(apTraits, GetAssertionDep(lclNum), assertions);

    BitVecOps::Iter iter(apTraits, filteredAssertions);
    unsigned        index = 0;
    while (iter.NextElem(&index))
    {
        AssertionIndex assertionIndex = GetAssertionIndex(index);
        if (assertionIndex > optAssertionCount)
        {
            break;
        }

        // See if the variable is equal to another variable.
        //
        AssertionDsc* const curAssertion = optGetAssertion(assertionIndex);
        if (curAssertion->CanPropLclVar() && (curAssertion->op2.kind == O2K_LCLVAR_COPY))
        {
            GenTree* const newTree = optCopyAssertionProp(curAssertion, tree, stmt DEBUGARG(assertionIndex));
            if (newTree != nullptr)
            {
                return newTree;
            }
        }
    }

    return nullptr;
}

//------------------------------------------------------------------------
// optAssertionProp_LocalStore: Try and optimize a local store via assertions.
//
// Propagates ZEROOBJ for the value. Suppresses no-op stores.
//
// Arguments:
//    assertions - set of live assertions
//    store      - the store to optimize
//    stmt       - statement containing "store"
//
// Returns:
//    Updated "store", or "nullptr"
//
// Notes:
//   stmt may be nullptr during local assertion prop
//
GenTree* Compiler::optAssertionProp_LocalStore(ASSERT_VALARG_TP assertions, GenTreeLclVarCommon* store, Statement* stmt)
{
    if (!optLocalAssertionProp)
    {
        // No ZEROOBJ assertions in global propagation.
        return nullptr;
    }

    // Try and simplify the value.
    //
    bool     madeChanges = false;
    GenTree* value       = store->Data();
    if (value->TypeIs(TYP_STRUCT) && optZeroObjAssertionProp(value, assertions))
    {
        madeChanges = true;
    }

    // If we're storing a value to a lcl/field that already has that value, suppress the store.
    //
    // For now we just check for zero.
    //
    // In particular we want to make sure that for struct S the "redundant init" pattern
    //
    //   S s = new S();
    //   s.field = 0;
    //
    // does not kill the zerobj assertion for s.
    //
    unsigned const       dstLclNum      = store->GetLclNum();
    bool const           dstLclIsStruct = lvaGetDesc(dstLclNum)->TypeIs(TYP_STRUCT);
    AssertionIndex const dstIndex =
        optLocalAssertionIsEqualOrNotEqual(O1K_LCLVAR, dstLclNum, dstLclIsStruct ? O2K_ZEROOBJ : O2K_CONST_INT, 0,
                                           assertions);
    if (dstIndex != NO_ASSERTION_INDEX)
    {
        AssertionDsc* const dstAssertion = optGetAssertion(dstIndex);
        if ((dstAssertion->assertionKind == OAK_EQUAL) && (dstAssertion->op2.u1.iconVal == 0))
        {
            // Destination is zero. Is value a literal zero? If so we don't need the store.
            //
            // The latter part of the if below is a heuristic.
            //
            // If we elimiate a zero store for integral lclVars it can lead to unnecessary
            // cloning. We need to make sure `optExtractInitTestIncr` still sees zero loop
            // iter lower bounds.
            //
            if (value->IsIntegralConst(0) && (dstLclIsStruct || varTypeIsGC(store)))
            {
                JITDUMP("[%06u] is assigning a constant zero to a struct field or gc local that is already zero\n",
                        dspTreeID(store));
                JITDUMPEXEC(optPrintAssertion(dstAssertion));

                store->gtBashToNOP();
                return optAssertionProp_Update(store, store, stmt);
            }
        }
    }

    // We might have simplified the value but were not able to remove the store.
    //
    if (madeChanges)
    {
        return optAssertionProp_Update(store, store, stmt);
    }

    return nullptr;
}

//------------------------------------------------------------------------
// optAssertionProp_BlockStore: Try and optimize a struct store via assertions.
//
// Propagates ZEROOBJ for the value. Propagates non-null assertions.
//
// Arguments:
//    assertions - set of live assertions
//    store      - the store to optimize
//    stmt       - statement containing "store"
//
// Returns:
//    Updated "store", or "nullptr"
//
// Notes:
//   stmt may be nullptr during local assertion prop
//
GenTree* Compiler::optAssertionProp_BlockStore(ASSERT_VALARG_TP assertions, GenTreeBlk* store, Statement* stmt)
{
    assert(store->OperIs(GT_STORE_BLK));

    bool didZeroObjProp = optZeroObjAssertionProp(store->Data(), assertions);
    bool didNonNullProp = optNonNullAssertionProp_Ind(assertions, store);
    if (didZeroObjProp || didNonNullProp)
    {
        return optAssertionProp_Update(store, store, stmt);
    }

    return nullptr;
}

//------------------------------------------------------------------------
// optAssertionProp_RangeProperties: Obtains range properties for an arbitrary tree
//
// Arguments:
//    assertions         - set of live assertions
//    tree               - the integral tree to analyze
//    stmt               - statement containing "tree"
//    block              - block containing "stmt"
//    isKnownNonZero     - [OUT] set to true if the tree is known to be non-zero
//    isKnownNonNegative - [OUT] set to true if the tree is known to be non-negative
//
void Compiler::optAssertionProp_RangeProperties(ASSERT_VALARG_TP assertions,
                                                GenTree*         tree,
                                                Statement*       stmt,
                                                BasicBlock*      block,
                                                bool*            isKnownNonZero,
                                                bool*            isKnownNonNegative)
{
    *isKnownNonZero     = false;
    *isKnownNonNegative = false;

    if (optLocalAssertionProp || !varTypeIsIntegral(tree) || BitVecOps::MayBeUninit(assertions) ||
        BitVecOps::IsEmpty(apTraits, assertions))
    {
        return;
    }

    // First, check simple properties without assertions.
    *isKnownNonNegative = tree->IsNeverNegative(this);
    *isKnownNonZero     = tree->IsNeverZero();

    if (*isKnownNonZero && *isKnownNonNegative)
    {
        // TP: We already have both properties, no need to check assertions.
        return;
    }

    const ValueNum  treeVN = vnStore->VNConservativeNormalValue(tree->gtVNPair);
    BitVecOps::Iter iter(apTraits, assertions);
    unsigned        index = 0;
    while (iter.NextElem(&index))
    {
        AssertionDsc* curAssertion = optGetAssertion(GetAssertionIndex(index));

        // if treeVN has a bound-check assertion where it's an index, then
        // it means it's not negative, example:
        //
        //   array[idx] = 42; // creates 'BoundsCheckNoThrow' assertion
        //   return idx % 8;  // idx is known to be never negative here, hence, MOD->UMOD
        //
        if (curAssertion->IsBoundsCheckNoThrow() && (curAssertion->op1.bnd.vnIdx == treeVN))
        {
            *isKnownNonNegative = true;
            continue;
        }

        // Same for Length, example:
        //
        //  array[idx] = 42;
        //  array.Length is known to be non-negative and non-zero here
        //
        if (curAssertion->IsBoundsCheckNoThrow() && (curAssertion->op1.bnd.vnLen == treeVN))
        {
            *isKnownNonNegative = true;
            *isKnownNonZero     = true;
            return; // both properties are known, no need to check other assertions
        }

        // First, analyze possible X ==/!= CNS assertions.
        if (curAssertion->IsConstantInt32Assertion() && (curAssertion->op1.vn == treeVN))
        {
            if ((curAssertion->assertionKind == OAK_NOT_EQUAL) && (curAssertion->op2.u1.iconVal == 0))
            {
                // X != 0 --> definitely non-zero
                // We can't say anything about X's non-negativity
                *isKnownNonZero = true;
            }
            else if (curAssertion->assertionKind != OAK_NOT_EQUAL)
            {
                // X == CNS --> definitely non-negative if CNS >= 0
                // and definitely non-zero if CNS != 0
                *isKnownNonNegative = curAssertion->op2.u1.iconVal >= 0;
                *isKnownNonZero     = curAssertion->op2.u1.iconVal != 0;
            }
        }

        // OAK_[NOT]_EQUAL assertion with op1 being O1K_CONSTANT_LOOP_BND
        // representing "(X relop CNS) ==/!= 0" assertion.
        if (!curAssertion->IsConstantBound() && !curAssertion->IsConstantBoundUnsigned())
        {
            continue;
        }

        ValueNumStore::ConstantBoundInfo info;
        vnStore->GetConstantBoundInfo(curAssertion->op1.vn, &info);

        if (info.cmpOpVN != treeVN)
        {
            continue;
        }

        // Root assertion has to be either:
        // (X relop CNS) == 0
        // (X relop CNS) != 0
        if ((curAssertion->op2.kind != O2K_CONST_INT) || (curAssertion->op2.u1.iconVal != 0))
        {
            continue;
        }

        genTreeOps cmpOper = static_cast<genTreeOps>(info.cmpOper);

        // Normalize "(X relop CNS) == false" to "(X reversed_relop CNS) == true"
        if (curAssertion->assertionKind == OAK_EQUAL)
        {
            cmpOper = GenTree::ReverseRelop(cmpOper);
        }

        if ((info.constVal >= 0))
        {
            if (info.isUnsigned && ((cmpOper == GT_LT) || (cmpOper == GT_LE)))
            {
                // (uint)X <= CNS means X is [0..CNS]
                *isKnownNonNegative = true;
            }
            else if (!info.isUnsigned && ((cmpOper == GT_GE) || (cmpOper == GT_GT)))
            {
                // X >= CNS means X is [CNS..unknown]
                *isKnownNonNegative = true;
                *isKnownNonZero     = (cmpOper == GT_GT) || (info.constVal > 0);
            }
        }
    }

    if (*isKnownNonZero && *isKnownNonNegative)
    {
        return;
    }

    // Let's see if MergeEdgeAssertions can help us:
    if (tree->TypeIs(TYP_INT))
    {
        // See if (X + CNS) is known to be non-negative
        if (tree->OperIs(GT_ADD) && tree->gtGetOp2()->IsIntCnsFitsInI32())
        {
            Range    rng = Range(Limit(Limit::keDependent));
            ValueNum vn  = vnStore->VNConservativeNormalValue(tree->gtGetOp1()->gtVNPair);
            if (!RangeCheck::TryGetRangeFromAssertions(this, vn, assertions, &rng))
            {
                return;
            }

            int cns = static_cast<int>(tree->gtGetOp2()->AsIntCon()->IconValue());
            rng.LowerLimit().AddConstant(cns);

            if ((rng.LowerLimit().IsConstant() && !rng.LowerLimit().AddConstant(cns)) ||
                (rng.UpperLimit().IsConstant() && !rng.UpperLimit().AddConstant(cns)))
            {
                // Add cns to both bounds if they are constants. Make sure the addition doesn't overflow.
                return;
            }

            if (rng.LowerLimit().IsConstant())
            {
                // E.g. "X + -8" when X's range is [8..unknown]
                // it's safe to say "X + -8" is non-negative
                if ((rng.LowerLimit().GetConstant() == 0))
                {
                    *isKnownNonNegative = true;
                }

                // E.g. "X + 8" when X's range is [0..CNS]
                // Here we have to check the upper bound as well to avoid overflow
                if ((rng.LowerLimit().GetConstant() > 0) && rng.UpperLimit().IsConstant() &&
                    rng.UpperLimit().GetConstant() > rng.LowerLimit().GetConstant())
                {
                    *isKnownNonNegative = true;
                    *isKnownNonZero     = true;
                }
            }
        }
        else
        {
            Range rng = Range(Limit(Limit::keUnknown));
            if (RangeCheck::TryGetRangeFromAssertions(this, treeVN, assertions, &rng))
            {
                Limit lowerBound = rng.LowerLimit();
                if (lowerBound.IsConstant())
                {
                    if (lowerBound.GetConstant() >= 0)
                    {
                        *isKnownNonNegative = true;
                    }
                    if (lowerBound.GetConstant() > 0)
                    {
                        *isKnownNonZero = true;
                    }
                }
            }
        }
    }
}

//------------------------------------------------------------------------
// optAssertionProp_ModDiv: Optimizes DIV/UDIV/MOD/UMOD via assertions
//    1) Convert DIV/MOD to UDIV/UMOD if both operands are proven to be never negative
//    2) Marks DIV/UDIV/MOD/UMOD with GTF_DIV_MOD_NO_BY_ZERO if divisor is proven to be never zero
//    3) Marks DIV/UDIV/MOD/UMOD with GTF_DIV_MOD_NO_OVERFLOW if both operands are proven to be never negative
//
// Arguments:
//    assertions - set of live assertions
//    tree       - the DIV/UDIV/MOD/UMOD node to optimize
//    stmt       - statement containing DIV/UDIV/MOD/UMOD
//    block      - the block containing the statement
//
// Returns:
//    Updated DIV/UDIV/MOD/UMOD node, or nullptr
//
GenTree* Compiler::optAssertionProp_ModDiv(ASSERT_VALARG_TP assertions,
                                           GenTreeOp*       tree,
                                           Statement*       stmt,
                                           BasicBlock*      block)
{
    GenTree* op1 = tree->gtGetOp1();
    GenTree* op2 = tree->gtGetOp2();

    bool op1IsNotZero;
    bool op2IsNotZero;
    bool op1IsNotNegative;
    bool op2IsNotNegative;
    optAssertionProp_RangeProperties(assertions, op1, stmt, block, &op1IsNotZero, &op1IsNotNegative);
    optAssertionProp_RangeProperties(assertions, op2, stmt, block, &op2IsNotZero, &op2IsNotNegative);

    bool changed = false;
    if (op1IsNotNegative && op2IsNotNegative && tree->OperIs(GT_DIV, GT_MOD))
    {
        JITDUMP("Converting DIV/MOD to unsigned UDIV/UMOD since both operands are never negative...\n")
        tree->SetOper(tree->OperIs(GT_DIV) ? GT_UDIV : GT_UMOD, GenTree::PRESERVE_VN);
        changed = true;
    }

    if (op2IsNotZero)
    {
        JITDUMP("Divisor for DIV/MOD is proven to be never negative...\n")
        tree->gtFlags |= GTF_DIV_MOD_NO_BY_ZERO;
        changed = true;
    }

    if (op1IsNotNegative || op2IsNotNegative)
    {
        JITDUMP("DIV/MOD is proven to never overflow...\n")
        tree->gtFlags |= GTF_DIV_MOD_NO_OVERFLOW;
        changed = true;
    }

    return changed ? optAssertionProp_Update(tree, tree, stmt) : nullptr;
}

//------------------------------------------------------------------------
// optAssertionProp_Return: Try and optimize a GT_RETURN/GT_SWIFT_ERROR_RET via assertions.
//
// Propagates ZEROOBJ for the return value.
//
// Arguments:
//    assertions - set of live assertions
//    ret        - the return node to optimize
//    stmt       - statement containing "ret"
//
// Returns:
//    Updated "ret", or "nullptr"
//
// Notes:
//   stmt may be nullptr during local assertion prop
//
GenTree* Compiler::optAssertionProp_Return(ASSERT_VALARG_TP assertions, GenTreeOp* ret, Statement* stmt)
{
    GenTree* retValue = ret->GetReturnValue();

    // Only propagate zeroes that lowering can deal with.
    if (!ret->TypeIs(TYP_VOID) && varTypeIsStruct(retValue) && !varTypeIsStruct(info.compRetNativeType))
    {
        if (optZeroObjAssertionProp(retValue, assertions))
        {
            return optAssertionProp_Update(ret, ret, stmt);
        }
    }

    return nullptr;
}

/*****************************************************************************
 *
 *  Given a set of "assertions" to search, find an assertion that matches
 *  op1Kind and lclNum, op2Kind and the constant value and is either equal or
 *  not equal assertion.
 */
AssertionIndex Compiler::optLocalAssertionIsEqualOrNotEqual(
    optOp1Kind op1Kind, unsigned lclNum, optOp2Kind op2Kind, ssize_t cnsVal, ASSERT_VALARG_TP assertions)
{
    noway_assert(op1Kind == O1K_LCLVAR);
    noway_assert((op2Kind == O2K_CONST_INT) || (op2Kind == O2K_ZEROOBJ));

    assert(optLocalAssertionProp);
    ASSERT_TP apDependent = BitVecOps::Intersection(apTraits, GetAssertionDep(lclNum), assertions);

    BitVecOps::Iter iter(apTraits, apDependent);
    unsigned        bvIndex = 0;
    while (iter.NextElem(&bvIndex))
    {
        AssertionIndex const index        = GetAssertionIndex(bvIndex);
        AssertionDsc*        curAssertion = optGetAssertion(index);

        if ((curAssertion->assertionKind != OAK_EQUAL) && (curAssertion->assertionKind != OAK_NOT_EQUAL))
        {
            continue;
        }

        if ((curAssertion->op1.kind == op1Kind) && (curAssertion->op1.lclNum == lclNum) &&
            (curAssertion->op2.kind == op2Kind))
        {
            bool constantIsEqual  = (curAssertion->op2.u1.iconVal == cnsVal);
            bool assertionIsEqual = (curAssertion->assertionKind == OAK_EQUAL);

            if (constantIsEqual || assertionIsEqual)
            {
                return index;
            }
        }
    }
    return NO_ASSERTION_INDEX;
}

//------------------------------------------------------------------------
// optGlobalAssertionIsEqualOrNotEqual: Look for an assertion in the specified
//        set that is one of op1 == op1, op1 != op2, or *op1 == op2,
//        where equality is based on value numbers.
//
// Arguments:
//      assertions: bit vector describing set of assertions
//      op1, op2:    the treen nodes in question
//
// Returns:
//      Index of first matching assertion, or NO_ASSERTION_INDEX if no
//      assertions in the set are matches.
//
// Notes:
//      Assertions based on *op1 are the result of exact type tests and are
//      only returned when op1 is a local var with ref type and the assertion
//      is an exact type equality.
//
AssertionIndex Compiler::optGlobalAssertionIsEqualOrNotEqual(ASSERT_VALARG_TP assertions, GenTree* op1, GenTree* op2)
{
    if (BitVecOps::IsEmpty(apTraits, assertions) || !optCanPropEqual)
    {
        return NO_ASSERTION_INDEX;
    }
    BitVecOps::Iter iter(apTraits, assertions);
    unsigned        index = 0;
    while (iter.NextElem(&index))
    {
        AssertionIndex assertionIndex = GetAssertionIndex(index);
        if (assertionIndex > optAssertionCount)
        {
            break;
        }
        AssertionDsc* curAssertion = optGetAssertion(assertionIndex);
        if (!curAssertion->CanPropEqualOrNotEqual())
        {
            continue;
        }

        if ((curAssertion->op1.vn == vnStore->VNConservativeNormalValue(op1->gtVNPair)) &&
            (curAssertion->op2.vn == vnStore->VNConservativeNormalValue(op2->gtVNPair)))
        {
            return assertionIndex;
        }

        // Look for matching exact type assertions based on vtable accesses. E.g.:
        //
        //   op1:       VNF_InvariantLoad(myObj) or in other words: a vtable access
        //   op2:       'MyType' class handle
        //   Assertion: 'myObj's type is exactly MyType
        //
        if ((curAssertion->assertionKind == OAK_EQUAL) && (curAssertion->op1.kind == O1K_EXACT_TYPE) &&
            (curAssertion->op2.vn == vnStore->VNConservativeNormalValue(op2->gtVNPair)) && op1->TypeIs(TYP_I_IMPL))
        {
            VNFuncApp funcApp;
            if (vnStore->GetVNFunc(vnStore->VNConservativeNormalValue(op1->gtVNPair), &funcApp) &&
                (funcApp.m_func == VNF_InvariantLoad) && (curAssertion->op1.vn == funcApp.m_args[0]))
            {
                return assertionIndex;
            }
        }
    }
    return NO_ASSERTION_INDEX;
}

/*****************************************************************************
 *
 *  Given a set of "assertions" to search for, find an assertion that is either
 *  op == 0 or op != 0
 *
 */
AssertionIndex Compiler::optGlobalAssertionIsEqualOrNotEqualZero(ASSERT_VALARG_TP assertions, GenTree* op1)
{
    if (BitVecOps::IsEmpty(apTraits, assertions) || !optCanPropEqual)
    {
        return NO_ASSERTION_INDEX;
    }
    BitVecOps::Iter iter(apTraits, assertions);
    unsigned        index = 0;
    while (iter.NextElem(&index))
    {
        AssertionIndex assertionIndex = GetAssertionIndex(index);
        if (assertionIndex > optAssertionCount)
        {
            break;
        }
        AssertionDsc* curAssertion = optGetAssertion(assertionIndex);
        if (!curAssertion->CanPropEqualOrNotEqual())
        {
            continue;
        }

        if ((curAssertion->op1.vn == vnStore->VNConservativeNormalValue(op1->gtVNPair)) &&
            (curAssertion->op2.vn == vnStore->VNZeroForType(op1->TypeGet())))
        {
            return assertionIndex;
        }
    }
    return NO_ASSERTION_INDEX;
}

/*****************************************************************************
 *
 *  Given a tree consisting of a RelOp and a set of available assertions
 *  we try to propagate an assertion and modify the RelOp tree if we can.
 *  We pass in the root of the tree via 'stmt', for local copy prop 'stmt' will be nullptr
 *  Returns the modified tree, or nullptr if no assertion prop took place
 */

GenTree* Compiler::optAssertionProp_RelOp(ASSERT_VALARG_TP assertions,
                                          GenTree*         tree,
                                          Statement*       stmt,
                                          BasicBlock*      block)
{
    assert(tree->OperIsCompare());

    if (!optLocalAssertionProp)
    {
        // If global assertion prop then use value numbering.
        return optAssertionPropGlobal_RelOp(assertions, tree, stmt, block);
    }

    //
    // Currently only GT_EQ or GT_NE are supported Relops for local AssertionProp
    //
    if (!tree->OperIs(GT_EQ, GT_NE))
    {
        return nullptr;
    }

    // If local assertion prop then use variable based prop.
    return optAssertionPropLocal_RelOp(assertions, tree, stmt);
}

//--------------------------------------------------------------------------------
// optVisitReachingAssertions: given a vn, call the specified callback function on all
//    the assertions that reach it via PHI definitions if any.
//
// Arguments:
//    vn         - The vn to visit all the reaching assertions for
//    argVisitor - The callback function to call on the vn and its reaching assertions
//
// Return Value:
//    AssertVisit::Aborted  - an argVisitor returned AssertVisit::Abort, we stop the walk and return
//    AssertVisit::Continue - all argVisitor returned AssertVisit::Continue
//
template <typename TAssertVisitor>
Compiler::AssertVisit Compiler::optVisitReachingAssertions(ValueNum vn, TAssertVisitor argVisitor)
{
    VNPhiDef phiDef;
    if (!vnStore->GetPhiDef(vn, &phiDef))
    {
        // We assume that the caller already checked assertions for the current block, so we're
        // interested only in assertions for PHI definitions.
        return AssertVisit::Abort;
    }

    LclSsaVarDsc*        ssaDef = lvaGetDesc(phiDef.LclNum)->GetPerSsaData(phiDef.SsaDef);
    GenTreeLclVarCommon* node   = ssaDef->GetDefNode();
    assert(node->IsPhiDefn());

    // Keep track of the set of phi-preds
    //
    BitVecTraits traits(fgBBNumMax + 1, this);
    BitVec       visitedBlocks = BitVecOps::MakeEmpty(&traits);

    for (GenTreePhi::Use& use : node->Data()->AsPhi()->Uses())
    {
        GenTreePhiArg* phiArg     = use.GetNode()->AsPhiArg();
        const ValueNum phiArgVN   = vnStore->VNConservativeNormalValue(phiArg->gtVNPair);
        ASSERT_TP      assertions = optGetEdgeAssertions(ssaDef->GetBlock(), phiArg->gtPredBB);
        if (argVisitor(phiArgVN, assertions) == AssertVisit::Abort)
        {
            // The visitor wants to abort the walk.
            return AssertVisit::Abort;
        }
        BitVecOps::AddElemD(&traits, visitedBlocks, phiArg->gtPredBB->bbNum);
    }

    // Verify the set of phi-preds covers the set of block preds
    //
    for (BasicBlock* const pred : ssaDef->GetBlock()->PredBlocks())
    {
        if (!BitVecOps::IsMember(&traits, visitedBlocks, pred->bbNum))
        {
            JITDUMP("... optVisitReachingAssertions in " FMT_BB ": pred " FMT_BB " not a phi-pred\n",
                    ssaDef->GetBlock()->bbNum, pred->bbNum);

            // We missed examining a block pred. Fail the phi inference.
            //
            return AssertVisit::Abort;
        }
    }
    return AssertVisit::Continue;
}

//------------------------------------------------------------------------
// optAssertionProp: try and optimize a relop via assertion propagation
//
// Arguments:
//   assertions  - set of live assertions
//   tree        - tree to possibly optimize
//   stmt        - statement containing the tree
//   block       - the block containing the statement
//
// Returns:
//   The modified tree, or nullptr if no assertion prop took place.
//
GenTree* Compiler::optAssertionPropGlobal_RelOp(ASSERT_VALARG_TP assertions,
                                                GenTree*         tree,
                                                Statement*       stmt,
                                                BasicBlock*      block)
{
    assert(!optLocalAssertionProp);

    GenTree* newTree = tree;
    GenTree* op1     = tree->AsOp()->gtOp1;
    GenTree* op2     = tree->AsOp()->gtOp2;

    // Can we fold "X relop 0" based on assertions?
    if (op2->IsIntegralConst(0) && tree->OperIsCmpCompare())
    {
        bool isNonZero, isNeverNegative;
        optAssertionProp_RangeProperties(assertions, op1, stmt, block, &isNonZero, &isNeverNegative);

        if (tree->OperIs(GT_GE, GT_LT) && isNeverNegative)
        {
            // Assertions: X >= 0
            //
            // X >= 0 --> true
            // X < 0  --> false
            newTree = tree->OperIs(GT_GE) ? gtNewTrue() : gtNewFalse();
        }
        else if (tree->OperIs(GT_GT, GT_LE) && isNeverNegative && isNonZero)
        {
            // Assertions: X > 0
            //
            // X > 0  --> true
            // X <= 0 --> false
            newTree = tree->OperIs(GT_GT) ? gtNewTrue() : gtNewFalse();
        }
        else if (tree->OperIs(GT_EQ, GT_NE) && isNonZero)
        {
            // Assertions: X != 0
            //
            // X != 0 --> true
            // X == 0 --> false
            newTree = tree->OperIs(GT_NE) ? gtNewTrue() : gtNewFalse();
        }

        if (newTree != tree)
        {
            newTree = gtWrapWithSideEffects(newTree, tree, GTF_ALL_EFFECT);
            return optAssertionProp_Update(newTree, tree, stmt);
        }
    }

    // Look for assertions of the form (tree EQ/NE 0)
    AssertionIndex index = optGlobalAssertionIsEqualOrNotEqualZero(assertions, tree);

    if (index != NO_ASSERTION_INDEX)
    {
        // We know that this relop is either 0 or != 0 (1)
        AssertionDsc* curAssertion = optGetAssertion(index);

#ifdef DEBUG
        if (verbose)
        {
            printf("\nVN relop based constant assertion prop in " FMT_BB ":\n", compCurBB->bbNum);
            printf("Assertion index=#%02u: ", index);
            printTreeID(tree);
            printf(" %s 0\n", (curAssertion->assertionKind == OAK_EQUAL) ? "==" : "!=");
        }
#endif

        newTree = curAssertion->assertionKind == OAK_EQUAL ? gtNewIconNode(0) : gtNewIconNode(1);
        newTree = gtWrapWithSideEffects(newTree, tree, GTF_ALL_EFFECT);
        DISPTREE(newTree);
        return optAssertionProp_Update(newTree, tree, stmt);
    }

    ValueNum op1VN = vnStore->VNConservativeNormalValue(op1->gtVNPair);
    ValueNum op2VN = vnStore->VNConservativeNormalValue(op2->gtVNPair);

    // See if we can fold "X relop CNS" using TryGetRangeFromAssertions.
    int op2cns;
    if (op1->TypeIs(TYP_INT) && op2->TypeIs(TYP_INT) &&
        vnStore->IsVNIntegralConstant(op2VN, &op2cns)
        // "op2cns != 0" is purely a TP quirk (such relops are handled by the code above):
        && (op2cns != 0))
    {
        // NOTE: we can call TryGetRangeFromAssertions for op2 as well if we want, but it's not cheap.
        Range rng1 = Range(Limit(Limit::keUndef));
        Range rng2 = Range(Limit(Limit::keConstant, op2cns));

        if (RangeCheck::TryGetRangeFromAssertions(this, op1VN, assertions, &rng1))
        {
            RangeOps::RelationKind kind = RangeOps::EvalRelop(tree->OperGet(), tree->IsUnsigned(), rng1, rng2);
            if ((kind != RangeOps::RelationKind::Unknown))
            {
                newTree = kind == RangeOps::RelationKind::AlwaysTrue ? gtNewTrue() : gtNewFalse();
                newTree = gtWrapWithSideEffects(newTree, tree, GTF_ALL_EFFECT);
                return optAssertionProp_Update(newTree, tree, stmt);
            }
        }
    }

    // Else check if we have an equality check involving a local or an indir
    if (!tree->OperIs(GT_EQ, GT_NE))
    {
        return nullptr;
    }

    // Bail out if op1 is not side effect free. Note we'll be bashing it below, unlike op2.
    if ((op1->gtFlags & GTF_SIDE_EFFECT) != 0)
    {
        return nullptr;
    }

    if (!op1->OperIs(GT_LCL_VAR, GT_IND))
    {
        return nullptr;
    }

    // See if we have "PHI ==/!= null" tree. If so, we iterate over all PHI's arguments,
    // and if all of them are known to be non-null, we can bash the comparison to true/false.
    if (op2->IsIntegralConst(0) && op1->TypeIs(TYP_REF))
    {
        JITDUMP("Checking PHI [%06u] arguments for non-nullness\n", dspTreeID(op1))
        auto visitor = [this](ValueNum reachingVN, ASSERT_TP reachingAssertions) {
            return optAssertionVNIsNonNull(reachingVN, reachingAssertions) ? AssertVisit::Continue : AssertVisit::Abort;
        };

        ValueNum op1vn = vnStore->VNConservativeNormalValue(op1->gtVNPair);
        if (optVisitReachingAssertions(op1vn, visitor) == AssertVisit::Continue)
        {
            JITDUMP("... all of PHI's arguments are never null!\n");
            assert(newTree->OperIs(GT_EQ, GT_NE));
            newTree = tree->OperIs(GT_EQ) ? gtNewIconNode(0) : gtNewIconNode(1);
            return optAssertionProp_Update(newTree, tree, stmt);
        }
    }

    // Find an equal or not equal assertion involving "op1" and "op2".
    index = optGlobalAssertionIsEqualOrNotEqual(assertions, op1, op2);

    if (index == NO_ASSERTION_INDEX)
    {
        return nullptr;
    }

    AssertionDsc* curAssertion         = optGetAssertion(index);
    bool          assertionKindIsEqual = (curAssertion->assertionKind == OAK_EQUAL);

    // Allow or not to reverse condition for OAK_NOT_EQUAL assertions.
    bool allowReverse = true;

    // If the assertion involves "op2" and it is a constant, then check if "op1" also has a constant value.
    ValueNum vnCns = vnStore->VNConservativeNormalValue(op2->gtVNPair);
    if (vnStore->IsVNConstant(vnCns))
    {
#ifdef DEBUG
        if (verbose)
        {
            printf("\nVN relop based constant assertion prop in " FMT_BB ":\n", compCurBB->bbNum);
            printf("Assertion index=#%02u: ", index);
            printTreeID(op1);
            printf(" %s ", assertionKindIsEqual ? "==" : "!=");
            if (genActualType(op1->TypeGet()) == TYP_INT)
            {
                printf("%d\n", vnStore->ConstantValue<int>(vnCns));
            }
            else if (op1->TypeIs(TYP_LONG))
            {
                printf("%lld\n", vnStore->ConstantValue<INT64>(vnCns));
            }
            else if (op1->TypeIs(TYP_DOUBLE))
            {
                printf("%f\n", vnStore->ConstantValue<double>(vnCns));
            }
            else if (op1->TypeIs(TYP_FLOAT))
            {
                printf("%f\n", vnStore->ConstantValue<float>(vnCns));
            }
            else if (op1->TypeIs(TYP_REF))
            {
                // The only constant of TYP_REF that ValueNumbering supports is 'null'
                if (vnStore->ConstantValue<size_t>(vnCns) == 0)
                {
                    printf("null\n");
                }
                else
                {
                    printf("%d (gcref)\n", static_cast<target_ssize_t>(vnStore->ConstantValue<size_t>(vnCns)));
                }
            }
            else if (op1->TypeIs(TYP_BYREF))
            {
                printf("%d (byref)\n", static_cast<target_ssize_t>(vnStore->ConstantValue<size_t>(vnCns)));
            }
            else
            {
                printf("??unknown\n");
            }
            gtDispTree(tree, nullptr, nullptr, true);
        }
#endif
        // Change the oper to const.
        if (genActualType(op1->TypeGet()) == TYP_INT)
        {
            op1->BashToConst(vnStore->ConstantValue<int>(vnCns));

            if (vnStore->IsVNHandle(vnCns))
            {
                op1->gtFlags |= (vnStore->GetHandleFlags(vnCns) & GTF_ICON_HDL_MASK);
            }
        }
        else if (op1->TypeIs(TYP_LONG))
        {
            op1->BashToConst(vnStore->ConstantValue<INT64>(vnCns));

            if (vnStore->IsVNHandle(vnCns))
            {
                op1->gtFlags |= (vnStore->GetHandleFlags(vnCns) & GTF_ICON_HDL_MASK);
            }
        }
        else if (op1->TypeIs(TYP_DOUBLE))
        {
            double constant = vnStore->ConstantValue<double>(vnCns);
            op1->BashToConst(constant);

            // Nothing can be equal to NaN. So if IL had "op1 == NaN", then we already made op1 NaN,
            // which will yield a false correctly. Instead if IL had "op1 != NaN", then we already
            // made op1 NaN which will yield a true correctly. Note that this is irrespective of the
            // assertion we have made.
            allowReverse = !FloatingPointUtils::isNaN(constant);
        }
        else if (op1->TypeIs(TYP_FLOAT))
        {
            float constant = vnStore->ConstantValue<float>(vnCns);
            op1->BashToConst(constant);

            // See comments for TYP_DOUBLE.
            allowReverse = !FloatingPointUtils::isNaN(constant);
        }
        else if (op1->TypeIs(TYP_REF))
        {
            op1->BashToConst(static_cast<target_ssize_t>(vnStore->ConstantValue<size_t>(vnCns)), TYP_REF);
        }
        else if (op1->TypeIs(TYP_BYREF))
        {
            op1->BashToConst(static_cast<target_ssize_t>(vnStore->ConstantValue<size_t>(vnCns)), TYP_BYREF);
        }
        else
        {
            noway_assert(!"unknown type in Global_RelOp");
        }

        op1->gtVNPair.SetBoth(vnCns); // Preserve the ValueNumPair, as BashToConst will clear it.

        // set foldResult to either 0 or 1
        bool foldResult = assertionKindIsEqual;
        if (tree->OperIs(GT_NE))
        {
            foldResult = !foldResult;
        }

        // Set the value number on the relop to 1 (true) or 0 (false)
        if (foldResult)
        {
            tree->gtVNPair.SetBoth(vnStore->VNOneForType(TYP_INT));
        }
        else
        {
            tree->gtVNPair.SetBoth(vnStore->VNZeroForType(TYP_INT));
        }
    }
    // If the assertion involves "op2" and "op1" is also a local var, then just morph the tree.
    else if (op1->OperIs(GT_LCL_VAR) && op2->OperIs(GT_LCL_VAR))
    {
#ifdef DEBUG
        if (verbose)
        {
            printf("\nVN relop based copy assertion prop in " FMT_BB ":\n", compCurBB->bbNum);
            printf("Assertion index=#%02u: V%02d.%02d %s V%02d.%02d\n", index, op1->AsLclVar()->GetLclNum(),
                   op1->AsLclVar()->GetSsaNum(),
                   (curAssertion->assertionKind == OAK_EQUAL) ? "==" : "!=", op2->AsLclVar()->GetLclNum(),
                   op2->AsLclVar()->GetSsaNum());
            gtDispTree(tree, nullptr, nullptr, true);
        }
#endif
        // If floating point, don't just substitute op1 with op2, this won't work if
        // op2 is NaN. Just turn it into a "true" or "false" yielding expression.
        if (op1->TypeIs(TYP_FLOAT, TYP_DOUBLE))
        {
            // Note we can't trust the OAK_EQUAL as the value could end up being a NaN
            // violating the assertion. However, we create OAK_EQUAL assertions for floating
            // point only on JTrue nodes, so if the condition held earlier, it will hold
            // now. We don't create OAK_EQUAL assertion on floating point from stores
            // because we depend on value num which would constant prop the NaN.
            op1->BashToConst(0.0, op1->TypeGet());
            op2->BashToConst(0.0, op2->TypeGet());
        }
        // Change the op1 LclVar to the op2 LclVar
        else
        {
            noway_assert(varTypeIsIntegralOrI(op1->TypeGet()));
            op1->AsLclVarCommon()->SetLclNum(op2->AsLclVarCommon()->GetLclNum());
            op1->AsLclVarCommon()->SetSsaNum(op2->AsLclVarCommon()->GetSsaNum());
        }
    }
    else
    {
        return nullptr;
    }

    // Finally reverse the condition, if we have a not equal assertion.
    if (allowReverse && curAssertion->assertionKind == OAK_NOT_EQUAL)
    {
        gtReverseCond(tree);
    }

    newTree = fgMorphTree(tree);

#ifdef DEBUG
    if (verbose)
    {
        gtDispTree(newTree, nullptr, nullptr, true);
    }
#endif

    return optAssertionProp_Update(newTree, tree, stmt);
}

/*************************************************************************************
 *
 *  Given the set of "assertions" to look up a relop assertion about the relop "tree",
 *  perform local variable name based relop assertion propagation on the tree.
 *
 */
GenTree* Compiler::optAssertionPropLocal_RelOp(ASSERT_VALARG_TP assertions, GenTree* tree, Statement* stmt)
{
    assert(tree->OperIs(GT_EQ) || tree->OperIs(GT_NE));

    GenTree* op1 = tree->AsOp()->gtOp1;
    GenTree* op2 = tree->AsOp()->gtOp2;

    // For Local AssertionProp we only can fold when op1 is a GT_LCL_VAR
    if (!op1->OperIs(GT_LCL_VAR))
    {
        return nullptr;
    }

    // For Local AssertionProp we only can fold when op2 is a GT_CNS_INT
    if (!op2->OperIs(GT_CNS_INT))
    {
        return nullptr;
    }

    optOp1Kind op1Kind = O1K_LCLVAR;
    optOp2Kind op2Kind = O2K_CONST_INT;
    ssize_t    cnsVal  = op2->AsIntCon()->gtIconVal;
    var_types  cmpType = op1->TypeGet();

    // Don't try to fold/optimize Floating Compares; there are multiple zero values.
    if (varTypeIsFloating(cmpType))
    {
        return nullptr;
    }

    // Find an equal or not equal assertion about op1 var.
    unsigned lclNum = op1->AsLclVarCommon()->GetLclNum();
    noway_assert(lclNum < lvaCount);
    AssertionIndex index = optLocalAssertionIsEqualOrNotEqual(op1Kind, lclNum, op2Kind, cnsVal, assertions);

    if (index == NO_ASSERTION_INDEX)
    {
        return nullptr;
    }

    AssertionDsc* curAssertion = optGetAssertion(index);

    bool assertionKindIsEqual = (curAssertion->assertionKind == OAK_EQUAL);
    bool constantIsEqual      = false;

    if (genTypeSize(cmpType) == TARGET_POINTER_SIZE)
    {
        constantIsEqual = (curAssertion->op2.u1.iconVal == cnsVal);
    }
#ifdef TARGET_64BIT
    else if (genTypeSize(cmpType) == sizeof(INT32))
    {
        // Compare the low 32-bits only
        constantIsEqual = (((INT32)curAssertion->op2.u1.iconVal) == ((INT32)cnsVal));
    }
#endif
    else
    {
        // We currently don't fold/optimize when the GT_LCL_VAR has been cast to a small type
        return nullptr;
    }

    noway_assert(constantIsEqual || assertionKindIsEqual);

#ifdef DEBUG
    if (verbose)
    {
        printf("\nAssertion prop for index #%02u in " FMT_BB ":\n", index, compCurBB->bbNum);
        gtDispTree(tree, nullptr, nullptr, true);
    }
#endif

    // Return either CNS_INT 0 or CNS_INT 1.
    bool foldResult = (constantIsEqual == assertionKindIsEqual);
    if (tree->OperIs(GT_NE))
    {
        foldResult = !foldResult;
    }

    op2->BashToConst((ssize_t)foldResult, TYP_INT);

    return optAssertionProp_Update(op2, tree, stmt);
}

//------------------------------------------------------------------------
// optAssertionProp_Cast: Propagate assertion for a cast, possibly removing it.
//
// The function use "optAssertionIsSubrange" to find an assertion which claims the
// cast's operand (only locals are supported) is a subrange of the "input" range
// for the cast, as computed by "IntegralRange::ForCastInput", and, if such
// assertion is found, act on it - either remove the cast if it is not changing
// representation, or try to remove the GTF_OVERFLOW flag from it.
//
// Arguments:
//    assertions - the set of live assertions
//    cast       - the cast for which to propagate the assertions
//    stmt       - statement "cast" is a part of, "nullptr" for local prop
//    block      - the block containing the statement
//
// Return Value:
//    The, possibly modified, cast tree or "nullptr" if no propagation took place.
//
GenTree* Compiler::optAssertionProp_Cast(ASSERT_VALARG_TP assertions,
                                         GenTreeCast*     cast,
                                         Statement*       stmt,
                                         BasicBlock*      block)
{
    GenTree* op1 = cast->CastOp();

    // Bail if we have a cast involving floating point or GC types.
    if (!varTypeIsIntegral(cast) || !varTypeIsIntegral(op1))
    {
        return nullptr;
    }

    // Skip over a GT_COMMA node(s), if necessary to get to the lcl.
    GenTree* lcl = op1->gtEffectiveVal();

    // Try and see if we can make this cast into a cheaper zero-extending version
    // if the input is known to be non-negative.
    if (!cast->IsUnsigned() && genActualTypeIsInt(lcl) && cast->TypeIs(TYP_LONG) && (TARGET_POINTER_SIZE == 8))
    {
        bool isKnownNonZero;
        bool isKnownNonNegative;
        optAssertionProp_RangeProperties(assertions, lcl, stmt, block, &isKnownNonZero, &isKnownNonNegative);
        if (isKnownNonNegative)
        {
            cast->SetUnsigned();
        }
    }

    // If we don't have a cast of a LCL_VAR then bail.
    if (!lcl->OperIs(GT_LCL_VAR))
    {
        return nullptr;
    }

    IntegralRange  range = IntegralRange::ForCastInput(cast);
    AssertionIndex index = optAssertionIsSubrange(lcl, range, assertions);
    if (index != NO_ASSERTION_INDEX)
    {
        LclVarDsc* varDsc = lvaGetDesc(lcl->AsLclVarCommon());

        // Representation-changing casts cannot be removed.
        if ((genActualType(cast) != genActualType(lcl)))
        {
            // Can we just remove the GTF_OVERFLOW flag?
            if (!cast->gtOverflow())
            {
                return nullptr;
            }
#ifdef DEBUG
            if (verbose)
            {
                printf("\nSubrange prop for index #%02u in " FMT_BB ":\n", index, compCurBB->bbNum);
                DISPNODE(cast);
            }
#endif
            cast->ClearOverflow();
            return optAssertionProp_Update(cast, cast, stmt);
        }

        // We might need to retype a "normalize on load" local back to its original small type
        // so that codegen recognizes it needs to use narrow loads if the local ends up in memory.
        if (varDsc->lvNormalizeOnLoad())
        {
            // The Jit is known to play somewhat loose with small types, so let's restrict this code
            // to the pattern we know is "safe and sound", i. e. CAST(type <- LCL_VAR(int, V00 type)).
            if ((varDsc->TypeGet() != cast->CastToType()) || !lcl->TypeIs(TYP_INT))
            {
                return nullptr;
            }

            op1->ChangeType(varDsc->TypeGet());
        }

#ifdef DEBUG
        if (verbose)
        {
            printf("\nSubrange prop for index #%02u in " FMT_BB ":\n", index, compCurBB->bbNum);
            DISPNODE(cast);
        }
#endif
        return optAssertionProp_Update(op1, cast, stmt);
    }

    return nullptr;
}

/*****************************************************************************
 *
 *  Given a tree with an array bounds check node, eliminate it because it was
 *  checked already in the program.
 */
GenTree* Compiler::optAssertionProp_Comma(ASSERT_VALARG_TP assertions, GenTree* tree, Statement* stmt)
{
    // Remove the bounds check as part of the GT_COMMA node since we need parent pointer to remove nodes.
    // When processing visits the bounds check, it sets the throw kind to None if the check is redundant.
    if (tree->gtGetOp1()->OperIs(GT_BOUNDS_CHECK) && ((tree->gtGetOp1()->gtFlags & GTF_CHK_INDEX_INBND) != 0))
    {
        optRemoveCommaBasedRangeCheck(tree, stmt);
        return optAssertionProp_Update(tree, tree, stmt);
    }
    return nullptr;
}

//------------------------------------------------------------------------
// optAssertionProp_Ind: see if we can prove the indirection can't cause
//    and exception.
//
// Arguments:
//   assertions  - set of live assertions
//   tree        - tree to possibly optimize
//   stmt        - statement containing the tree
//
// Returns:
//   The modified tree, or nullptr if no assertion prop took place.
//
// Notes:
//   stmt may be nullptr during local assertion prop
//
GenTree* Compiler::optAssertionProp_Ind(ASSERT_VALARG_TP assertions, GenTree* tree, Statement* stmt)
{
    assert(tree->OperIsIndir());

    bool updated = optNonNullAssertionProp_Ind(assertions, tree);
    if (tree->OperIs(GT_STOREIND))
    {
        updated |= optWriteBarrierAssertionProp_StoreInd(assertions, tree->AsStoreInd());
    }

    if (updated)
    {
        return optAssertionProp_Update(tree, tree, stmt);
    }
    return nullptr;
}

//------------------------------------------------------------------------
// optAssertionIsNonNull: see if we can prove a tree's value will be non-null
//   based on assertions
//
// Arguments:
//   op          - tree to check
//   assertions  - set of live assertions
//
// Return Value:
//   true if the tree's value will be non-null
//
bool Compiler::optAssertionIsNonNull(GenTree* op, ASSERT_VALARG_TP assertions)
{
    if (op->OperIs(GT_ADD) && op->AsOp()->gtGetOp2()->IsCnsIntOrI() &&
        !fgIsBigOffset(op->AsOp()->gtGetOp2()->AsIntCon()->IconValue()))
    {
        op = op->AsOp()->gtGetOp1();
    }

    // Fast path when we have a VN
    if (!optLocalAssertionProp && vnStore->IsKnownNonNull(op->gtVNPair.GetConservative()))
    {
        return true;
    }

    if (!optCanPropNonNull || BitVecOps::MayBeUninit(assertions))
    {
        return false;
    }

    op = op->gtEffectiveVal();
    if (!op->OperIs(GT_LCL_VAR))
    {
        return false;
    }

    // If local assertion prop use lcl comparison, else use VN comparison.
    if (!optLocalAssertionProp)
    {
        // Look at both the top-level vn, and
        // the vn we get by stripping off any constant adds.
        //
        ValueNum vn = vnStore->VNConservativeNormalValue(op->gtVNPair);
        if (vn == ValueNumStore::NoVN)
        {
            return false;
        }

        ValueNum       vnBase = vn;
        target_ssize_t offset = 0;
        vnStore->PeelOffsets(&vnBase, &offset);

        // Check each assertion to find if we have a vn != null assertion.
        //
        BitVecOps::Iter iter(apTraits, assertions);
        unsigned        index = 0;
        while (iter.NextElem(&index))
        {
            AssertionIndex assertionIndex = GetAssertionIndex(index);
            AssertionDsc*  curAssertion   = optGetAssertion(assertionIndex);
            if (curAssertion->CanPropNonNull() && ((curAssertion->op1.vn == vn) || (curAssertion->op1.vn == vnBase)))
            {
                return true;
            }
        }
    }
    else
    {
        // Find live assertions related to lclNum
        //
        unsigned const lclNum      = op->AsLclVarCommon()->GetLclNum();
        ASSERT_TP      apDependent = BitVecOps::Intersection(apTraits, GetAssertionDep(lclNum), assertions);

        // Scan those looking for a suitable assertion
        //
        BitVecOps::Iter iter(apTraits, apDependent);
        unsigned        index = 0;
        while (iter.NextElem(&index))
        {
            AssertionIndex assertionIndex = GetAssertionIndex(index);
            AssertionDsc*  curAssertion   = optGetAssertion(assertionIndex);

            if ((curAssertion->assertionKind == OAK_NOT_EQUAL) && // kind
                (curAssertion->op1.kind == O1K_LCLVAR) &&         // op1
                (curAssertion->op2.kind == O2K_CONST_INT) &&      // op2
                (curAssertion->op1.lclNum == lclNum) && (curAssertion->op2.u1.iconVal == 0))
            {
                return true;
            }
        }
    }
    return false;
}

//------------------------------------------------------------------------
// optAssertionVNIsNonNull: See if we can prove that the value of a VN is
// non-null using assertions.
//
// Arguments:
//   vn         - VN to check
//   assertions - set of live assertions
//
// Return Value:
//   True if the VN could be proven non-null.
//
bool Compiler::optAssertionVNIsNonNull(ValueNum vn, ASSERT_VALARG_TP assertions)
{
    if (vnStore->IsKnownNonNull(vn))
    {
        return true;
    }

    if (!BitVecOps::MayBeUninit(assertions))
    {
        BitVecOps::Iter iter(apTraits, assertions);
        unsigned        index = 0;
        while (iter.NextElem(&index))
        {
            AssertionDsc* curAssertion = optGetAssertion(GetAssertionIndex(index));
            if (curAssertion->CanPropNonNull() && curAssertion->op1.vn == vn)
            {
                return true;
            }
        }
    }

    return false;
}

/*****************************************************************************
 *
 *  Given a tree consisting of a call and a set of available assertions, we
 *  try to propagate a non-null assertion and modify the Call tree if we can.
 *  Returns the modified tree, or nullptr if no assertion prop took place.
 *
 */
GenTree* Compiler::optNonNullAssertionProp_Call(ASSERT_VALARG_TP assertions, GenTreeCall* call)
{
    if (!call->NeedsNullCheck())
    {
        return nullptr;
    }

    GenTree* op1 = call->gtArgs.GetThisArg()->GetNode();
    noway_assert(op1 != nullptr);

    if (optAssertionIsNonNull(op1, assertions))
    {
        JITDUMP("Non-null assertion prop for tree [%06d] in " FMT_BB ":\n", dspTreeID(op1), compCurBB->bbNum);

        call->gtFlags &= ~GTF_CALL_NULLCHECK;
        call->gtFlags &= ~GTF_EXCEPT;
        noway_assert(call->gtFlags & GTF_SIDE_EFFECT);
        return call;
    }

    return nullptr;
}

//------------------------------------------------------------------------
// optNonNullAssertionProp_Ind: Possibly prove an indirection non-faulting.
//
// Arguments:
//    assertions - Active assertions
//    indir      - The indirection
//
// Return Value:
//    Whether the indirection was found to be non-faulting and marked as such.
//
bool Compiler::optNonNullAssertionProp_Ind(ASSERT_VALARG_TP assertions, GenTree* indir)
{
    assert(indir->OperIsIndir());

    if ((indir->gtFlags & GTF_EXCEPT) == 0)
    {
        return false;
    }

    if (optAssertionIsNonNull(indir->AsIndir()->Addr(), assertions))
    {
        JITDUMP("Non-null assertion prop for indirection [%06d] in " FMT_BB ":\n", dspTreeID(indir), compCurBB->bbNum);

        indir->gtFlags &= ~GTF_EXCEPT;
        indir->gtFlags |= GTF_IND_NONFAULTING;

        // Set this flag to prevent reordering
        indir->SetHasOrderingSideEffect();

        return true;
    }

    return false;
}

//------------------------------------------------------------------------
// GetWriteBarrierForm: Determinate the exact type of write barrier required for the
//    given address.
//
// Arguments:
//    vnStore - ValueNumStore object
//    vn      - VN of the address
//
// Return Value:
//    Exact type of write barrier required for the given address.
//
static GCInfo::WriteBarrierForm GetWriteBarrierForm(Compiler* comp, ValueNum vn)
{
    ValueNumStore*  vnStore = comp->vnStore;
    const var_types type    = vnStore->TypeOfVN(vn);
    if (type == TYP_REF)
    {
        return GCInfo::WriteBarrierForm::WBF_BarrierUnchecked;
    }
    if (type != TYP_BYREF)
    {
        return GCInfo::WriteBarrierForm::WBF_BarrierUnknown;
    }

    VNFuncApp funcApp;
    if (vnStore->GetVNFunc(vnStore->VNNormalValue(vn), &funcApp))
    {
        if (funcApp.m_func == VNF_PtrToArrElem)
        {
            // Check whether the array is on the heap
            ValueNum arrayVN = funcApp.m_args[1];
            return GetWriteBarrierForm(comp, arrayVN);
        }
        if (funcApp.m_func == VNF_PtrToLoc)
        {
            // Pointer to a local
            return GCInfo::WriteBarrierForm::WBF_NoBarrier;
        }
        if ((funcApp.m_func == VNF_PtrToStatic) && vnStore->IsVNHandle(funcApp.m_args[0], GTF_ICON_STATIC_BOX_PTR))
        {
            // Boxed static - always on the heap
            return GCInfo::WriteBarrierForm::WBF_BarrierUnchecked;
        }
        if (funcApp.m_func == VNFunc(GT_ADD))
        {
            // Check arguments of the GT_ADD
            // To make it conservative, we require one of the arguments to be a constant, e.g.:
            //
            //   addressOfLocal + cns    -> NoBarrier
            //   cns + addressWithinHeap -> BarrierUnchecked
            //
            // Because "addressOfLocal + nativeIntVariable" could be in fact a pointer to the heap.
            // if "nativeIntVariable == addressWithinHeap - addressOfLocal".
            //
            if (vnStore->IsVNConstantNonHandle(funcApp.m_args[0]))
            {
                return GetWriteBarrierForm(comp, funcApp.m_args[1]);
            }
            if (vnStore->IsVNConstantNonHandle(funcApp.m_args[1]))
            {
                return GetWriteBarrierForm(comp, funcApp.m_args[0]);
            }
        }
    }
    return GCInfo::WriteBarrierForm::WBF_BarrierUnknown;
}

//------------------------------------------------------------------------
// optWriteBarrierAssertionProp_StoreInd: This function assists gcIsWriteBarrierCandidate with help of
//    assertions and VNs since CSE may "hide" addresses/values under locals, making it impossible for
//    gcIsWriteBarrierCandidate to determine the exact type of write barrier required
//    (it's too late for it to rely on VNs).
//
//    There are three cases we handle here:
//     * Target is not on the heap - no write barrier is required
//     * Target could be on the heap, but the value being stored doesn't require any write barrier
//     * Target is definitely on the heap - checked (slower) write barrier is not required
//
// Arguments:
//    assertions - Active assertions
//    indir      - The STOREIND node
//
// Return Value:
//    Whether the exact type of write barrier was determined and marked on the STOREIND node.
//
bool Compiler::optWriteBarrierAssertionProp_StoreInd(ASSERT_VALARG_TP assertions, GenTreeStoreInd* indir)
{
    const GenTree* value = indir->AsIndir()->Data();
    const GenTree* addr  = indir->AsIndir()->Addr();

    if (optLocalAssertionProp || !indir->TypeIs(TYP_REF) || !value->TypeIs(TYP_REF) ||
        ((indir->gtFlags & GTF_IND_TGT_NOT_HEAP) != 0))
    {
        return false;
    }

    GCInfo::WriteBarrierForm barrierType = GCInfo::WriteBarrierForm::WBF_BarrierUnknown;

    // First, analyze the value being stored
    auto vnVisitor = [this](ValueNum vn) -> ValueNumStore::VNVisit {
        if ((vn == ValueNumStore::VNForNull()) || vnStore->IsVNObjHandle(vn))
        {
            // No write barrier is required for null or nongc object handles as values
            return ValueNumStore::VNVisit::Continue;
        }
        return ValueNumStore::VNVisit::Abort;
    };

    if (vnStore->VNVisitReachingVNs(value->gtVNPair.GetConservative(), vnVisitor) == ValueNumStore::VNVisit::Continue)
    {
        barrierType = GCInfo::WriteBarrierForm::WBF_NoBarrier;
    }
    // Next, analyze the address if we haven't already determined the barrier type from the value
    else if ((indir->gtFlags & GTF_IND_TGT_HEAP) == 0)
    {
        // NOTE: we might want to inspect indirs with GTF_IND_TGT_HEAP flag as well - what if we can prove
        // that they actually need no barrier? But that comes with a TP regression.
        barrierType = GetWriteBarrierForm(this, addr->gtVNPair.GetConservative());
    }

    JITDUMP("Trying to determine the exact type of write barrier for STOREIND [%d06]: ", dspTreeID(indir));
    if (barrierType == GCInfo::WriteBarrierForm::WBF_NoBarrier)
    {
        JITDUMP("is not needed at all.\n");
        indir->gtFlags |= GTF_IND_TGT_NOT_HEAP;
        return true;
    }
    if (barrierType == GCInfo::WriteBarrierForm::WBF_BarrierUnchecked)
    {
        JITDUMP("unchecked is fine.\n");
        indir->gtFlags |= GTF_IND_TGT_HEAP;
        return true;
    }

    JITDUMP("unknown (checked).\n");
    return false;
}

/*****************************************************************************
 *
 *  Given a tree consisting of a call and a set of available assertions, we
 *  try to propagate an assertion and modify the Call tree if we can. Our
 *  current modifications are limited to removing the nullptrCHECK flag from
 *  the call.
 *  We pass in the root of the tree via 'stmt', for local copy prop 'stmt'
 *  will be nullptr. Returns the modified tree, or nullptr if no assertion prop
 *  took place.
 *
 */

GenTree* Compiler::optAssertionProp_Call(ASSERT_VALARG_TP assertions, GenTreeCall* call, Statement* stmt)
{
    if (optNonNullAssertionProp_Call(assertions, call))
    {
        return optAssertionProp_Update(call, call, stmt);
    }

    if (!optLocalAssertionProp && call->IsHelperCall())
    {
        const CorInfoHelpFunc helper = eeGetHelperNum(call->gtCallMethHnd);
        if ((helper == CORINFO_HELP_ISINSTANCEOFINTERFACE) || (helper == CORINFO_HELP_ISINSTANCEOFARRAY) ||
            (helper == CORINFO_HELP_ISINSTANCEOFCLASS) || (helper == CORINFO_HELP_ISINSTANCEOFANY) ||
            (helper == CORINFO_HELP_CHKCASTINTERFACE) || (helper == CORINFO_HELP_CHKCASTARRAY) ||
            (helper == CORINFO_HELP_CHKCASTCLASS) || (helper == CORINFO_HELP_CHKCASTANY) ||
            (helper == CORINFO_HELP_CHKCASTCLASS_SPECIAL))
        {
            CallArg* castToCallArg = call->gtArgs.GetArgByIndex(0);
            CallArg* objCallArg    = call->gtArgs.GetArgByIndex(1);
            GenTree* castToArg     = castToCallArg->GetNode();
            GenTree* objArg        = objCallArg->GetNode();

            const unsigned index = optAssertionIsSubtype(objArg, castToArg, assertions);
            if (index != NO_ASSERTION_INDEX)
            {
                JITDUMP("\nDid VN based subtype prop for index #%02u in " FMT_BB ":\n", index, compCurBB->bbNum);
                DISPTREE(call);

                // if castObjArg is not simple, we replace the arg with a temp assignment and
                // continue using that temp - it allows us reliably extract all side effects
                objArg = fgMakeMultiUse(&objCallArg->NodeRef());
                objArg = gtWrapWithSideEffects(objArg, call, GTF_SIDE_EFFECT, true);
                return optAssertionProp_Update(objArg, call, stmt);
            }

            // Leave a hint for fgLateCastExpansion that obj is never null.
            // GTF_CALL_M_CAST_CAN_BE_EXPANDED check is to improve TP
            if (((call->gtCallMoreFlags & GTF_CALL_M_CAST_CAN_BE_EXPANDED) != 0) &&
                optAssertionIsNonNull(objArg, assertions))
            {
                call->gtCallMoreFlags |= GTF_CALL_M_CAST_OBJ_NONNULL;
                return optAssertionProp_Update(call, call, stmt);
            }
        }
    }

    return nullptr;
}

/*****************************************************************************
 *
 *  Given a tree with a bounds check, remove it if it has already been checked in the program flow.
 */
GenTree* Compiler::optAssertionProp_BndsChk(ASSERT_VALARG_TP assertions, GenTree* tree, Statement* stmt)
{
    if (optLocalAssertionProp || !optCanPropBndsChk)
    {
        return nullptr;
    }

    assert(tree->OperIs(GT_BOUNDS_CHECK));

#ifdef FEATURE_ENABLE_NO_RANGE_CHECKS
    if (JitConfig.JitNoRngChks())
    {
#ifdef DEBUG
        if (verbose)
        {
            printf("\nFlagging check redundant due to JitNoRngChks in " FMT_BB ":\n", compCurBB->bbNum);
            gtDispTree(tree, nullptr, nullptr, true);
        }
#endif // DEBUG
        tree->gtFlags |= GTF_CHK_INDEX_INBND;
        return nullptr;
    }
#endif // FEATURE_ENABLE_NO_RANGE_CHECKS

    GenTreeBoundsChk* arrBndsChk = tree->AsBoundsChk();
    ValueNum          vnCurIdx   = vnStore->VNConservativeNormalValue(arrBndsChk->GetIndex()->gtVNPair);
    ValueNum          vnCurLen   = vnStore->VNConservativeNormalValue(arrBndsChk->GetArrayLength()->gtVNPair);

    auto dropBoundsCheck = [&](INDEBUG(const char* reason)) -> GenTree* {
        JITDUMP("\nVN based redundant (%s) bounds check assertion prop in " FMT_BB ":\n", reason, compCurBB->bbNum);
        DISPTREE(tree);
        if (arrBndsChk != stmt->GetRootNode())
        {
            // Defer the removal.
            arrBndsChk->gtFlags |= GTF_CHK_INDEX_INBND;
            return nullptr;
        }

        GenTree* newTree = optRemoveStandaloneRangeCheck(arrBndsChk, stmt);
        return optAssertionProp_Update(newTree, arrBndsChk, stmt);
    };

    // First, check if we have arr[arr.Length - cns] when we know arr.Length is >= cns.
    VNFuncApp funcApp;
    if (vnStore->GetVNFunc(vnCurIdx, &funcApp) && (funcApp.m_func == VNF_ADD))
    {
        if (!vnStore->IsVNInt32Constant(funcApp.m_args[1]))
        {
            // Normalize constants to be on the right side
            std::swap(funcApp.m_args[0], funcApp.m_args[1]);
        }

        Range rng = Range(Limit(Limit::keUnknown));
        if ((funcApp.m_args[0] == vnCurLen) && vnStore->IsVNInt32Constant(funcApp.m_args[1]) &&
            RangeCheck::TryGetRangeFromAssertions(this, vnCurLen, assertions, &rng) && rng.LowerLimit().IsConstant())
        {
            // Lower known limit of ArrLen:
            const int lenLowerLimit = rng.LowerLimit().GetConstant();

            // Negative delta in the array access (ArrLen + -CNS)
            const int delta = vnStore->GetConstantInt32(funcApp.m_args[1]);
            if ((lenLowerLimit > 0) && (delta < 0) && (delta > INT_MIN) && (lenLowerLimit >= -delta))
            {
                return dropBoundsCheck(INDEBUG("a[a.Length-cns] when a.Length is known to be >= cns"));
            }
        }
    }

    BitVecOps::Iter iter(apTraits, assertions);
    unsigned        index = 0;
    while (iter.NextElem(&index))
    {
        AssertionIndex assertionIndex = GetAssertionIndex(index);
        if (assertionIndex > optAssertionCount)
        {
            break;
        }
        // If it is not a nothrow assertion, skip.
        AssertionDsc* curAssertion = optGetAssertion(assertionIndex);
        if (!curAssertion->IsBoundsCheckNoThrow())
        {
            continue;
        }

        // Do we have a previous range check involving the same 'vnLen' upper bound?
        if (curAssertion->op1.bnd.vnLen == vnStore->VNConservativeNormalValue(arrBndsChk->GetArrayLength()->gtVNPair))
        {
            // Do we have the exact same lower bound 'vnIdx'?
            //       a[i] followed by a[i]
            if (curAssertion->op1.bnd.vnIdx == vnCurIdx)
            {
                return dropBoundsCheck(INDEBUG("a[i] followed by a[i]"));
            }
            // Are we using zero as the index?
            // It can always be considered as redundant with any previous value
            //       a[*] followed by a[0]
            else if (vnCurIdx == vnStore->VNZeroForType(arrBndsChk->GetIndex()->TypeGet()))
            {
                return dropBoundsCheck(INDEBUG("a[*] followed by a[0]"));
            }
            // Do we have two constant indexes?
            else if (vnStore->IsVNConstant(curAssertion->op1.bnd.vnIdx) && vnStore->IsVNConstant(vnCurIdx))
            {
                // Make sure the types match.
                var_types type1 = vnStore->TypeOfVN(curAssertion->op1.bnd.vnIdx);
                var_types type2 = vnStore->TypeOfVN(vnCurIdx);

                if (type1 == type2 && type1 == TYP_INT)
                {
                    int index1 = vnStore->ConstantValue<int>(curAssertion->op1.bnd.vnIdx);
                    int index2 = vnStore->ConstantValue<int>(vnCurIdx);

                    // the case where index1 == index2 should have been handled above
                    assert(index1 != index2);

                    // It can always be considered as redundant with any previous higher constant value
                    //       a[K1] followed by a[K2], with K2 >= 0 and K1 >= K2
                    if (index2 >= 0 && index1 >= index2)
                    {
                        return dropBoundsCheck(INDEBUG("a[K1] followed by a[K2], with K2 >= 0 and K1 >= K2"));
                    }
                }
            }
            // Extend this to remove additional redundant bounds checks:
            // i.e.  a[i+1] followed by a[i]  by using the VN(i+1) >= VN(i)
            //       a[i]   followed by a[j]  when j is known to be >= i
            //       a[i]   followed by a[5]  when i is known to be >= 5
        }
    }

    return nullptr;
}

/*****************************************************************************
 *
 *  Called when we have a successfully performed an assertion prop. We have
 *  the newTree in hand. This method will replace the existing tree in the
 *  stmt with the newTree.
 *
 */

GenTree* Compiler::optAssertionProp_Update(GenTree* newTree, GenTree* tree, Statement* stmt)
{
    assert(newTree != nullptr);
    assert(tree != nullptr);

    if (stmt == nullptr)
    {
        noway_assert(optLocalAssertionProp);
    }
    else
    {
        noway_assert(!optLocalAssertionProp);

        // If newTree == tree then we modified the tree in-place otherwise we have to
        // locate our parent node and update it so that it points to newTree.
        if (newTree != tree)
        {
            FindLinkData linkData = gtFindLink(stmt, tree);
            GenTree**    useEdge  = linkData.result;
            GenTree*     parent   = linkData.parent;
            noway_assert(useEdge != nullptr);

            if (parent != nullptr)
            {
                parent->ReplaceOperand(useEdge, newTree);

                // If the parent is a GT_IND and we replaced the child with a handle constant, we might need
                // to mark the GT_IND as invariant. This is the same as what gtNewIndOfIconHandleNode() does.
                // Review: should some kind of more general morphing take care of this?
                // Should this share code with gtNewIndOfIconHandleNode()?

                if (parent->OperIs(GT_IND) && newTree->IsIconHandle())
                {
                    GenTreeFlags iconFlags = newTree->GetIconHandleFlag();
                    if (GenTree::HandleKindDataIsInvariant(iconFlags))
                    {
                        parent->gtFlags |= GTF_IND_INVARIANT;
                        if (iconFlags == GTF_ICON_STR_HDL)
                        {
                            // String literals are never null
                            parent->gtFlags |= GTF_IND_NONNULL;
                        }
                    }
                }
            }
            else
            {
                // If there's no parent, the tree being replaced is the root of the
                // statement.
                assert((stmt->GetRootNode() == tree) && (stmt->GetRootNodePointer() == useEdge));
                stmt->SetRootNode(newTree);
            }

            // We only need to ensure that the gtNext field is set as it is used to traverse
            // to the next node in the tree. We will re-morph this entire statement in
            // optAssertionPropMain(). It will reset the gtPrev and gtNext links for all nodes.
            newTree->gtNext = tree->gtNext;

            // Old tree should not be referenced anymore.
            DEBUG_DESTROY_NODE(tree);
        }
    }

    // Record that we propagated the assertion.
    optAssertionPropagated            = true;
    optAssertionPropagatedCurrentStmt = true;

    return newTree;
}

//------------------------------------------------------------------------
// optAssertionProp: try and optimize a tree via assertion propagation
//
// Arguments:
//   assertions  - set of live assertions
//   tree        - tree to possibly optimize
//   stmt        - statement containing the tree
//   block       - block containing the statement
//
// Returns:
//   The modified tree, or nullptr if no assertion prop took place.
//
// Notes:
//   stmt may be nullptr during local assertion prop
//
GenTree* Compiler::optAssertionProp(ASSERT_VALARG_TP assertions, GenTree* tree, Statement* stmt, BasicBlock* block)
{
    switch (tree->gtOper)
    {
        case GT_LCL_VAR:
            return optAssertionProp_LclVar(assertions, tree->AsLclVarCommon(), stmt);

        case GT_LCL_FLD:
            return optAssertionProp_LclFld(assertions, tree->AsLclVarCommon(), stmt);

        case GT_STORE_LCL_VAR:
        case GT_STORE_LCL_FLD:
            return optAssertionProp_LocalStore(assertions, tree->AsLclVarCommon(), stmt);

        case GT_STORE_BLK:
            return optAssertionProp_BlockStore(assertions, tree->AsBlk(), stmt);

        case GT_RETURN:
        case GT_SWIFT_ERROR_RET:
            return optAssertionProp_Return(assertions, tree->AsOp(), stmt);

        case GT_MOD:
        case GT_DIV:
        case GT_UMOD:
        case GT_UDIV:
            return optAssertionProp_ModDiv(assertions, tree->AsOp(), stmt, block);

        case GT_BLK:
        case GT_IND:
        case GT_STOREIND:
        case GT_NULLCHECK:
            return optAssertionProp_Ind(assertions, tree, stmt);

        case GT_BOUNDS_CHECK:
            return optAssertionProp_BndsChk(assertions, tree, stmt);

        case GT_COMMA:
            return optAssertionProp_Comma(assertions, tree, stmt);

        case GT_CAST:
            return optAssertionProp_Cast(assertions, tree->AsCast(), stmt, block);

        case GT_CALL:
            return optAssertionProp_Call(assertions, tree->AsCall(), stmt);

        case GT_EQ:
        case GT_NE:
        case GT_LT:
        case GT_LE:
        case GT_GT:
        case GT_GE:
            return optAssertionProp_RelOp(assertions, tree, stmt, block);

        case GT_JTRUE:
            if (block != nullptr)
            {
                return optVNConstantPropOnJTrue(block, tree);
            }
            return nullptr;

        default:
            return nullptr;
    }
}

//------------------------------------------------------------------------
// optImpliedAssertions: Given an assertion this method computes the set
//                       of implied assertions that are also true.
//
// Arguments:
//      assertionIndex   : The id of the assertion.
//      activeAssertions : The assertions that are already true at this point.
//                         This method will add the discovered implied assertions
//                         to this set.
//
void Compiler::optImpliedAssertions(AssertionIndex assertionIndex, ASSERT_TP& activeAssertions)
{
    noway_assert(!optLocalAssertionProp);
    noway_assert(assertionIndex != 0);
    noway_assert(assertionIndex <= optAssertionCount);

    // Is curAssertion a constant store of a 32-bit integer?
    // (i.e  GT_LVL_VAR X  == GT_CNS_INT)
    AssertionDsc* curAssertion = optGetAssertion(assertionIndex);
    if ((curAssertion->assertionKind == OAK_EQUAL) && (curAssertion->op1.kind == O1K_LCLVAR) &&
        (curAssertion->op2.kind == O2K_CONST_INT))
    {
        optImpliedByConstAssertion(curAssertion, activeAssertions);
    }
}

//------------------------------------------------------------------------
// optCreateJumpTableImpliedAssertions: Create assertions for the switch statement
//     for each of its jump targets.
//
// Arguments:
//     switchBb - The switch statement block.
//
// Returns:
//     true if any modifications were made, false otherwise.
//
bool Compiler::optCreateJumpTableImpliedAssertions(BasicBlock* switchBb)
{
    assert(!optLocalAssertionProp);
    assert(switchBb->KindIs(BBJ_SWITCH));
    assert(switchBb->lastStmt() != nullptr);
    bool modified = false;

    GenTree* switchTree = switchBb->lastStmt()->GetRootNode()->gtEffectiveVal();
    assert(switchTree->OperIs(GT_SWITCH));

    // Case count is uint32_t, but it's unlikely to be more than INT32_MAX.
    noway_assert(switchBb->GetSwitchTargets()->GetCaseCount() <= INT32_MAX);

    ValueNum opVN = optConservativeNormalVN(switchTree->gtGetOp1());
    if (opVN == ValueNumStore::NoVN)
    {
        return modified;
    }

    if (vnStore->TypeOfVN(opVN) != TYP_INT)
    {
        // Should probably be an assert instead - GT_SWITCH is expected to be TYP_INT.
        return modified;
    }

    // Typically, the switch value is ADD(X, -cns), so we actually want to create the assertions for X
    int offset = 0;
    vnStore->PeelOffsetsI32(&opVN, &offset);

    int        jumpCount  = static_cast<int>(switchBb->GetSwitchTargets()->GetCaseCount());
    FlowEdge** jumpTable  = switchBb->GetSwitchTargets()->GetCases();
    bool       hasDefault = switchBb->GetSwitchTargets()->HasDefaultCase();

    for (int jmpTargetIdx = 0; jmpTargetIdx < jumpCount; jmpTargetIdx++)
    {
        // The value for each target is jmpTargetIdx - offset.
        if (CheckedOps::SubOverflows(jmpTargetIdx, offset, false))
        {
            continue;
        }
        int value = jmpTargetIdx - offset;

        // We can only make "X == caseValue" assertions for blocks with a single edge from the switch.
        FlowEdge* const   edge   = jumpTable[jmpTargetIdx];
        BasicBlock* const target = edge->getDestinationBlock();
        if (target->GetUniquePred(this) != switchBb)
        {
            // Target block is potentially reachable from multiple blocks (outside the switch).
            continue;
        }

        if (edge->getDupCount() > 1)
        {
            // We have just one predecessor (BBJ_SWITCH), but there may be multiple edges (cases) per target.
            continue;
        }

        AssertionInfo newAssertIdx = NO_ASSERTION_INDEX;

        // Is this target a default case?
        if (hasDefault && (jmpTargetIdx == jumpCount - 1))
        {
            // For default case we can create "X >= maxValue" assertion. Example:
            //
            //   void Test(ReadOnlySpan<byte> name)
            //   {
            //       switch (name.Length)
            //       {
            //           case 0: ...
            //           case 1: ...
            //           ...
            //           case 7: ...
            //           default: %name.Length is >= 8 here%
            //       }
            //
            if ((value > 0) && !vnStore->IsVNConstant(opVN))
            {
                AssertionDsc dsc   = {};
                dsc.assertionKind  = OAK_NOT_EQUAL;
                dsc.op2.kind       = O2K_CONST_INT;
                dsc.op2.vn         = vnStore->VNZeroForType(TYP_INT);
                dsc.op2.u1.iconVal = 0;
                dsc.op2.SetIconFlag(GTF_EMPTY);
                if (vnStore->IsVNNeverNegative(opVN))
                {
                    // Create "X >= value" assertion (both operands are never negative)
                    dsc.op1.kind = O1K_CONSTANT_LOOP_BND;
                    dsc.op1.vn   = vnStore->VNForFunc(TYP_INT, VNF_GE, opVN, vnStore->VNForIntCon(value));
                    assert(vnStore->IsVNConstantBound(dsc.op1.vn));
                }
                else
                {
                    // Create "X u>= value" assertion
                    dsc.op1.kind = O1K_CONSTANT_LOOP_BND_UN;
                    dsc.op1.vn   = vnStore->VNForFunc(TYP_INT, VNF_GE_UN, opVN, vnStore->VNForIntCon(value));
                    assert(vnStore->IsVNConstantBoundUnsigned(dsc.op1.vn));
                }
                newAssertIdx = optAddAssertion(&dsc);
            }
            else
            {
                continue;
            }
        }
        else
        {
            // Create "VN == value" assertion.
            AssertionDsc dsc   = {};
            dsc.assertionKind  = OAK_EQUAL;
            dsc.op1.lclNum     = BAD_VAR_NUM; // O1K_LCLVAR relies only on op1.vn in Global Assertion Prop
            dsc.op1.vn         = opVN;
            dsc.op1.kind       = O1K_LCLVAR;
            dsc.op2.vn         = vnStore->VNForIntCon(value);
            dsc.op2.u1.iconVal = value;
            dsc.op2.kind       = O2K_CONST_INT;
            dsc.op2.SetIconFlag(GTF_EMPTY);
            newAssertIdx = optAddAssertion(&dsc);
        }

        if (newAssertIdx.HasAssertion())
        {
            // TODO-Cleanup: We shouldn't attach assertions to nodes in Global Assertion Prop.
            // It limits the ability to create multiple assertions for the same node.
            GenTree* tree = gtNewNothingNode();
            fgInsertStmtAtBeg(target, fgNewStmtFromTree(tree));

            modified = true;
            tree->SetAssertionInfo(newAssertIdx);
        }
    }

    return modified;
}

/*****************************************************************************
 *
 *   Given a set of active assertions this method computes the set
 *   of non-Null implied assertions that are also true
 */

void Compiler::optImpliedByTypeOfAssertions(ASSERT_TP& activeAssertions)
{
    if (BitVecOps::IsEmpty(apTraits, activeAssertions))
    {
        return;
    }

    // Check each assertion in activeAssertions to see if it can be applied to constAssertion
    BitVecOps::Iter chkIter(apTraits, activeAssertions);
    unsigned        chkIndex = 0;
    while (chkIter.NextElem(&chkIndex))
    {
        AssertionIndex chkAssertionIndex = GetAssertionIndex(chkIndex);
        if (chkAssertionIndex > optAssertionCount)
        {
            break;
        }
        // chkAssertion must be Type/Subtype is equal assertion
        AssertionDsc* chkAssertion = optGetAssertion(chkAssertionIndex);
        if ((chkAssertion->op1.kind != O1K_SUBTYPE && chkAssertion->op1.kind != O1K_EXACT_TYPE) ||
            (chkAssertion->assertionKind != OAK_EQUAL))
        {
            continue;
        }

        // Search the assertion table for a non-null assertion on op1 that matches chkAssertion
        for (AssertionIndex impIndex = 1; impIndex <= optAssertionCount; impIndex++)
        {
            AssertionDsc* impAssertion = optGetAssertion(impIndex);

            //  The impAssertion must be different from the chkAssertion
            if (impIndex == chkAssertionIndex)
            {
                continue;
            }

            // impAssertion must be a Non Null assertion on lclNum
            if ((impAssertion->assertionKind != OAK_NOT_EQUAL) || (impAssertion->op1.kind != O1K_LCLVAR) ||
                (impAssertion->op2.kind != O2K_CONST_INT) || (impAssertion->op1.vn != chkAssertion->op1.vn))
            {
                continue;
            }

            // The bit may already be in the result set
            if (!BitVecOps::IsMember(apTraits, activeAssertions, impIndex - 1))
            {
                BitVecOps::AddElemD(apTraits, activeAssertions, impIndex - 1);
#ifdef DEBUG
                if (verbose)
                {
                    printf("\nCompiler::optImpliedByTypeOfAssertions: %s Assertion #%02d, implies assertion #%02d",
                           (chkAssertion->op1.kind == O1K_SUBTYPE) ? "Subtype" : "Exact-type", chkAssertionIndex,
                           impIndex);
                }
#endif
            }

            // There is at most one non-null assertion that is implied by the current chkIndex assertion
            break;
        }
    }
}

//------------------------------------------------------------------------
// optGetVnMappedAssertions: Given a value number, get the assertions
//                           we have about the value number.
//
// Arguments:
//      vn - The given value number.
//
// Return Value:
//      The assertions we have about the value number.
//
ASSERT_VALRET_TP Compiler::optGetVnMappedAssertions(ValueNum vn)
{
    ASSERT_TP set = BitVecOps::UninitVal();
    if (optValueNumToAsserts->Lookup(vn, &set))
    {
        return set;
    }
    return BitVecOps::UninitVal();
}

//------------------------------------------------------------------------
// optGetEdgeAssertions: Given a block and its predecessor, get the assertions
//                       the predecessor creates for the block.
//
// Arguments:
//      block     - The block to get the assertions for.
//      blockPred - The predecessor of the block (creating the assertions).
//
// Return Value:
//      The assertions we have about the value number.
//
ASSERT_VALRET_TP Compiler::optGetEdgeAssertions(const BasicBlock* block, const BasicBlock* blockPred) const
{
    if ((blockPred->KindIs(BBJ_COND) && blockPred->TrueTargetIs(block)))
    {
        if (bbJtrueAssertionOut != nullptr)
        {
            return bbJtrueAssertionOut[blockPred->bbNum];
        }
        return BitVecOps::MakeEmpty(apTraits);
    }
    return blockPred->bbAssertionOut;
}

/*****************************************************************************
 *
 *   Given a const assertion this method computes the set of implied assertions
 *   that are also true
 */

void Compiler::optImpliedByConstAssertion(AssertionDsc* constAssertion, ASSERT_TP& result)
{
    noway_assert(constAssertion->assertionKind == OAK_EQUAL);
    noway_assert(constAssertion->op1.kind == O1K_LCLVAR);
    noway_assert(constAssertion->op2.kind == O2K_CONST_INT);

    ssize_t iconVal = constAssertion->op2.u1.iconVal;

    const ASSERT_TP chkAssertions = optGetVnMappedAssertions(constAssertion->op1.vn);
    if (chkAssertions == nullptr || BitVecOps::IsEmpty(apTraits, chkAssertions))
    {
        return;
    }

    // Check each assertion in chkAssertions to see if it can be applied to constAssertion
    BitVecOps::Iter chkIter(apTraits, chkAssertions);
    unsigned        chkIndex = 0;
    while (chkIter.NextElem(&chkIndex))
    {
        AssertionIndex chkAssertionIndex = GetAssertionIndex(chkIndex);
        if (chkAssertionIndex > optAssertionCount)
        {
            break;
        }
        // The impAssertion must be different from the const assertion.
        AssertionDsc* impAssertion = optGetAssertion(chkAssertionIndex);
        if (impAssertion == constAssertion)
        {
            continue;
        }

        // The impAssertion must be an assertion about the same local var.
        if (impAssertion->op1.vn != constAssertion->op1.vn)
        {
            continue;
        }

        bool usable = false;
        switch (impAssertion->op2.kind)
        {
            case O2K_SUBRANGE:
                // Is the const assertion's constant, within implied assertion's bounds?
                usable = impAssertion->op2.u2.Contains(iconVal);
                break;

            case O2K_CONST_INT:
                // Is the const assertion's constant equal/not equal to the implied assertion?
                usable = ((impAssertion->assertionKind == OAK_EQUAL) && (impAssertion->op2.u1.iconVal == iconVal)) ||
                         ((impAssertion->assertionKind == OAK_NOT_EQUAL) && (impAssertion->op2.u1.iconVal != iconVal));
                break;

            default:
                // leave 'usable' = false;
                break;
        }

        if (usable)
        {
            BitVecOps::AddElemD(apTraits, result, chkIndex);
#ifdef DEBUG
            if (verbose)
            {
                AssertionDsc* firstAssertion = optGetAssertion(1);
                printf("Compiler::optImpliedByConstAssertion: const assertion #%02d implies assertion #%02d\n",
                       (constAssertion - firstAssertion) + 1, (impAssertion - firstAssertion) + 1);
            }
#endif
        }
    }
}

#include "dataflow.h"

/*****************************************************************************
 *
 * Dataflow visitor like callback so that all dataflow is in a single place
 *
 */
class AssertionPropFlowCallback
{
private:
    ASSERT_TP preMergeOut;
    ASSERT_TP preMergeJumpDestOut;

    ASSERT_TP* mJumpDestOut;
    ASSERT_TP* mJumpDestGen;

    BitVecTraits* apTraits;

public:
    AssertionPropFlowCallback(Compiler* pCompiler, ASSERT_TP* jumpDestOut, ASSERT_TP* jumpDestGen)
        : preMergeOut(BitVecOps::UninitVal())
        , preMergeJumpDestOut(BitVecOps::UninitVal())
        , mJumpDestOut(jumpDestOut)
        , mJumpDestGen(jumpDestGen)
        , apTraits(pCompiler->apTraits)
    {
    }

    // At the start of the merge function of the dataflow equations, initialize premerge state (to detect change.)
    void StartMerge(BasicBlock* block)
    {
        if (VerboseDataflow())
        {
            JITDUMP("StartMerge: " FMT_BB " ", block->bbNum);
            Compiler::optDumpAssertionIndices("in -> ", block->bbAssertionIn, "\n");
        }

        BitVecOps::Assign(apTraits, preMergeOut, block->bbAssertionOut);
        BitVecOps::Assign(apTraits, preMergeJumpDestOut, mJumpDestOut[block->bbNum]);
    }

    // During merge, perform the actual merging of the predecessor's (since this is a forward analysis) dataflow flags.
    void Merge(BasicBlock* block, BasicBlock* predBlock, unsigned dupCount)
    {
        ASSERT_TP pAssertionOut;

        if (predBlock->KindIs(BBJ_COND) && predBlock->TrueTargetIs(block))
        {
            pAssertionOut = mJumpDestOut[predBlock->bbNum];

            if (dupCount > 1)
            {
                // Scenario where next block and conditional block, both point to the same block.
                // In such case, intersect the assertions present on both the out edges of predBlock.
                assert(predBlock->FalseTargetIs(block));
                BitVecOps::IntersectionD(apTraits, pAssertionOut, predBlock->bbAssertionOut);

                if (VerboseDataflow())
                {
                    JITDUMP("Merge     : Duplicate flow, " FMT_BB " ", block->bbNum);
                    Compiler::optDumpAssertionIndices("in -> ", block->bbAssertionIn, "; ");
                    JITDUMP("pred " FMT_BB " ", predBlock->bbNum);
                    Compiler::optDumpAssertionIndices("out1 -> ", mJumpDestOut[predBlock->bbNum], "; ");
                    Compiler::optDumpAssertionIndices("out2 -> ", predBlock->bbAssertionOut, "\n");
                }
            }
        }
        else
        {
            pAssertionOut = predBlock->bbAssertionOut;
        }

        if (VerboseDataflow())
        {
            JITDUMP("Merge     : " FMT_BB " ", block->bbNum);
            Compiler::optDumpAssertionIndices("in -> ", block->bbAssertionIn, "; ");
            JITDUMP("pred " FMT_BB " ", predBlock->bbNum);
            Compiler::optDumpAssertionIndices("out -> ", pAssertionOut, "\n");
        }

        BitVecOps::IntersectionD(apTraits, block->bbAssertionIn, pAssertionOut);
    }

    //------------------------------------------------------------------------
    // MergeHandler: Merge assertions into the first exception handler/filter block.
    //
    // Arguments:
    //   block         - the block that is the start of a handler or filter;
    //   firstTryBlock - the first block of the try for "block" handler;
    //   lastTryBlock  - the last block of the try for "block" handler;.
    //
    // Notes:
    //   We can jump to the handler from any instruction in the try region. It
    //   means we can propagate only assertions that are valid for the whole
    //   try region.
    //
    //   It suffices to intersect with only the head 'try' block's assertions,
    //   since that block dominates all other blocks in the try, and since
    //   assertions are VN-based and can never become false.
    //
    void MergeHandler(BasicBlock* block, BasicBlock* firstTryBlock, BasicBlock* lastTryBlock)
    {
        if (VerboseDataflow())
        {
            JITDUMP("Merge     : " FMT_BB " ", block->bbNum);
            Compiler::optDumpAssertionIndices("in -> ", block->bbAssertionIn, "; ");
            JITDUMP("firstTryBlock " FMT_BB " ", firstTryBlock->bbNum);
            Compiler::optDumpAssertionIndices("in -> ", firstTryBlock->bbAssertionIn, "; ");
        }
        BitVecOps::IntersectionD(apTraits, block->bbAssertionIn, firstTryBlock->bbAssertionIn);
    }

    // At the end of the merge store results of the dataflow equations, in a postmerge state.
    bool EndMerge(BasicBlock* block)
    {
        if (VerboseDataflow())
        {
            JITDUMP("EndMerge  : " FMT_BB " ", block->bbNum);
            Compiler::optDumpAssertionIndices("in -> ", block->bbAssertionIn, "\n\n");
        }

        BitVecOps::DataFlowD(apTraits, block->bbAssertionOut, block->bbAssertionGen, block->bbAssertionIn);
        BitVecOps::DataFlowD(apTraits, mJumpDestOut[block->bbNum], mJumpDestGen[block->bbNum], block->bbAssertionIn);

        bool changed = (!BitVecOps::Equal(apTraits, preMergeOut, block->bbAssertionOut) ||
                        !BitVecOps::Equal(apTraits, preMergeJumpDestOut, mJumpDestOut[block->bbNum]));

        if (VerboseDataflow())
        {
            if (changed)
            {
                JITDUMP("Changed   : " FMT_BB " ", block->bbNum);
                Compiler::optDumpAssertionIndices("before out -> ", preMergeOut, "; ");
                Compiler::optDumpAssertionIndices("after out -> ", block->bbAssertionOut, ";\n        ");
                Compiler::optDumpAssertionIndices("jumpDest before out -> ", preMergeJumpDestOut, "; ");
                Compiler::optDumpAssertionIndices("jumpDest after out -> ", mJumpDestOut[block->bbNum], ";\n\n");
            }
            else
            {
                JITDUMP("Unchanged : " FMT_BB " ", block->bbNum);
                Compiler::optDumpAssertionIndices("out -> ", block->bbAssertionOut, "; ");
                Compiler::optDumpAssertionIndices("jumpDest out -> ", mJumpDestOut[block->bbNum], "\n\n");
            }
        }

        return changed;
    }

    // Can be enabled to get detailed debug output about dataflow for assertions.
    bool VerboseDataflow()
    {
#if 0
        return VERBOSE;
#endif
        return false;
    }
};

/*****************************************************************************
 *
 *   Compute the assertions generated by each block.
 */
ASSERT_TP* Compiler::optComputeAssertionGen()
{
    ASSERT_TP* jumpDestGen = fgAllocateTypeForEachBlk<ASSERT_TP>();

    for (BasicBlock* const block : Blocks())
    {
        ASSERT_TP valueGen = BitVecOps::MakeEmpty(apTraits);
        GenTree*  jtrue    = nullptr;

        // Walk the statement trees in this basic block.
        for (Statement* const stmt : block->Statements())
        {
            for (GenTree* const tree : stmt->TreeList())
            {
                if (tree->OperIs(GT_JTRUE))
                {
                    // A GT_TRUE is always the last node in a tree, so we can break here
                    assert((tree->gtNext == nullptr) && (stmt->GetNextStmt() == nullptr));
                    jtrue = tree;
                    break;
                }

                if (tree->GeneratesAssertion())
                {
                    AssertionInfo info = tree->GetAssertionInfo();
                    optImpliedAssertions(info.GetAssertionIndex(), valueGen);
                    BitVecOps::AddElemD(apTraits, valueGen, info.GetAssertionIndex() - 1);
                }
            }
        }

        if (jtrue != nullptr)
        {
            // Copy whatever we have accumulated into jumpDest edge's valueGen.
            ASSERT_TP jumpDestValueGen = BitVecOps::MakeCopy(apTraits, valueGen);

            if (jtrue->GeneratesAssertion())
            {
                AssertionInfo  info = jtrue->GetAssertionInfo();
                AssertionIndex valueAssertionIndex;
                AssertionIndex jumpDestAssertionIndex;

                if (info.AssertionHoldsOnFalseEdge())
                {
                    valueAssertionIndex    = info.GetAssertionIndex();
                    jumpDestAssertionIndex = optFindComplementary(info.GetAssertionIndex());
                }
                else // is jump edge assertion
                {
                    jumpDestAssertionIndex = info.GetAssertionIndex();
                    valueAssertionIndex    = optFindComplementary(jumpDestAssertionIndex);
                }

                if (valueAssertionIndex != NO_ASSERTION_INDEX)
                {
                    // Update valueGen if we have an assertion for the bbNext edge
                    optImpliedAssertions(valueAssertionIndex, valueGen);
                    BitVecOps::AddElemD(apTraits, valueGen, valueAssertionIndex - 1);
                }

                if (jumpDestAssertionIndex != NO_ASSERTION_INDEX)
                {
                    // Update jumpDestValueGen if we have an assertion for the bbTarget edge
                    optImpliedAssertions(jumpDestAssertionIndex, jumpDestValueGen);
                    BitVecOps::AddElemD(apTraits, jumpDestValueGen, jumpDestAssertionIndex - 1);
                }
            }

            jumpDestGen[block->bbNum] = jumpDestValueGen;
        }
        else
        {
            jumpDestGen[block->bbNum] = BitVecOps::MakeEmpty(apTraits);
        }

        block->bbAssertionGen = valueGen;

#ifdef DEBUG
        if (verbose)
        {
            if (block == fgFirstBB)
            {
                printf("\n");
            }

            printf(FMT_BB " valueGen = ", block->bbNum);
            optPrintAssertionIndices(block->bbAssertionGen);
            if (block->KindIs(BBJ_COND))
            {
                printf(" => " FMT_BB " valueGen = ", block->GetTrueTarget()->bbNum);
                optPrintAssertionIndices(jumpDestGen[block->bbNum]);
            }
            printf("\n");

            if (block == fgLastBB)
            {
                printf("\n");
            }
        }
#endif
    }

    return jumpDestGen;
}

/*****************************************************************************
 *
 *   Initialize the assertion data flow flags that will be propagated.
 */

ASSERT_TP* Compiler::optInitAssertionDataflowFlags()
{
    ASSERT_TP* jumpDestOut = fgAllocateTypeForEachBlk<ASSERT_TP>();

    // The local assertion gen phase may have created unreachable blocks.
    // They will never be visited in the dataflow propagation phase, so they need to
    // be initialized correctly. This means that instead of setting their sets to
    // apFull (i.e. all possible bits set), we need to set the bits only for valid
    // assertions (note that at this point we are not creating any new assertions).
    // Also note that assertion indices start from 1.
    ASSERT_TP apValidFull = BitVecOps::MakeEmpty(apTraits);
    for (int i = 1; i <= optAssertionCount; i++)
    {
        BitVecOps::AddElemD(apTraits, apValidFull, i - 1);
    }

    // Initially estimate the OUT sets to everything except killed expressions
    // Also set the IN sets to 1, so that we can perform the intersection.
    for (BasicBlock* const block : Blocks())
    {
        block->bbAssertionIn      = BitVecOps::MakeCopy(apTraits, apValidFull);
        block->bbAssertionGen     = BitVecOps::MakeEmpty(apTraits);
        block->bbAssertionOut     = BitVecOps::MakeCopy(apTraits, apValidFull);
        jumpDestOut[block->bbNum] = BitVecOps::MakeCopy(apTraits, apValidFull);
    }
    // Compute the data flow values for all tracked expressions
    // IN and OUT never change for the initial basic block B1
    BitVecOps::ClearD(apTraits, fgFirstBB->bbAssertionIn);
    return jumpDestOut;
}

// Callback data for the VN based constant prop visitor.
struct VNAssertionPropVisitorInfo
{
    Compiler*   pThis;
    Statement*  stmt;
    BasicBlock* block;
    VNAssertionPropVisitorInfo(Compiler* pThis, BasicBlock* block, Statement* stmt)
        : pThis(pThis)
        , stmt(stmt)
        , block(block)
    {
    }
};

//------------------------------------------------------------------------------
// optVNConstantPropOnJTrue
//    Constant propagate on the JTrue node.
//
// Arguments:
//    block - The block that contains the JTrue.
//    test  - The JTrue node whose relop evaluates to 0 or non-zero value.
//
// Return Value:
//    nullptr if no constant propagation is done, else the modified JTrue node
//    containing "0==0" or "0!=0" relop node
//    (where op1 is wrapped with side effects if any).
//
GenTree* Compiler::optVNConstantPropOnJTrue(BasicBlock* block, GenTree* test)
{
    GenTree* relop = test->gtGetOp1();

    // VN based assertion non-null on this relop has been performed.
    if (!relop->OperIsCompare())
    {
        return nullptr;
    }

    //
    // Make sure GTF_RELOP_JMP_USED flag is set so that we can later skip constant
    // prop'ing a JTRUE's relop child node for a second time in the pre-order
    // tree walk.
    //
    assert((relop->gtFlags & GTF_RELOP_JMP_USED) != 0);

    // We want to use the Normal ValueNumber when checking for constants.
    ValueNum vnCns = vnStore->VNConservativeNormalValue(relop->gtVNPair);
    if (!vnStore->IsVNConstant(vnCns))
    {
        return nullptr;
    }

    GenTree* sideEffects = gtWrapWithSideEffects(gtNewNothingNode(), relop);
    if (!sideEffects->IsNothingNode())
    {
        // Insert side effects before the JTRUE stmt.
        Statement* newStmt = fgNewStmtNearEnd(block, sideEffects);
        fgMorphBlockStmt(block, newStmt DEBUGARG(__FUNCTION__));
    }

    // Let's maintain the invariant that JTRUE's operand is always a relop.
    // and if we have side effects, we wrap one of the operands with them, not the relop.
    const bool evalsToTrue = (vnStore->CoercedConstantValue<INT64>(vnCns) != 0);
    test->AsOp()->gtOp1    = gtNewOperNode(evalsToTrue ? GT_EQ : GT_NE, relop->TypeGet(), gtNewFalse(), gtNewFalse());
    return test;
}

//------------------------------------------------------------------------------
// optVNBasedFoldCurStmt: Performs VN-based folding
//    on the current statement's tree nodes using VN.
//
// Assumption:
//    This function is called as part of a post-order tree walk.
//
// Arguments:
//    tree   - The currently visited tree node.
//    stmt   - The statement node in which the "tree" is present.
//    parent - The parent node of the tree.
//    block  - The block that contains the statement that contains the tree.
//
// Return Value:
//    Returns the standard visitor walk result.
//
Compiler::fgWalkResult Compiler::optVNBasedFoldCurStmt(BasicBlock* block,
                                                       Statement*  stmt,
                                                       GenTree*    parent,
                                                       GenTree*    tree)
{
    // Don't try and fold expressions marked with GTF_DONT_CSE
    // TODO-ASG: delete.
    if (!tree->CanCSE())
    {
        return WALK_CONTINUE;
    }

    // Don't propagate floating-point constants into a TYP_STRUCT LclVar
    // This can occur for HFA return values (see hfa_sf3E_r.exe)
    if (tree->TypeIs(TYP_STRUCT))
    {
        return WALK_CONTINUE;
    }

    switch (tree->OperGet())
    {
        // Make sure we have an R-value.
        case GT_ADD:
        case GT_SUB:
        case GT_DIV:
        case GT_MOD:
        case GT_UDIV:
        case GT_UMOD:
        case GT_EQ:
        case GT_NE:
        case GT_LT:
        case GT_LE:
        case GT_GE:
        case GT_GT:
        case GT_OR:
        case GT_XOR:
        case GT_AND:
        case GT_LSH:
        case GT_RSH:
        case GT_RSZ:
        case GT_NEG:
        case GT_CAST:
        case GT_BITCAST:
        case GT_INTRINSIC:
#ifdef FEATURE_HW_INTRINSICS
        case GT_HWINTRINSIC:
#endif // FEATURE_HW_INTRINSICS
        case GT_ARR_LENGTH:
            break;

        case GT_BLK:
        case GT_IND:
        {
            const ValueNum vn = tree->GetVN(VNK_Conservative);
            if (vnStore->VNNormalValue(vn) != vn)
            {
                return WALK_CONTINUE;
            }
        }
        break;

        case GT_JTRUE:
            break;

        case GT_MUL:
            // Don't transform long multiplies.
            if (tree->gtFlags & GTF_MUL_64RSLT)
            {
                return WALK_CONTINUE;
            }
            break;

        case GT_LCL_VAR:
        case GT_LCL_FLD:
            // Let's not conflict with CSE (to save the movw/movt).
            if (lclNumIsCSE(tree->AsLclVarCommon()->GetLclNum()))
            {
                return WALK_CONTINUE;
            }
            break;

        case GT_CALL:
            // The checks aren't for correctness, but to avoid unnecessary work.
            if (!tree->AsCall()->IsPure(this) && !tree->AsCall()->IsSpecialIntrinsic())
            {
                return WALK_CONTINUE;
            }
            break;

        default:
            // Unknown node, continue to walk.
            return WALK_CONTINUE;
    }

    // Perform the VN-based folding:
    GenTree* newTree = optVNBasedFoldExpr(block, parent, tree);

    if (newTree == nullptr)
    {
        // Not propagated, keep going.
        return WALK_CONTINUE;
    }

    optAssertionProp_Update(newTree, tree, stmt);

    JITDUMP("After VN-based fold of [%06u]:\n", tree->gtTreeID);
    DBEXEC(VERBOSE, gtDispStmt(stmt));

    return WALK_CONTINUE;
}

//------------------------------------------------------------------------------
// optVnNonNullPropCurStmt
//    Performs VN based non-null propagation on the tree node.
//
// Assumption:
//    This function is called as part of a pre-order tree walk.
//
// Arguments:
//    block - The block that contains the statement that contains the tree.
//    stmt  - The statement node in which the "tree" is present.
//    tree  - The currently visited tree node.
//
// Return Value:
//    None.
//
// Description:
//    Performs value number based non-null propagation on GT_CALL and
//    indirections. This is different from flow based assertions and helps
//    unify VN based constant prop and non-null prop in a single pre-order walk.
//
void Compiler::optVnNonNullPropCurStmt(BasicBlock* block, Statement* stmt, GenTree* tree)
{
    ASSERT_TP empty   = BitVecOps::UninitVal();
    GenTree*  newTree = nullptr;
    if (tree->OperIs(GT_CALL))
    {
        newTree = optNonNullAssertionProp_Call(empty, tree->AsCall());
    }
    else if (tree->OperIsIndir())
    {
        newTree = optAssertionProp_Ind(empty, tree, stmt);
    }
    if (newTree)
    {
        assert(newTree == tree);
        optAssertionProp_Update(newTree, tree, stmt);
    }
}

//------------------------------------------------------------------------------
// optVNAssertionPropCurStmtVisitor
//    Unified Value Numbering based assertion propagation visitor.
//
// Assumption:
//    This function is called as part of a post-order tree walk.
//
// Return Value:
//    WALK_RESULTs.
//
// Description:
//    An unified value numbering based assertion prop visitor that
//    performs non-null and constant assertion propagation based on
//    value numbers.
//
/* static */
Compiler::fgWalkResult Compiler::optVNAssertionPropCurStmtVisitor(GenTree** ppTree, fgWalkData* data)
{
    VNAssertionPropVisitorInfo* pData = (VNAssertionPropVisitorInfo*)data->pCallbackData;
    Compiler*                   pThis = pData->pThis;

    pThis->optVnNonNullPropCurStmt(pData->block, pData->stmt, *ppTree);

    return pThis->optVNBasedFoldCurStmt(pData->block, pData->stmt, data->parent, *ppTree);
}

/*****************************************************************************
 *
 *   Perform VN based i.e., data flow based assertion prop first because
 *   even if we don't gen new control flow assertions, we still propagate
 *   these first.
 *
 *   Returns the skipped next stmt if the current statement or next few
 *   statements got removed, else just returns the incoming stmt.
 */
Statement* Compiler::optVNAssertionPropCurStmt(BasicBlock* block, Statement* stmt)
{
    // TODO-Review: EH successor/predecessor iteration seems broken.
    // See: SELF_HOST_TESTS_ARM\jit\Directed\ExcepFilters\fault\fault.exe
    if (block->bbCatchTyp == BBCT_FAULT)
    {
        return stmt;
    }

    // Preserve the prev link before the propagation and morph.
    Statement* prev = (stmt == block->firstStmt()) ? nullptr : stmt->GetPrevStmt();

    // Perform VN based assertion prop first, in case we don't find
    // anything in assertion gen.
    optAssertionPropagatedCurrentStmt = false;

    VNAssertionPropVisitorInfo data(this, block, stmt);
    fgWalkTreePost(stmt->GetRootNodePointer(), Compiler::optVNAssertionPropCurStmtVisitor, &data);

    if (optAssertionPropagatedCurrentStmt)
    {
        fgMorphBlockStmt(block, stmt DEBUGARG("optVNAssertionPropCurStmt"));
    }

    // Check if propagation removed statements starting from current stmt.
    // If so, advance to the next good statement.
    Statement* nextStmt = (prev == nullptr) ? block->firstStmt() : prev->GetNextStmt();
    return nextStmt;
}

//------------------------------------------------------------------------------
// optAssertionPropMain: assertion propagation phase
//
// Returns:
//    Suitable phase status.
//
PhaseStatus Compiler::optAssertionPropMain()
{
    if (fgSsaPassesCompleted == 0)
    {
        return PhaseStatus::MODIFIED_NOTHING;
    }

    optAssertionInit(false);

    noway_assert(optAssertionCount == 0);
    bool madeChanges = false;

    // Assertion prop can speculatively create trees.
    INDEBUG(const unsigned baseTreeID = compGenTreeID);

    // First discover all assertions and record them in the table.
    ArrayStack<BasicBlock*> switchBlocks(getAllocator(CMK_AssertionProp));
    for (BasicBlock* const block : Blocks())
    {
        compCurBB           = block;
        fgRemoveRestOfBlock = false;

        Statement* stmt = block->firstStmt();
        while (stmt != nullptr)
        {
            // We need to remove the rest of the block.
            if (fgRemoveRestOfBlock)
            {
                fgRemoveStmt(block, stmt);
                stmt        = stmt->GetNextStmt();
                madeChanges = true;
                continue;
            }
            else
            {
                // Perform VN based assertion prop before assertion gen.
                Statement* nextStmt = optVNAssertionPropCurStmt(block, stmt);
                madeChanges |= optAssertionPropagatedCurrentStmt;
                INDEBUG(madeChanges |= (baseTreeID != compGenTreeID));

                // Propagation resulted in removal of the remaining stmts, perform it.
                if (fgRemoveRestOfBlock)
                {
                    stmt = stmt->GetNextStmt();
                    continue;
                }

                // Propagation removed the current stmt or next few stmts, so skip them.
                if (stmt != nextStmt)
                {
                    stmt = nextStmt;
                    continue;
                }
            }

            // Perform assertion gen for control flow based assertions.
            for (GenTree* const tree : stmt->TreeList())
            {
                optAssertionGen(tree);
            }

            // Advance the iterator
            stmt = stmt->GetNextStmt();
        }

        if (block->KindIs(BBJ_SWITCH))
        {
            switchBlocks.Push(block);
        }
    }

    for (int i = 0; i < switchBlocks.Height(); i++)
    {
        madeChanges |= optCreateJumpTableImpliedAssertions(switchBlocks.Bottom(i));
    }

    if (optAssertionCount == 0)
    {
        // Zero out the bbAssertionIn values, as these can be referenced in RangeCheck::MergeAssertion
        // and this is sharedstate with the CSE phase: bbCseIn
        //
        for (BasicBlock* const block : Blocks())
        {
            block->bbAssertionIn = BitVecOps::MakeEmpty(apTraits);
        }
        return madeChanges ? PhaseStatus::MODIFIED_EVERYTHING : PhaseStatus::MODIFIED_NOTHING;
    }

#ifdef DEBUG
    fgDebugCheckLinks();
#endif

    // Allocate the bits for the predicate sensitive dataflow analysis
    bbJtrueAssertionOut    = optInitAssertionDataflowFlags();
    ASSERT_TP* jumpDestGen = optComputeAssertionGen();

    // Modified dataflow algorithm for available expressions.
    DataFlow                  flow(this);
    AssertionPropFlowCallback ap(this, bbJtrueAssertionOut, jumpDestGen);
    if (ap.VerboseDataflow())
    {
        JITDUMP("AssertionPropFlowCallback:\n\n")
    }
    flow.ForwardAnalysis(ap);

    for (BasicBlock* const block : Blocks())
    {
        // Compute any implied non-Null assertions for block->bbAssertionIn
        optImpliedByTypeOfAssertions(block->bbAssertionIn);
    }

#ifdef DEBUG
    if (verbose)
    {
        for (BasicBlock* const block : Blocks())
        {
            printf(FMT_BB ":\n", block->bbNum);
            optDumpAssertionIndices(" in   = ", block->bbAssertionIn, "\n");
            optDumpAssertionIndices(" out  = ", block->bbAssertionOut, "\n");
            if (block->KindIs(BBJ_COND))
            {
                printf(" " FMT_BB " = ", block->GetTrueTarget()->bbNum);
                optDumpAssertionIndices(bbJtrueAssertionOut[block->bbNum], "\n");
            }
        }
        printf("\n");
    }
#endif // DEBUG

    ASSERT_TP assertions = BitVecOps::MakeEmpty(apTraits);

    // Perform assertion propagation (and constant folding)
    for (BasicBlock* const block : Blocks())
    {
        BitVecOps::Assign(apTraits, assertions, block->bbAssertionIn);

        // TODO-Review: EH successor/predecessor iteration seems broken.
        // SELF_HOST_TESTS_ARM\jit\Directed\ExcepFilters\fault\fault.exe
        if (block->bbCatchTyp == BBCT_FAULT)
        {
            continue;
        }

        // Make the current basic block address available globally.
        compCurBB           = block;
        fgRemoveRestOfBlock = false;

        // Walk the statement trees in this basic block
        Statement* stmt = block->FirstNonPhiDef();
        while (stmt != nullptr)
        {
            // Propagation tells us to remove the rest of the block. Remove it.
            if (fgRemoveRestOfBlock)
            {
                fgRemoveStmt(block, stmt);
                stmt        = stmt->GetNextStmt();
                madeChanges = true;
                continue;
            }

            // Preserve the prev link before the propagation and morph, to check if propagation
            // removes the current stmt.
            Statement* prevStmt = (stmt == block->firstStmt()) ? nullptr : stmt->GetPrevStmt();

            optAssertionPropagatedCurrentStmt = false; // set to true if a assertion propagation took place
                                                       // and thus we must morph, set order, re-link
            for (GenTree* tree = stmt->GetTreeList(); tree != nullptr; tree = tree->gtNext)
            {
                optDumpAssertionIndices("Propagating ", assertions, " ");
                JITDUMP("for " FMT_BB ", stmt " FMT_STMT ", tree [%06d]", block->bbNum, stmt->GetID(), dspTreeID(tree));
                JITDUMP(", tree -> ");
                JITDUMPEXEC(optPrintAssertionIndex(tree->GetAssertionInfo().GetAssertionIndex()));
                JITDUMP("\n");

                GenTree* newTree = optAssertionProp(assertions, tree, stmt, block);
                if (newTree)
                {
                    assert(optAssertionPropagatedCurrentStmt == true);
                    tree = newTree;
                }

                // If this tree makes an assertion - make it available.
                if (tree->GeneratesAssertion())
                {
                    AssertionInfo info = tree->GetAssertionInfo();
                    optImpliedAssertions(info.GetAssertionIndex(), assertions);
                    BitVecOps::AddElemD(apTraits, assertions, info.GetAssertionIndex() - 1);
                }
            }

            if (optAssertionPropagatedCurrentStmt)
            {
#ifdef DEBUG
                if (verbose)
                {
                    printf("Re-morphing this stmt:\n");
                    gtDispStmt(stmt);
                    printf("\n");
                }
#endif
                // Re-morph the statement.
                fgMorphBlockStmt(block, stmt DEBUGARG("optAssertionPropMain"));
                madeChanges = true;
            }

            // Check if propagation removed statements starting from current stmt.
            // If so, advance to the next good statement.
            Statement* nextStmt = (prevStmt == nullptr) ? block->firstStmt() : prevStmt->GetNextStmt();
            stmt                = (stmt == nextStmt) ? stmt->GetNextStmt() : nextStmt;
        }
        optAssertionPropagatedCurrentStmt = false; // clear it back as we are done with stmts.
    }

    return madeChanges ? PhaseStatus::MODIFIED_EVERYTHING : PhaseStatus::MODIFIED_NOTHING;
}
