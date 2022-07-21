// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace System.IO.Compression
{
    public class GZipOptions : ZlibOptions
    {
        public GZipOptions(CompressionMode compressionMode)
            : base(ZLibNative.GZip_DefaultWindowBits, ZlibMemoryLevel.Level8, ZlibCompressionStrategy.DefaultStrategy, compressionMode)
        {
        }
    }
}
