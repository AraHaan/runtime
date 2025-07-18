﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Intrinsics;

namespace System.Numerics.Tensors
{
    public static partial class TensorPrimitives
    {
        /// <summary>Searches for the largest number in the specified tensor.</summary>
        /// <param name="x">The tensor, represented as a span.</param>
        /// <returns>The maximum element in <paramref name="x"/>.</returns>
        /// <exception cref="ArgumentException">Length of <paramref name="x" /> must be greater than zero.</exception>
        /// <remarks>
        /// <para>
        /// The determination of the maximum element matches the IEEE 754:2019 `maximum` function. If any value equal to <see cref="IFloatingPointIeee754{TSelf}.NaN"/>
        /// is present, the first is returned. Positive 0 is considered greater than negative 0.
        /// </para>
        /// <para>
        /// This method may call into the underlying C runtime or employ instructions specific to the current architecture. Exact results may differ between different
        /// operating systems or architectures.
        /// </para>
        /// </remarks>
        public static T Max<T>(ReadOnlySpan<T> x)
            where T : INumber<T>
        {
            if (typeof(T) == typeof(Half) && TryMinMaxHalfAsInt16<T, MaxOperator<float>>(x, out T result))
            {
                return result;
            }

            return MinMaxCore<T, MaxOperator<T>>(x);
        }

        /// <summary>Computes the element-wise maximum of the numbers in the specified tensors.</summary>
        /// <param name="x">The first tensor, represented as a span.</param>
        /// <param name="y">The second tensor, represented as a span.</param>
        /// <param name="destination">The destination tensor, represented as a span.</param>
        /// <exception cref="ArgumentException">Length of <paramref name="x" /> must be same as length of <paramref name="y" />.</exception>
        /// <exception cref="ArgumentException">Destination is too short.</exception>
        /// <exception cref="ArgumentException"><paramref name="x"/> and <paramref name="destination"/> reference overlapping memory locations and do not begin at the same location.</exception>
        /// <exception cref="ArgumentException"><paramref name="y"/> and <paramref name="destination"/> reference overlapping memory locations and do not begin at the same location.</exception>
        /// <remarks>
        /// <para>
        /// This method effectively computes <c><paramref name="destination" />[i] = <typeparamref name="T"/>.Max(<paramref name="x" />[i], <paramref name="y" />[i])</c>.
        /// </para>
        /// <para>
        /// The determination of the maximum element matches the IEEE 754:2019 `maximum` function. If either value is equal to <see cref="IFloatingPointIeee754{TSelf}.NaN"/>,
        /// that value is stored as the result. Positive 0 is considered greater than negative 0.
        /// </para>
        /// <para>
        /// This method may call into the underlying C runtime or employ instructions specific to the current architecture. Exact results may differ between different
        /// operating systems or architectures.
        /// </para>
        /// </remarks>
        public static void Max<T>(ReadOnlySpan<T> x, ReadOnlySpan<T> y, Span<T> destination)
            where T : INumber<T>
        {
            if (typeof(T) == typeof(Half) && TryAggregateInvokeHalfAsInt16<T, MaxOperator<float>>(x, y, destination))
            {
                return;
            }

            InvokeSpanSpanIntoSpan<T, MaxOperator<T>>(x, y, destination);
        }

        /// <summary>Computes the element-wise maximum of the numbers in the specified tensors.</summary>
        /// <param name="x">The first tensor, represented as a span.</param>
        /// <param name="y">The second tensor, represented as a scalar.</param>
        /// <param name="destination">The destination tensor, represented as a span.</param>
        /// <exception cref="ArgumentException">Destination is too short.</exception>
        /// <exception cref="ArgumentException"><paramref name="x"/> and <paramref name="destination"/> reference overlapping memory locations and do not begin at the same location.</exception>
        /// <remarks>
        /// <para>
        /// This method effectively computes <c><paramref name="destination" />[i] = <typeparamref name="T"/>.Max(<paramref name="x" />[i], <paramref name="y" />)</c>.
        /// </para>
        /// <para>
        /// The determination of the maximum element matches the IEEE 754:2019 `maximum` function. If either value is equal to <see cref="IFloatingPointIeee754{TSelf}.NaN"/>,
        /// that value is stored as the result. Positive 0 is considered greater than negative 0.
        /// </para>
        /// <para>
        /// This method may call into the underlying C runtime or employ instructions specific to the current architecture. Exact results may differ between different
        /// operating systems or architectures.
        /// </para>
        /// </remarks>
        public static void Max<T>(ReadOnlySpan<T> x, T y, Span<T> destination)
            where T : INumber<T>
        {
            if (typeof(T) == typeof(Half) && TryAggregateInvokeHalfAsInt16<T, MaxOperator<float>>(x, y, destination))
            {
                return;
            }

            InvokeSpanScalarIntoSpan<T, MaxOperator<T>>(x, y, destination);
        }

        /// <summary>Max(x, y)</summary>
        internal readonly struct MaxOperator<T> : IAggregationOperator<T>
             where T : INumber<T>
        {
            public static bool Vectorizable => true;

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public static T Invoke(T x, T y) => T.Max(x, y);

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public static Vector128<T> Invoke(Vector128<T> x, Vector128<T> y)
            {
#if !NET9_0_OR_GREATER
                if ((typeof(T) == typeof(float)) || (typeof(T) == typeof(double)))
                {
                    return Vector128.ConditionalSelect(
                        Vector128.LessThan(y, x) | IsNaN(x) | (Vector128.Equals(x, y) & IsNegative(y)),
                        x,
                        y
                    );
                }
#endif

                return Vector128.Max(x, y);
            }

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public static Vector256<T> Invoke(Vector256<T> x, Vector256<T> y)
            {
#if !NET9_0_OR_GREATER
                if ((typeof(T) == typeof(float)) || (typeof(T) == typeof(double)))
                {
                    return Vector256.ConditionalSelect(
                        Vector256.LessThan(y, x) | IsNaN(x) | (Vector256.Equals(x, y) & IsNegative(y)),
                        x,
                        y
                    );
                }
#endif

                return Vector256.Max(x, y);
            }

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public static Vector512<T> Invoke(Vector512<T> x, Vector512<T> y)
            {
#if !NET9_0_OR_GREATER
                if ((typeof(T) == typeof(float)) || (typeof(T) == typeof(double)))
                {
                    return Vector512.ConditionalSelect(
                        Vector512.LessThan(y, x) | IsNaN(x) | (Vector512.Equals(x, y) & IsNegative(y)),
                        x,
                        y
                    );
                }
#endif

                return Vector512.Max(x, y);
            }

            public static T Invoke(Vector128<T> x) => HorizontalAggregate<T, MaxOperator<T>>(x);
            public static T Invoke(Vector256<T> x) => HorizontalAggregate<T, MaxOperator<T>>(x);
            public static T Invoke(Vector512<T> x) => HorizontalAggregate<T, MaxOperator<T>>(x);
        }

        /// <summary>Gets whether each specified <see cref="float"/> is NaN.</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static Vector128<T> IsNaN<T>(Vector128<T> vector)
        {
#if NET9_0_OR_GREATER
            return Vector128.IsNaN(vector);
#else
            if ((typeof(T) == typeof(float)) || (typeof(T) == typeof(double)))
            {
                return ~Vector128.Equals(vector, vector);
            }
            return Vector128<T>.Zero;
#endif
        }

        /// <summary>Gets whether each specified <see cref="float"/> is NaN.</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static Vector256<T> IsNaN<T>(Vector256<T> vector)
        {
#if NET9_0_OR_GREATER
            return Vector256.IsNaN(vector);
#else
            if ((typeof(T) == typeof(float)) || (typeof(T) == typeof(double)))
            {
                return ~Vector256.Equals(vector, vector);
            }
            return Vector256<T>.Zero;
#endif
        }

        /// <summary>Gets whether each specified <see cref="float"/> is NaN.</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static Vector512<T> IsNaN<T>(Vector512<T> vector)
        {
#if NET9_0_OR_GREATER
            return Vector512.IsNaN(vector);
#else
            if ((typeof(T) == typeof(float)) || (typeof(T) == typeof(double)))
            {
                return ~Vector512.Equals(vector, vector);
            }
            return Vector512<T>.Zero;
#endif
        }

        /// <summary>Gets whether each specified <see cref="float"/> is negative.</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static Vector128<T> IsNegative<T>(Vector128<T> vector)
        {
#if NET9_0_OR_GREATER
            return Vector128.IsNegative(vector);
#else
            if ((typeof(T) == typeof(byte))
             || (typeof(T) == typeof(ushort))
             || (typeof(T) == typeof(uint))
             || (typeof(T) == typeof(ulong))
             || (typeof(T) == typeof(nuint)))
            {
                return Vector128<T>.Zero;
            }

            if (typeof(T) == typeof(float))
            {
                return Vector128.LessThan(vector.AsInt32(), Vector128<int>.Zero).As<int, T>();
            }

            if (typeof(T) == typeof(double))
            {
                return Vector128.LessThan(vector.AsInt64(), Vector128<long>.Zero).As<long, T>();
            }

            return Vector128.LessThan(vector, Vector128<T>.Zero);
#endif
        }

        /// <summary>Gets whether each specified <see cref="float"/> is negative.</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static Vector256<T> IsNegative<T>(Vector256<T> vector)
        {
#if NET9_0_OR_GREATER
            return Vector256.IsNegative(vector);
#else
            if ((typeof(T) == typeof(byte))
             || (typeof(T) == typeof(ushort))
             || (typeof(T) == typeof(uint))
             || (typeof(T) == typeof(ulong))
             || (typeof(T) == typeof(nuint)))
            {
                return Vector256<T>.Zero;
            }

            if (typeof(T) == typeof(float))
            {
                return Vector256.LessThan(vector.AsInt32(), Vector256<int>.Zero).As<int, T>();
            }

            if (typeof(T) == typeof(double))
            {
                return Vector256.LessThan(vector.AsInt64(), Vector256<long>.Zero).As<long, T>();
            }

            return Vector256.LessThan(vector, Vector256<T>.Zero);
#endif
        }

        /// <summary>Gets whether each specified <see cref="float"/> is negative.</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static Vector512<T> IsNegative<T>(Vector512<T> vector)
        {
#if NET9_0_OR_GREATER
            return Vector512.IsNegative(vector);
#else
            if ((typeof(T) == typeof(byte))
             || (typeof(T) == typeof(ushort))
             || (typeof(T) == typeof(uint))
             || (typeof(T) == typeof(ulong))
             || (typeof(T) == typeof(nuint)))
            {
                return Vector512<T>.Zero;
            }

            if (typeof(T) == typeof(float))
            {
                return Vector512.LessThan(vector.AsInt32(), Vector512<int>.Zero).As<int, T>();
            }

            if (typeof(T) == typeof(double))
            {
                return Vector512.LessThan(vector.AsInt64(), Vector512<long>.Zero).As<long, T>();
            }

            return Vector512.LessThan(vector, Vector512<T>.Zero);
#endif
        }

        /// <summary>Gets whether each specified <see cref="float"/> is positive.</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static Vector128<T> IsPositive<T>(Vector128<T> vector)
        {
#if NET9_0_OR_GREATER
            return Vector128.IsPositive(vector);
#else
            if ((typeof(T) == typeof(byte))
             || (typeof(T) == typeof(ushort))
             || (typeof(T) == typeof(uint))
             || (typeof(T) == typeof(ulong))
             || (typeof(T) == typeof(nuint)))
            {
                return Vector128<T>.AllBitsSet;
            }

            if (typeof(T) == typeof(float))
            {
                return Vector128.GreaterThanOrEqual(vector.AsInt32(), Vector128<int>.Zero).As<int, T>();
            }

            if (typeof(T) == typeof(double))
            {
                return Vector128.GreaterThanOrEqual(vector.AsInt64(), Vector128<long>.Zero).As<long, T>();
            }

            return Vector128.GreaterThanOrEqual(vector, Vector128<T>.Zero);
#endif
        }

        /// <summary>Gets whether each specified <see cref="float"/> is positive.</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static Vector256<T> IsPositive<T>(Vector256<T> vector)
        {
#if NET9_0_OR_GREATER
            return Vector256.IsPositive(vector);
#else
            if ((typeof(T) == typeof(byte))
             || (typeof(T) == typeof(ushort))
             || (typeof(T) == typeof(uint))
             || (typeof(T) == typeof(ulong))
             || (typeof(T) == typeof(nuint)))
            {
                return Vector256<T>.AllBitsSet;
            }

            if (typeof(T) == typeof(float))
            {
                return Vector256.GreaterThanOrEqual(vector.AsInt32(), Vector256<int>.Zero).As<int, T>();
            }

            if (typeof(T) == typeof(double))
            {
                return Vector256.GreaterThanOrEqual(vector.AsInt64(), Vector256<long>.Zero).As<long, T>();
            }

            return Vector256.GreaterThanOrEqual(vector, Vector256<T>.Zero);
#endif
        }

        /// <summary>Gets whether each specified <see cref="float"/> is positive.</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static Vector512<T> IsPositive<T>(Vector512<T> vector)
        {
#if NET9_0_OR_GREATER
            return Vector512.IsPositive(vector);
#else
            if ((typeof(T) == typeof(byte))
             || (typeof(T) == typeof(ushort))
             || (typeof(T) == typeof(uint))
             || (typeof(T) == typeof(ulong))
             || (typeof(T) == typeof(nuint)))
            {
                return Vector512<T>.AllBitsSet;
            }

            if (typeof(T) == typeof(float))
            {
                return Vector512.GreaterThanOrEqual(vector.AsInt32(), Vector512<int>.Zero).As<int, T>();
            }

            if (typeof(T) == typeof(double))
            {
                return Vector512.GreaterThanOrEqual(vector.AsInt64(), Vector512<long>.Zero).As<long, T>();
            }

            return Vector512.GreaterThanOrEqual(vector, Vector512<T>.Zero);
#endif
        }

            /// <remarks>
            /// This is the same as <see cref="Aggregate{T, TTransformOperator, TAggregationOperator}(ReadOnlySpan{T})"/>
            /// with an identity transform, except it early exits on NaN.
            /// </remarks>
            private static T MinMaxCore<T, TMinMaxOperator>(ReadOnlySpan<T> x)
            where T : INumberBase<T>
            where TMinMaxOperator : struct, IAggregationOperator<T>
        {
            if (x.IsEmpty)
            {
                ThrowHelper.ThrowArgument_SpansMustBeNonEmpty();
            }

            // This matches the IEEE 754:2019 `maximum`/`minimum` functions.
            // It propagates NaN inputs back to the caller and
            // otherwise returns the greater of the inputs.
            // It treats +0 as greater than -0 as per the specification.

            if (Vector512.IsHardwareAccelerated && Vector512<T>.IsSupported && x.Length >= Vector512<T>.Count)
            {
                ref T xRef = ref MemoryMarshal.GetReference(x);

                // Load the first vector as the initial set of results, and bail immediately
                // to scalar handling if it contains any NaNs (which don't compare equally to themselves).
                Vector512<T> result = Vector512.LoadUnsafe(ref xRef, 0);
                Vector512<T> current;

                Vector512<T> nanMask;
                if (typeof(T) == typeof(float) || typeof(T) == typeof(double))
                {
                    // Check for NaNs
                    nanMask = IsNaN(result);
                    if (nanMask != Vector512<T>.Zero)
                    {
                        return result.GetElement(IndexOfFirstMatch(nanMask));
                    }
                }

                int oneVectorFromEnd = x.Length - Vector512<T>.Count;
                int i = Vector512<T>.Count;

                // Aggregate additional vectors into the result as long as there's at least one full vector left to process.
                while (i <= oneVectorFromEnd)
                {
                    // Load the next vector, and early exit on NaN.
                    current = Vector512.LoadUnsafe(ref xRef, (uint)i);

                    if (typeof(T) == typeof(float) || typeof(T) == typeof(double))
                    {
                        // Check for NaNs
                        nanMask = ~Vector512.Equals(current, current);
                        if (nanMask != Vector512<T>.Zero)
                        {
                            return current.GetElement(IndexOfFirstMatch(nanMask));
                        }
                    }

                    result = TMinMaxOperator.Invoke(result, current);
                    i += Vector512<T>.Count;
                }

                // If any elements remain, handle them in one final vector.
                if (i != x.Length)
                {
                    current = Vector512.LoadUnsafe(ref xRef, (uint)(x.Length - Vector512<T>.Count));

                    if (typeof(T) == typeof(float) || typeof(T) == typeof(double))
                    {
                        // Check for NaNs
                        nanMask = ~Vector512.Equals(current, current);
                        if (nanMask != Vector512<T>.Zero)
                        {
                            return current.GetElement(IndexOfFirstMatch(nanMask));
                        }
                    }

                    result = TMinMaxOperator.Invoke(result, current);
                }

                // Aggregate the lanes in the vector to create the final scalar result.
                return TMinMaxOperator.Invoke(result);
            }

            if (Vector256.IsHardwareAccelerated && Vector256<T>.IsSupported && x.Length >= Vector256<T>.Count)
            {
                ref T xRef = ref MemoryMarshal.GetReference(x);

                // Load the first vector as the initial set of results, and bail immediately
                // to scalar handling if it contains any NaNs (which don't compare equally to themselves).
                Vector256<T> result = Vector256.LoadUnsafe(ref xRef, 0);
                Vector256<T> current;

                Vector256<T> nanMask;
                if (typeof(T) == typeof(float) || typeof(T) == typeof(double))
                {
                    // Check for NaNs
                    nanMask = ~Vector256.Equals(result, result);
                    if (nanMask != Vector256<T>.Zero)
                    {
                        return result.GetElement(IndexOfFirstMatch(nanMask));
                    }
                }

                int oneVectorFromEnd = x.Length - Vector256<T>.Count;
                int i = Vector256<T>.Count;

                // Aggregate additional vectors into the result as long as there's at least one full vector left to process.
                while (i <= oneVectorFromEnd)
                {
                    // Load the next vector, and early exit on NaN.
                    current = Vector256.LoadUnsafe(ref xRef, (uint)i);

                    if (typeof(T) == typeof(float) || typeof(T) == typeof(double))
                    {
                        // Check for NaNs
                        nanMask = ~Vector256.Equals(current, current);
                        if (nanMask != Vector256<T>.Zero)
                        {
                            return current.GetElement(IndexOfFirstMatch(nanMask));
                        }
                    }

                    result = TMinMaxOperator.Invoke(result, current);
                    i += Vector256<T>.Count;
                }

                // If any elements remain, handle them in one final vector.
                if (i != x.Length)
                {
                    current = Vector256.LoadUnsafe(ref xRef, (uint)(x.Length - Vector256<T>.Count));


                    if (typeof(T) == typeof(float) || typeof(T) == typeof(double))
                    {
                        // Check for NaNs
                        nanMask = ~Vector256.Equals(current, current);
                        if (nanMask != Vector256<T>.Zero)
                        {
                            return current.GetElement(IndexOfFirstMatch(nanMask));
                        }
                    }

                    result = TMinMaxOperator.Invoke(result, current);
                }

                // Aggregate the lanes in the vector to create the final scalar result.
                return TMinMaxOperator.Invoke(result);
            }

            if (Vector128.IsHardwareAccelerated && Vector128<T>.IsSupported && x.Length >= Vector128<T>.Count)
            {
                ref T xRef = ref MemoryMarshal.GetReference(x);

                // Load the first vector as the initial set of results, and bail immediately
                // to scalar handling if it contains any NaNs (which don't compare equally to themselves).
                Vector128<T> result = Vector128.LoadUnsafe(ref xRef, 0);
                Vector128<T> current;

                Vector128<T> nanMask;
                if (typeof(T) == typeof(float) || typeof(T) == typeof(double))
                {
                    // Check for NaNs
                    nanMask = IsNaN(result);
                    if (nanMask != Vector128<T>.Zero)
                    {
                        return result.GetElement(IndexOfFirstMatch(nanMask));
                    }
                }

                int oneVectorFromEnd = x.Length - Vector128<T>.Count;
                int i = Vector128<T>.Count;

                // Aggregate additional vectors into the result as long as there's at least one full vector left to process.
                while (i <= oneVectorFromEnd)
                {
                    // Load the next vector, and early exit on NaN.
                    current = Vector128.LoadUnsafe(ref xRef, (uint)i);

                    if (typeof(T) == typeof(float) || typeof(T) == typeof(double))
                    {
                        // Check for NaNs
                        nanMask = IsNaN(current);
                        if (nanMask != Vector128<T>.Zero)
                        {
                            return current.GetElement(IndexOfFirstMatch(nanMask));
                        }
                    }

                    result = TMinMaxOperator.Invoke(result, current);
                    i += Vector128<T>.Count;
                }

                // If any elements remain, handle them in one final vector.
                if (i != x.Length)
                {
                    current = Vector128.LoadUnsafe(ref xRef, (uint)(x.Length - Vector128<T>.Count));

                    if (typeof(T) == typeof(float) || typeof(T) == typeof(double))
                    {
                        // Check for NaNs
                        nanMask = IsNaN(current);
                        if (nanMask != Vector128<T>.Zero)
                        {
                            return current.GetElement(IndexOfFirstMatch(nanMask));
                        }
                    }

                    result = TMinMaxOperator.Invoke(result, current);
                }

                // Aggregate the lanes in the vector to create the final scalar result.
                return TMinMaxOperator.Invoke(result);
            }

            // Scalar path used when either vectorization is not supported or the input is too small to vectorize.
            T curResult = x[0];
            if (T.IsNaN(curResult))
            {
                return curResult;
            }

            for (int i = 1; i < x.Length; i++)
            {
                T current = x[i];
                if (T.IsNaN(current))
                {
                    return current;
                }

                curResult = TMinMaxOperator.Invoke(curResult, current);
            }

            return curResult;
        }
    }
}
