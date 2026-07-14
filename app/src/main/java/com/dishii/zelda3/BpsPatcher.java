package com.dishii.zelda3;

import java.util.zip.CRC32;

// Pure-Java port of ApplyBps() in app/jni/src/src/util.c. Applies a BPS binary
// patch (the bundled zelda3_assets.bps) to a source ROM to produce the
// zelda3_assets.dat the game loads. Doing it here, before SDL starts, lets the
// setup screen turn a user-supplied ROM into the assets file on first launch.
//
// The BPS format bakes in the CRC32 of the exact ROM it was built against, so a
// mismatch tells the user they picked the wrong (or a bad-dump / headered) ROM
// rather than silently producing garbage.
final class BpsPatcher {

    // Thrown with a human-readable reason so the setup screen can show it.
    static final class BpsException extends Exception {
        BpsException(String message) { super(message); }
    }

    private BpsPatcher() {}

    private static long crc32(byte[] data, int off, int len) {
        CRC32 crc = new CRC32();
        crc.update(data, off, len);
        return crc.getValue();
    }

    // Little-endian uint32 read as an unsigned long.
    private static long readU32(byte[] b, int off) {
        return (b[off] & 0xFFL)
                | ((b[off + 1] & 0xFFL) << 8)
                | ((b[off + 2] & 0xFFL) << 16)
                | ((b[off + 3] & 0xFFL) << 24);
    }

    // Cursor over the patch so the varint decoder can advance a shared position.
    private static final class Cursor { int pos; }

    // Mirror of BpsDecodeInt(): a base-128 varint with an implicit +1 carry per
    // continuation group.
    private static long decodeInt(byte[] bps, Cursor c) {
        long data = 0, shift = 1;
        while (true) {
            int x = bps[c.pos++] & 0xFF;
            data += (x & 0x7f) * shift;
            if ((x & 0x80) != 0) break;
            shift <<= 7;
            data += shift;
        }
        return data;
    }

    /**
     * Apply {@code bps} to {@code src} and return the patched output.
     *
     * @throws BpsException if the patch is malformed, or (most usefully) if the
     *         supplied ROM does not match the one the patch was built for.
     */
    static byte[] apply(byte[] src, byte[] bps) throws BpsException {
        if (bps.length < 16 || bps[0] != 'B' || bps[1] != 'P' || bps[2] != 'S' || bps[3] != '1')
            throw new BpsException("Bundled patch is not a BPS1 file.");

        int trailer = bps.length - 12;
        long expectSrcCrc = readU32(bps, trailer);
        long expectDstCrc = readU32(bps, trailer + 4);
        long expectPatchCrc = readU32(bps, trailer + 8);

        if (crc32(src, 0, src.length) != expectSrcCrc)
            throw new BpsException("That file isn't the expected ROM. Use an unheadered US "
                    + "\"Legend of Zelda, The - A Link to the Past (USA).sfc\" (1,048,576 bytes).");
        if (crc32(bps, 0, bps.length - 4) != expectPatchCrc)
            throw new BpsException("Bundled patch is corrupt.");

        Cursor c = new Cursor();
        c.pos = 4;
        long srcSize = decodeInt(bps, c);
        long dstSize = decodeInt(bps, c);
        long metaSize = decodeInt(bps, c);
        c.pos += (int) metaSize; // skip metadata (empty for our patch)

        if (srcSize != src.length)
            throw new BpsException("ROM is the wrong size (expected " + srcSize + " bytes).");
        if (dstSize > Integer.MAX_VALUE)
            throw new BpsException("Patch target too large.");

        byte[] dst = new byte[(int) dstSize];
        int outputOffset = 0;
        int sourceRelativeOffset = 0;
        int targetRelativeOffset = 0;

        while (c.pos < trailer) {
            long cmd = decodeInt(bps, c);
            int length = (int) ((cmd >> 2) + 1);
            switch ((int) (cmd & 3)) {
                case 0: // SourceRead: copy from src at the current output offset.
                    while (length-- > 0) {
                        dst[outputOffset] = src[outputOffset];
                        outputOffset++;
                    }
                    break;
                case 1: // TargetRead: literal bytes from the patch stream.
                    while (length-- > 0)
                        dst[outputOffset++] = bps[c.pos++];
                    break;
                case 2: { // SourceCopy: signed relative seek into src.
                    long d = decodeInt(bps, c);
                    sourceRelativeOffset += ((d & 1) != 0 ? -1 : 1) * (int) (d >> 1);
                    while (length-- > 0)
                        dst[outputOffset++] = src[sourceRelativeOffset++];
                    break;
                }
                default: { // TargetCopy: signed relative seek into the output so far.
                    long d = decodeInt(bps, c);
                    targetRelativeOffset += ((d & 1) != 0 ? -1 : 1) * (int) (d >> 1);
                    while (length-- > 0)
                        dst[outputOffset++] = dst[targetRelativeOffset++];
                    break;
                }
            }
        }

        if (outputOffset != dstSize)
            throw new BpsException("Patch produced the wrong amount of data.");
        if (crc32(dst, 0, dst.length) != expectDstCrc)
            throw new BpsException("Extraction failed a checksum. Try a different ROM dump.");

        return dst;
    }
}
