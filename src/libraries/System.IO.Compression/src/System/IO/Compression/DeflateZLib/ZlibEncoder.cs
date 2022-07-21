// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Buffers;

namespace System.IO.Compression
{
    public class ZlibEncoder
    {
        public ZlibOptions? Options { get; set; }

        // caller is responsible for disposing of the result.
        public bool TryCompress(ref ZlibResult zlibResult, ReadOnlySpan<byte> source, Span<byte> dest)
        {
            Compress(ref zlibResult, source, dest, false);
            return zlibResult.Status == OperationStatus.Done;
        }

        public void Compress(ref ZlibResult zlibResult, ReadOnlySpan<byte> source, Span<byte> dest)
            => Compress(ref zlibResult, source, dest, true);

#pragma warning disable CS3002 // Return type is not CLS-compliant
        // when window bits is >= 25 and <= 31 returns a Crc-32 checksum
        // otherwise returns an Adler-32 checksum.
        public uint CalculateChecksum(ReadOnlySpan<byte> source)
#pragma warning restore CS3002 // Return type is not CLS-compliant
            => CalculateChecksumCore(source, Options);

        internal static unsafe uint CalculateChecksumCore(ReadOnlySpan<byte> source, ZlibOptions? options)
        {
            fixed (byte* sourcePtr = source)
            {
                return options?.WindowBits is >= 25 and <= 31
                    ? Interop.ZLib.crc32(Interop.ZLib.crc32(0, null, 0), sourcePtr, source.Length)
                    : Interop.ZLib.adler32(Interop.ZLib.adler32(0, null, 0), sourcePtr, source.Length);
            }
        }

        internal unsafe OperationStatus DeflateCore(ref ZlibResult zlibResult, ReadOnlySpan<byte> source, Span<byte> dest, ZLibNative.FlushCode flush, bool throwOnError)
        {
            fixed (byte* sourcePtr = source)
            fixed (byte* destPtr = dest)
            {
                zlibResult.stream!.NextIn = (IntPtr)sourcePtr;
                zlibResult.stream.AvailIn = (uint)source.Length;
                zlibResult.stream.NextOut = (IntPtr)destPtr;
                zlibResult.stream.AvailOut = (uint)dest.Length;
                return Deflate(ref zlibResult, flush, throwOnError);
            }
        }

        private unsafe void Compress(ref ZlibResult zlibResult, ReadOnlySpan<byte> source, Span<byte> dest, bool throwOnError)
        {
            bool skip = zlibResult.IsDisposed || Options?.CompressionMode != CompressionMode.Compress || source.Length >= dest.Length;
            OperationStatus status = (zlibResult.IsDisposed, Options?.CompressionMode != CompressionMode.Compress, source.Length >= dest.Length) switch
            {
                (true, false, false) or (true, true, false) or (false, true, false) or (true, false, true) or (true, true, true) or (false, true, true) => OperationStatus.Error,
                (false, false, true) => OperationStatus.DestinationTooSmall,
                _ => OperationStatus.Done,
            };
            if (!skip)
            {
                status = zlibResult.DeflateInit(throwOnError, Options!);
                if (status is OperationStatus.Error)
                {
                    skip = true;
                }
            }

            if (!skip)
            {
                status = DeflateCore(ref zlibResult, source, dest, (ZLibNative.FlushCode)Options!.FlushMode, throwOnError);
                status = status == OperationStatus.Done ? Deflate(ref zlibResult, ZLibNative.FlushCode.Finish, throwOnError) : status;
                zlibResult.LastBytesWritten = status != OperationStatus.Done ? default : (int)zlibResult.stream!.TotalOut;
                zlibResult.LastBytesRead = status != OperationStatus.Done ? default : dest.Length - (int)zlibResult.stream!.AvailOut;
                zlibResult.TotalBytesWritten += zlibResult.LastBytesWritten;
                zlibResult.TotalBytesRead += zlibResult.LastBytesRead;
                zlibResult.ClearBuffers();

                // release the handle.
                zlibResult.stream?.Dispose();
                zlibResult.stream = null;
            }
            else
            {
                zlibResult.LastBytesWritten = default;
                zlibResult.LastBytesRead = default;
            }

            zlibResult.Status = status;
        }

        private static OperationStatus Deflate(ref ZlibResult zlibResult, ZLibNative.FlushCode flush, bool throwOnError)
        {
            ZLibNative.ErrorCode result = zlibResult.stream!.Deflate(flush);
            return result switch
            {
                ZLibNative.ErrorCode.Ok or ZLibNative.ErrorCode.StreamEnd => OperationStatus.Done,
                // This is a recoverable error
                ZLibNative.ErrorCode.BufError => OperationStatus.NeedMoreData,
                ZLibNative.ErrorCode.StreamError => throwOnError
                    ? throw new ZLibException(SR.ZLibErrorInconsistentStream, "deflate", (int)result, zlibResult.stream.GetErrorMessage())
                    : OperationStatus.InvalidData,
                _ => throwOnError
                    ? throw new ZLibException(SR.ZLibErrorUnexpected, "deflate", (int)result, zlibResult.stream.GetErrorMessage())
                    : OperationStatus.InvalidData,
            };
        }
    }
}
