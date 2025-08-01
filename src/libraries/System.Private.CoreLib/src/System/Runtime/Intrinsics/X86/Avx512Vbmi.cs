// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using System.Runtime.Intrinsics;

namespace System.Runtime.Intrinsics.X86
{
    /// <summary>Provides access to X86 AVX512VBMI hardware instructions via intrinsics.</summary>
    [Intrinsic]
    [CLSCompliant(false)]
    public abstract class Avx512Vbmi : Avx512BW
    {
        internal Avx512Vbmi() { }

        /// <summary>Gets a value that indicates whether the APIs in this class are supported.</summary>
        /// <value><see langword="true" /> if the APIs are supported; otherwise, <see langword="false" />.</value>
        /// <remarks>A value of <see langword="false" /> indicates that the APIs will throw <see cref="PlatformNotSupportedException" />.</remarks>
        public static new bool IsSupported { get => IsSupported; }

        /// <summary>Provides access to the x86 AVX512VBMI+VL hardware instructions via intrinsics.</summary>
        [Intrinsic]
        public new abstract class VL : Avx512BW.VL
        {
            internal VL() { }

            /// <summary>Gets a value that indicates whether the APIs in this class are supported.</summary>
            /// <value><see langword="true" /> if the APIs are supported; otherwise, <see langword="false" />.</value>
            /// <remarks>A value of <see langword="false" /> indicates that the APIs will throw <see cref="PlatformNotSupportedException" />.</remarks>
            public static new bool IsSupported { get => IsSupported; }

            /// <summary>
            ///   <para>__m128i _mm_multishift_epi64_epi8(__m128i a, __m128i b)</para>
            ///   <para>  VPMULTISHIFTQB xmm1 {k1}{z}, xmm2, xmm3/m128/m64bcst</para>
            /// </summary>
            public static Vector128<byte> MultiShift(Vector128<byte> control, Vector128<ulong> value) => MultiShift(control, value);
            /// <summary>
            ///   <para>__m128i _mm_multishift_epi64_epi8(__m128i a, __m128i b)</para>
            ///   <para>  VPMULTISHIFTQB xmm1 {k1}{z}, xmm2, xmm3/m128/m64bcst</para>
            /// </summary>
            public static Vector128<sbyte> MultiShift(Vector128<sbyte> control, Vector128<long> value) => MultiShift(control, value);

            /// <summary>
            ///   <para>__m256i _mm256_multishift_epi64_epi8(__m256i a, __m256i b)</para>
            ///   <para>  VPMULTISHIFTQB ymm1 {k1}{z}, ymm2, ymm3/m256/m64bcst</para>
            /// </summary>
            public static Vector256<byte> MultiShift(Vector256<byte> control, Vector256<ulong> value) => MultiShift(control, value);
            /// <summary>
            ///   <para>__m256i _mm256_multishift_epi64_epi8(__m256i a, __m256i b)</para>
            ///   <para>  VPMULTISHIFTQB ymm1 {k1}{z}, ymm2, ymm3/m256/m64bcst</para>
            /// </summary>
            public static Vector256<sbyte> MultiShift(Vector256<sbyte> control, Vector256<long> value) => MultiShift(control, value);

            /// <summary>
            ///   <para>__m128i _mm_permutexvar_epi8 (__m128i idx, __m128i a)</para>
            ///   <para>  VPERMB xmm1 {k1}{z}, xmm2, xmm3/m128</para>
            /// </summary>
            /// <remarks>The native and managed intrinsics have different order of parameters.</remarks>
            public static Vector128<sbyte> PermuteVar16x8(Vector128<sbyte> left, Vector128<sbyte> control) => PermuteVar16x8(left, control);
            /// <summary>
            ///   <para>__m128i _mm_permutexvar_epi8 (__m128i idx, __m128i a)</para>
            ///   <para>  VPERMB xmm1 {k1}{z}, xmm2, xmm3/m128</para>
            /// </summary>
            /// <remarks>The native and managed intrinsics have different order of parameters.</remarks>
            public static Vector128<byte> PermuteVar16x8(Vector128<byte> left, Vector128<byte> control) => PermuteVar16x8(left, control);

            /// <summary>
            ///   <para>__m128i _mm_permutex2var_epi8 (__m128i a, __m128i idx, __m128i b)</para>
            ///   <para>  VPERMI2B xmm1 {k1}{z}, xmm2, xmm3/m128</para>
            ///   <para>  VPERMT2B xmm1 {k1}{z}, xmm2, xmm3/m128</para>
            /// </summary>
            public static Vector128<byte> PermuteVar16x8x2(Vector128<byte> lower, Vector128<byte> indices, Vector128<byte> upper) => PermuteVar16x8x2(lower, indices, upper);
            /// <summary>
            ///   <para>__m128i _mm_permutex2var_epi8 (__m128i a, __m128i idx, __m128i b)</para>
            ///   <para>  VPERMI2B xmm1 {k1}{z}, xmm2, xmm3/m128</para>
            ///   <para>  VPERMT2B xmm1 {k1}{z}, xmm2, xmm3/m128</para>
            /// </summary>
            public static Vector128<sbyte> PermuteVar16x8x2(Vector128<sbyte> lower, Vector128<sbyte> indices, Vector128<sbyte> upper) => PermuteVar16x8x2(lower, indices, upper);

            /// <summary>
            ///   <para>__m256i _mm256_permutexvar_epi8 (__m256i idx, __m256i a)</para>
            ///   <para>  VPERMB ymm1 {k1}{z}, ymm2, ymm3/m256</para>
            /// </summary>
            /// <remarks>The native and managed intrinsics have different order of parameters.</remarks>
            public static Vector256<sbyte> PermuteVar32x8(Vector256<sbyte> left, Vector256<sbyte> control) => PermuteVar32x8(left, control);
            /// <summary>
            ///   <para>__m256i _mm256_permutexvar_epi8 (__m256i idx, __m256i a)</para>
            ///   <para>  VPERMB ymm1 {k1}{z}, ymm2, ymm3/m256</para>
            /// </summary>
            /// <remarks>The native and managed intrinsics have different order of parameters.</remarks>
            public static Vector256<byte> PermuteVar32x8(Vector256<byte> left, Vector256<byte> control) => PermuteVar32x8(left, control);

            /// <summary>
            ///   <para>__m256i _mm256_permutex2var_epi8 (__m256i a, __m256i idx, __m256i b)</para>
            ///   <para>  VPERMI2B ymm1 {k1}{z}, ymm2, ymm3/m256</para>
            ///   <para>  VPERMT2B ymm1 {k1}{z}, ymm2, ymm3/m256</para>
            /// </summary>
            public static Vector256<byte> PermuteVar32x8x2(Vector256<byte> lower, Vector256<byte> indices, Vector256<byte> upper) => PermuteVar32x8x2(lower, indices, upper);
            /// <summary>
            ///   <para>__m256i _mm256_permutex2var_epi8 (__m256i a, __m256i idx, __m256i b)</para>
            ///   <para>  VPERMI2B ymm1 {k1}{z}, ymm2, ymm3/m256</para>
            ///   <para>  VPERMT2B ymm1 {k1}{z}, ymm2, ymm3/m256</para>
            /// </summary>
            public static Vector256<sbyte> PermuteVar32x8x2(Vector256<sbyte> lower, Vector256<sbyte> indices, Vector256<sbyte> upper) => PermuteVar32x8x2(lower, indices, upper);
        }

        /// <summary>Provides access to the x86 AVX512VBMI hardware instructions, that are only available to 64-bit processes, via intrinsics.</summary>
        [Intrinsic]
        public new abstract class X64 : Avx512BW.X64
        {
            internal X64() { }

            /// <summary>Gets a value that indicates whether the APIs in this class are supported.</summary>
            /// <value><see langword="true" /> if the APIs are supported; otherwise, <see langword="false" />.</value>
            /// <remarks>A value of <see langword="false" /> indicates that the APIs will throw <see cref="PlatformNotSupportedException" />.</remarks>
            public static new bool IsSupported { get => IsSupported; }
        }

        /// <summary>
        ///   <para>__m512i _mm512_multishift_epi64_epi8(__m512i a, __m512i b)</para>
        ///   <para>  VPMULTISHIFTQB zmm1 {k1}{z}, zmm2, zmm3/m512/m64bcst</para>
        /// </summary>
        public static Vector512<byte> MultiShift(Vector512<byte> control, Vector512<ulong> value) => MultiShift(control, value);
        /// <summary>
        ///   <para>__m512i _mm512_multishift_epi64_epi8(__m512i a, __m512i b)</para>
        ///   <para>  VPMULTISHIFTQB zmm1 {k1}{z}, zmm2, zmm3/m512/m64bcst</para>
        /// </summary>
        public static Vector512<sbyte> MultiShift(Vector512<sbyte> control, Vector512<long> value) => MultiShift(control, value);

        /// <summary>
        ///   <para>__m512i _mm512_permutexvar_epi8 (__m512i idx, __m512i a)</para>
        ///   <para>  VPERMB zmm1 {k1}{z}, zmm2, zmm3/m512</para>
        /// </summary>
        /// <remarks>The native and managed intrinsics have different order of parameters.</remarks>
        public static Vector512<sbyte> PermuteVar64x8(Vector512<sbyte> left, Vector512<sbyte> control) => PermuteVar64x8(left, control);
        /// <summary>
        ///   <para>__m512i _mm512_permutexvar_epi8 (__m512i idx, __m512i a)</para>
        ///   <para>  VPERMB zmm1 {k1}{z}, zmm2, zmm3/m512</para>
        /// </summary>
        /// <remarks>The native and managed intrinsics have different order of parameters.</remarks>
        public static Vector512<byte> PermuteVar64x8(Vector512<byte> left, Vector512<byte> control) => PermuteVar64x8(left, control);

        /// <summary>
        ///   <para>__m512i _mm512_permutex2var_epi8 (__m512i a, __m512i idx, __m512i b)</para>
        ///   <para>  VPERMI2B zmm1 {k1}{z}, zmm2, zmm3/m512</para>
        ///   <para>  VPERMT2B zmm1 {k1}{z}, zmm2, zmm3/m512</para>
        /// </summary>
        public static Vector512<byte> PermuteVar64x8x2(Vector512<byte> lower, Vector512<byte> indices, Vector512<byte> upper) => PermuteVar64x8x2(lower, indices, upper);
        /// <summary>
        ///   <para>__m512i _mm512_permutex2var_epi8 (__m512i a, __m512i idx, __m512i b)</para>
        ///   <para>  VPERMI2B zmm1 {k1}{z}, zmm2, zmm3/m512</para>
        ///   <para>  VPERMT2B zmm1 {k1}{z}, zmm2, zmm3/m512</para>
        /// </summary>
        public static Vector512<sbyte> PermuteVar64x8x2(Vector512<sbyte> lower, Vector512<sbyte> indices, Vector512<sbyte> upper) => PermuteVar64x8x2(lower, indices, upper);
    }
}
