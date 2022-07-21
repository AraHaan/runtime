// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace System.IO.Compression
{
    public class DeflateOptions : ZlibOptions
    {
        public DeflateOptions(CompressionMode compressionMode)
            : base(ZLibNative.Deflate_DefaultWindowBits, ZlibMemoryLevel.Level8, ZlibCompressionStrategy.DefaultStrategy, compressionMode)
        {
        }
    }
}
