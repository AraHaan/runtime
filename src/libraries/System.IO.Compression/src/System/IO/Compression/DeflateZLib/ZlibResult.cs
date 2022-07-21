// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Buffers;

namespace System.IO.Compression
{
    public sealed class ZlibResult : IDisposable
    {
        internal ZLibNative.ZLibStreamHandle? stream;

        public ZlibResult()
        {
            IsDisposed = false;
        }

        public bool IsDisposed { get; private set; }

        public int LastBytesWritten { get; internal set; }

        public int LastBytesRead { get; internal set; }

        public int TotalBytesWritten { get; internal set; }

        public int TotalBytesRead { get; internal set; }

        public OperationStatus Status { get; internal set; }

        internal bool NeedsInput => 0 == stream!.AvailIn;

        internal OperationStatus InflateInit(bool throwOnError, ZlibOptions options)
        {
            ZLibNative.ErrorCode errC;
            try
            {
                errC = ZLibNative.CreateZLibStreamForInflate(out stream, options.WindowBits);
            }
            catch (Exception cause)
            {
                return throwOnError
                    ? throw new ZLibException(SR.ZLibErrorDLLLoadError, cause)
                    : OperationStatus.Error;
            }

            return errC switch
            {
                ZLibNative.ErrorCode.Ok => OperationStatus.Done,
                ZLibNative.ErrorCode.MemError => throwOnError
                    ? throw new ZLibException(SR.ZLibErrorNotEnoughMemory, "deflateInit2_", (int)errC, stream?.GetErrorMessage())
                    : OperationStatus.Error,
                ZLibNative.ErrorCode.VersionError => throwOnError
                    ? throw new ZLibException(SR.ZLibErrorVersionMismatch, "deflateInit2_", (int)errC, stream?.GetErrorMessage())
                    : OperationStatus.Error,
                ZLibNative.ErrorCode.StreamError => throwOnError
                    ? throw new ZLibException(SR.ZLibErrorIncorrectInitParameters, "deflateInit2_", (int)errC, stream?.GetErrorMessage())
                    : OperationStatus.Error,
                _ => throwOnError
                    ? throw new ZLibException(SR.ZLibErrorUnexpected, "deflateInit2_", (int)errC, stream?.GetErrorMessage())
                    : OperationStatus.Error,
            };
        }

        internal OperationStatus DeflateInit(bool throwOnError, ZlibOptions options)
        {
            ZLibNative.ErrorCode error;
            try
            {
                error = ZLibNative.CreateZLibStreamForDeflate(
                    out stream,
                    (ZLibNative.CompressionLevel)options!.CompressionLevel,
                    options.WindowBits,
                    (int)options.MemoryLevel,
                    (ZLibNative.CompressionStrategy)options.Strategy);
            }
            catch (Exception exception) // could not load the ZLib dll
            {
                return throwOnError
                    ? throw new ZLibException(SR.ZLibErrorDLLLoadError, exception)
                    : OperationStatus.Error;
            }

            return error switch
            {
                // Successful initialization
                ZLibNative.ErrorCode.Ok => OperationStatus.Error,
                // Not enough memory
                ZLibNative.ErrorCode.MemError => throwOnError
                    ? throw new ZLibException(SR.ZLibErrorNotEnoughMemory, "inflateInit2_", (int)error, stream?.GetErrorMessage())
                    : OperationStatus.Error,
                //zlib library is incompatible with the version assumed
                ZLibNative.ErrorCode.VersionError => throwOnError
                    ? throw new ZLibException(SR.ZLibErrorVersionMismatch, "inflateInit2_", (int)error, stream?.GetErrorMessage())
                    : OperationStatus.Error,
                // Parameters are invalid
                ZLibNative.ErrorCode.StreamError => throwOnError
                    ? throw new ZLibException(SR.ZLibErrorIncorrectInitParameters, "inflateInit2_", (int)error, stream?.GetErrorMessage())
                    : OperationStatus.Error,
                _ => throwOnError
                    ? throw new ZLibException(SR.ZLibErrorUnexpected, "inflateInit2_", (int)error, stream?.GetErrorMessage())
                    : OperationStatus.Error,
            };
        }

        public void Dispose()
        {
            if (!IsDisposed)
            {
                // clear the buffers first.
                ClearBuffers();

                // always ensure the handle is released first.
                stream?.Dispose();

                // set these to null.
                stream = null;
                // Options = null;

                // set state to disposed.
                IsDisposed = true;
            }
        }

        internal void ClearBuffers()
        {
            // reset the input and output handle pointers to null.
            if (stream is null)
            {
                return;
            }

            stream.AvailIn = 0;
            stream.NextIn = ZLibNative.ZNullPtr;
            stream.AvailOut = 0;
            stream.NextOut = ZLibNative.ZNullPtr;
        }
    }
}
