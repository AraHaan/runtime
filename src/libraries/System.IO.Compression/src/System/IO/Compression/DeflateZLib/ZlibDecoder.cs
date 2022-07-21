// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Buffers;

namespace System.IO.Compression
{
    public class ZlibDecoder
    {
        public ZlibOptions? Options { get; set; }

        // caller is responsible for disposing of the result.
        public bool TryDecompress(ref ZlibResult zlibResult, ReadOnlySpan<byte> source, Span<byte> dest)
        {
            Decompress(ref zlibResult, source, dest, false);
            return zlibResult.Status == OperationStatus.Done;
        }

        public void Decompress(ref ZlibResult zlibResult, ReadOnlySpan<byte> source, Span<byte> dest)
            => Decompress(ref zlibResult, source, dest, true);

#pragma warning disable CS3002 // Return type is not CLS-compliant
        // when window bits is >= 25 and <= 31 returns a Crc-32 checksum
        // otherwise returns an Adler-32 checksum.
        public uint CalculateChecksum(ReadOnlySpan<byte> source)
#pragma warning restore CS3002 // Return type is not CLS-compliant
            => ZlibEncoder.CalculateChecksumCore(source, Options);

        internal unsafe OperationStatus InflateCore(ref ZlibResult zlibResult, ReadOnlySpan<byte> source, Span<byte> dest, ZLibNative.FlushCode flush, bool throwOnError)
        {
            fixed (byte* sourcePtr = source)
            fixed (byte* destPtr = dest)
            {
                zlibResult.stream!.NextIn = (IntPtr)sourcePtr;
                zlibResult.stream.AvailIn = (uint)source.Length;
                zlibResult.stream.NextOut = (IntPtr)destPtr;
                zlibResult.stream.AvailOut = (uint)dest.Length;
                return Inflate(ref zlibResult, flush, throwOnError);
            }
        }

        private unsafe void Decompress(ref ZlibResult zlibResult, ReadOnlySpan<byte> source, Span<byte> dest, bool throwOnError)
        {
            bool skip = zlibResult.IsDisposed || Options?.CompressionMode == CompressionMode.Compress || source.Length >= dest.Length;
            OperationStatus status = (zlibResult.IsDisposed, Options?.CompressionMode == CompressionMode.Compress, source.Length >= dest.Length) switch
            {
                (true, false, false) or (true, true, false) or (false, true, false) or (true, false, true) or (true, true, true) or (false, true, true) => OperationStatus.Error,
                (false, false, true) => OperationStatus.DestinationTooSmall,
                _ => OperationStatus.Done,
            };
            if (!skip)
            {
                status = zlibResult.InflateInit(throwOnError, Options!);
                if (status is OperationStatus.Error)
                {
                    skip = true;
                }
            }

            if (!skip)
            {
                status = InflateCore(ref zlibResult, source, dest, (ZLibNative.FlushCode)Options!.FlushMode, throwOnError);
                status = status == OperationStatus.Done ? Inflate(ref zlibResult, ZLibNative.FlushCode.Finish, throwOnError) : status;
                zlibResult.LastBytesWritten = status != OperationStatus.Done ? default : (int)zlibResult.stream!.TotalOut;
                zlibResult.LastBytesRead = status != OperationStatus.Done ? default : dest.Length - (int)zlibResult.stream!.AvailOut;
                zlibResult.ClearBuffers();

                // release the handle.
                zlibResult.stream!.Dispose();
                zlibResult.stream = null;
            }
            else
            {
                zlibResult.LastBytesWritten = default;
                zlibResult.LastBytesRead = default;
            }

            zlibResult.Status = status;
        }

        private static OperationStatus Inflate(ref ZlibResult zlibResult, ZLibNative.FlushCode flush, bool throwOnError)
        {
            ZLibNative.ErrorCode result;
            try
            {
                result = zlibResult.stream!.Inflate(flush);
            }
            catch (Exception cause) // could not load the Zlib DLL correctly
            {
                throw new ZLibException(SR.ZLibErrorDLLLoadError, cause);
            }

            return result switch
            {
                // progress has been made inflating
                ZLibNative.ErrorCode.Ok => OperationStatus.Done,
                // The end of the input stream has been reached
                ZLibNative.ErrorCode.StreamEnd => OperationStatus.Done,
                // No room in the output buffer - inflate() can be called again with more space to continue
                ZLibNative.ErrorCode.BufError => OperationStatus.NeedMoreData,
                // Not enough memory to complete the operation
                ZLibNative.ErrorCode.MemError => throwOnError
                    ? throw new ZLibException(SR.ZLibErrorNotEnoughMemory, "inflate_", (int)result, zlibResult.stream.GetErrorMessage())
                    : OperationStatus.InvalidData,
                // The input data was corrupted (input stream not conforming to the zlib format or incorrect check value)
                ZLibNative.ErrorCode.DataError => throwOnError
                    ? throw new InvalidDataException(SR.UnsupportedCompression)
                    : OperationStatus.InvalidData,
                //the stream structure was inconsistent (for example if next_in or next_out was NULL),
                ZLibNative.ErrorCode.StreamError => throwOnError
                    ? throw new ZLibException(SR.ZLibErrorInconsistentStream, "inflate_", (int)result, zlibResult.stream.GetErrorMessage())
                    : OperationStatus.InvalidData,
                _ => throwOnError
                    ? throw new ZLibException(SR.ZLibErrorUnexpected, "inflate_", (int)result, zlibResult.stream.GetErrorMessage())
                    : OperationStatus.InvalidData,
            };
        }
    }
}
