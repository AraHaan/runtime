// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace System.IO.Compression
{
    // users should subclass this in order to set a custom "WindowBits", "MemoryLevel", or "Strategy".
    public class ZlibOptions
    {
        public int WindowBits { get; }
        public ZlibMemoryLevel MemoryLevel { get; }
        public ZlibFlushCode FlushMode { get; set; }
        public ZlibCompressionLevel CompressionLevel { get; }
        public ZlibCompressionStrategy Strategy { get; }
        public CompressionMode CompressionMode { get; }

        public ZlibOptions()
            : this(CompressionMode.Decompress)
        {
        }

        public ZlibOptions(CompressionMode compressionMode)
            : this(ZLibNative.ZLib_DefaultWindowBits, ZlibMemoryLevel.Level8, ZlibCompressionStrategy.DefaultStrategy, compressionMode)
        {
        }

        public ZlibOptions(ZlibCompressionLevel compressionLevel)
            : this(CompressionMode.Compress)
        {
            if (compressionLevel is < ZlibCompressionLevel.DefaultCompression or > ZlibCompressionLevel.BestCompression)
            {
                throw new ArgumentOutOfRangeException(nameof(compressionLevel));
            }

            // constants used for deflate.
            CompressionLevel = compressionLevel;
            if (CompressionLevel is ZlibCompressionLevel.NoCompression)
            {
                MemoryLevel = ZlibMemoryLevel.Level7;
            }
        }

        public ZlibOptions(int windowBits, ZlibMemoryLevel memoryLevel, ZlibCompressionStrategy strategy, CompressionMode compressionMode)
        {
            // if window bits is not less than -8 and greater than or equal to -15 (no header) (Deflate),
            // or is not greater than +8 and less than or equal to +15 throw out of range (Zlib header).
            // Window bits between +25 to +31 (aka +16 + (+8~+15)) includes a gzip header.
            // Window bits between +40 to +47 (aka +32 + (+8~+15)) to accept either an zlib or gzip header (inflate only).
            // Window bits of 0 is for (inflate only).
            if (windowBits is not (<= -8 and >= ZLibNative.Deflate_DefaultWindowBits)
                and not (>= 8 and <= ZLibNative.ZLib_DefaultWindowBits)
                and not (>= 25 and <= ZLibNative.GZip_DefaultWindowBits)
                and not (>= 40 and <= 47)
                and not 0)
            {
                throw new ArgumentOutOfRangeException(nameof(windowBits));
            }

            if (windowBits is (>= 40 and <= 47) or 0 && compressionMode is CompressionMode.Compress)
            {
                throw new InvalidOperationException("WindowBits value of 0 or 40 to 47 can only be used when decompressing.");
            }

            WindowBits = windowBits;

            // constants used for inflate.
            MemoryLevel = memoryLevel;
            FlushMode = ZlibFlushCode.NoFlush;
            Strategy = strategy;
            CompressionMode = compressionMode;
        }

        internal ZlibOptions(CompressionLevel compressionLevel)
            : this(
                  compressionLevel switch
                  {
                      Compression.CompressionLevel.Optimal => ZlibCompressionLevel.DefaultCompression,
                      Compression.CompressionLevel.Fastest => ZlibCompressionLevel.BestSpeed,
                      Compression.CompressionLevel.NoCompression => ZlibCompressionLevel.NoCompression,
                      Compression.CompressionLevel.SmallestSize => ZlibCompressionLevel.BestCompression,
                      _ => throw new ArgumentOutOfRangeException(nameof(compressionLevel))
                  })
        {
        }
    }
}
