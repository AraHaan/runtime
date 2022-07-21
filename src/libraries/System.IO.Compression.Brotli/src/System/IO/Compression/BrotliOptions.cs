// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace System.IO.Compression
{
    public struct BrotliOptions
    {
        public readonly BrotliCompressionLevel CompressionLevel { get; }
        public readonly int WindowBits { get; }
        public readonly CompressionMode CompressionMode { get; }

        public BrotliOptions()
        {
            CompressionLevel = BrotliCompressionLevel.NoCompression;
            WindowBits = BrotliUtils.WindowBits_Default;
            CompressionMode = CompressionMode.Decompress;
        }

        public BrotliOptions(BrotliCompressionLevel compressionLevel)
            : this(compressionLevel, BrotliUtils.WindowBits_Default)
        {
        }

        public BrotliOptions(BrotliCompressionLevel compressionLevel, int windowBits)
        {
            if ((int)compressionLevel is < BrotliUtils.Quality_Min or > BrotliUtils.Quality_Max)
            {
                throw new ArgumentOutOfRangeException(nameof(compressionLevel), SR.Format(SR.BrotliEncoder_Quality, compressionLevel, 0, BrotliUtils.Quality_Max));
            }

            if (windowBits is < BrotliUtils.WindowBits_Min or > BrotliUtils.WindowBits_Max)
            {
                throw new ArgumentOutOfRangeException(nameof(windowBits), SR.Format(SR.BrotliEncoder_Window, windowBits, BrotliUtils.WindowBits_Min, BrotliUtils.WindowBits_Max));
            }

            CompressionLevel = compressionLevel;
            WindowBits = windowBits;
            CompressionMode = CompressionMode.Compress;
        }

        internal BrotliOptions(CompressionLevel compressionLevel)
            : this(compressionLevel switch
            {
                Compression.CompressionLevel.NoCompression => (BrotliCompressionLevel)BrotliUtils.Quality_Min,
                Compression.CompressionLevel.Fastest => (BrotliCompressionLevel)BrotliUtils.Quality_Fastest,
                Compression.CompressionLevel.Optimal => (BrotliCompressionLevel)BrotliUtils.Quality_Default,
                Compression.CompressionLevel.SmallestSize => (BrotliCompressionLevel)BrotliUtils.Quality_Max,
                _ => throw new ArgumentException(SR.ArgumentOutOfRange_Enum, nameof(compressionLevel)),
            })
        {
        }
    }
}
