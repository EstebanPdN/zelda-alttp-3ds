package com.dishii.zelda3;

import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.List;

// Pulls the text of a translation romhack straight out of the translated ROM
// and appends it to zelda3_assets.dat as an extra language, so the game's
// existing `Language =` ini setting can select it (see ZeldaSetLanguage in
// zelda_rtl.c).
//
// This mirrors what upstream's `restool.py --extract-dialogue` plus
// `--languages=xx` builds, minus the dialogue.txt detour: the assets file
// stores dialogue in the ROM's native text encoding, so the messages,
// dictionary, font and width table can be copied verbatim from the ROM and
// only need repackaging into the assets container. That also means no
// per-language alphabet tables are needed here — any hack of the US ROM that
// keeps the stock text engine layout works, even ones upstream doesn't know.
//
// Not supported: the official PAL ROMs (de/fr/fr-c/en) which use a different
// base ROM and text encoding, and the pt-br hack which remaps the font.
final class TranslationExtractor {

    // Thrown with a human-readable reason so the setup screen can show it.
    static final class RomException extends Exception {
        RomException(String message) { super(message); }
    }

    // Everything pulled from one translated ROM, ready to append to the .dat.
    static final class Language {
        final String code;         // value for `Language =` in zelda3.ini
        final String displayName;  // for setup screen messages
        final byte[] dialogueBlk;  // packed [dictionary, messages] (kDialogue entry)
        final byte[] fontBlk;      // packed [font gfx, width table] (kDialogueFont entry)

        Language(String code, String displayName, byte[] dialogueBlk, byte[] fontBlk) {
            this.code = code;
            this.displayName = displayName;
            this.dialogueBlk = dialogueBlk;
            this.fontBlk = fontBlk;
        }
    }

    private TranslationExtractor() {}

    // ---- ROM layout of the stock US text engine ----

    private static final int ROM_SIZE = 1048576;
    // Message data lives in bank $1C and overflows into bank $0E; a 0x80 byte
    // switches banks, 0x7F ends a message and 0xFF ends the whole list.
    private static final int TEXT_BANK1 = 0xE0000;
    private static final int TEXT_BANK2 = 0x75F40;
    // The dictionary is a table of 16-bit bank-$0E pointers at $0E:C703; the
    // gap between the table and the first word it points at gives the count.
    private static final int DICT_PTRS = 0x74703;
    private static final int DICT_PTR_BASE = 0xC703;
    private static final int BANK_0E = 0x70000;
    // 2bpp message font (256 chars) and the proportional width table.
    private static final int FONT_GFX = 0x70000;
    private static final int FONT_GFX_SIZE = 0x1000;
    private static final int FONT_WIDTHS = 0x74ADF;
    private static final int FONT_WIDTHS_COUNT = 99;

    // Extra bytes following each command byte 0x67..0x7F (mirrors
    // kText_CommandLengths_US in messaging.c).
    private static final byte[] CMD_ARG_BYTES = {
        0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0,
    };

    // Some hacks were built from a dump that misses message 4 (the item-select
    // digits template); the US bytes for it, spliced back in so the message
    // numbering the game hardcodes stays aligned. Same fixup as upstream.
    private static final byte[] US_MESSAGE_4 = {
        0x7a, 0x00, 0x34, 0x40, 0x59, 0x6c, 0x00, 0x41, 0x59, 0x35, 0x40, 0x59, 0x6c, 0x01,
        0x75, 0x36, 0x40, 0x59, 0x6c, 0x02, 0x41, 0x59, 0x37, 0x40, 0x59, 0x6c, 0x03,
    };

    // SHA-1 of known translated ROMs (same list restool.py recognizes), so the
    // setup screen can name what it found and the ini gets a stable code.
    private static final String[][] KNOWN_ROMS = {
        { "461FCBD700D1332009C0E85A7A136E2A8E4B111E", "es", "Spanish" },
        { "D455AB9E6B24B20D393AEBD17E7610A3FA21D653", "es", "Spanish" },  // older v1.0c patch
        { "3C4D605EEFDA1D76F101965138F238476655B11D", "pl", "Polish" },
        { "FA8ADFDBA2697C9A54D583A1284A22AC764C7637", "nl", "Dutch" },
        { "43CD3438469B2C3FE879EA2F410B3EF3CB3F1CA4", "sv", "Swedish" },
        { "B2A07A59E64C498BC1B2F28728F9BF4014C8D582", "redux", "English Redux" },
        { "9325C22EB0A2A1F0017157C8B620BC3A605CEDE1", "redux", "English Redux" },
    };

    // ROMs we can identify but whose text can't be lifted with the US layout.
    private static final String[][] UNSUPPORTED_ROMS = {
        { "2E62494967FB0AFDF5DA1635607F9641DF7C6559", "the German PAL ROM" },
        { "229364A1B92A05167CD38609B1AA98F7041987CC", "the French PAL ROM" },
        { "C1C6C7F76FFF936C534FF11F87A54162FC0AA100", "the French-Canadian PAL ROM" },
        { "7C073A222569B9B8E8CA5FCB5DFEC3B5E31DA895", "the European PAL ROM" },
        { "D0D09ED41F9C373FE6AFDCCAFBF0DA8C88D3D90D", "the Portuguese translation" },
    };

    /**
     * Read the dialogue, dictionary and font out of {@code rom} (a translation
     * hack of the US ROM). Throws with a user-facing reason if the ROM doesn't
     * carry text in the stock US format.
     */
    static Language extract(byte[] rom) throws RomException {
        if (rom.length < ROM_SIZE)
            throw new RomException("That file isn't the US ROM or a translation of it. Use an "
                    + "unheadered US ROM (1,048,576 bytes), optionally patched with a translation.");

        String sha1 = sha1Hex(rom);
        for (String[] entry : UNSUPPORTED_ROMS) {
            if (entry[0].equals(sha1))
                throw new RomException("This looks like " + entry[1]
                        + ", which uses a different text format and isn't supported.");
        }
        String code = "mod", displayName = "an unrecognized translation";
        for (String[] entry : KNOWN_ROMS) {
            if (entry[0].equals(sha1)) {
                code = entry[1];
                displayName = "the " + entry[2] + " translation";
                break;
            }
        }

        List<byte[]> messages = extractMessages(rom);
        if (messages.size() == 396)
            messages.add(4, US_MESSAGE_4);
        List<byte[]> dictionary = extractDictionary(rom);

        byte[] dialogueBlk = pack(twoBlocks(pack(dictionary), pack(messages)));

        byte[] fontGfx = new byte[FONT_GFX_SIZE];
        System.arraycopy(rom, FONT_GFX, fontGfx, 0, FONT_GFX_SIZE);
        byte[] widths = new byte[FONT_WIDTHS_COUNT];
        System.arraycopy(rom, FONT_WIDTHS, widths, 0, FONT_WIDTHS_COUNT);
        byte[] fontBlk = pack(twoBlocks(fontGfx, widths));

        return new Language(code, displayName, dialogueBlk, fontBlk);
    }

    // Walk the message data exactly like the game's text engine: honor command
    // argument bytes so a parameter can't be mistaken for a terminator. The
    // stored messages drop the trailing 0x7F, matching what restool emits.
    private static List<byte[]> extractMessages(byte[] rom) throws RomException {
        List<byte[]> messages = new ArrayList<byte[]>(400);
        List<Byte> cur = new ArrayList<Byte>(64);
        int p = TEXT_BANK1, bankSwitches = 0;
        for (int guard = 0; ; guard++) {
            if (guard > 0x20000 || p >= ROM_SIZE || messages.size() > 1000)
                throw new RomException("Couldn't find translated text in that ROM.");
            int c = rom[p] & 0xFF;
            if (c == 0xFF)
                break;
            if (c == 0x80) {
                if (++bankSwitches > 1)
                    throw new RomException("Couldn't find translated text in that ROM.");
                p = TEXT_BANK2;
                continue;
            }
            int len = (c >= 0x67 && c < 0x80) ? 1 + CMD_ARG_BYTES[c - 0x67] : 1;
            for (int i = 0; i < len; i++)
                cur.add(rom[p + i]);
            p += len;
            if (c == 0x7F) {
                cur.remove(cur.size() - 1);
                messages.add(toArray(cur));
                cur.clear();
            }
        }
        if (messages.size() < 300)
            throw new RomException("Couldn't find translated text in that ROM.");
        return messages;
    }

    // The words are stored back to back right after the pointer table, so the
    // first pointer doubles as the entry count and each entry ends where the
    // next one starts.
    private static List<byte[]> extractDictionary(byte[] rom) throws RomException {
        int first = readU16(rom, DICT_PTRS);
        int gap = first - DICT_PTR_BASE;
        if (gap <= 0 || (gap & 1) != 0 || gap / 2 < 50 || gap / 2 > 200)
            throw new RomException("Couldn't find the text dictionary in that ROM.");
        int count = gap / 2 - 1;  // last pointer only marks the end of the final word
        List<byte[]> words = new ArrayList<byte[]>(count);
        for (int i = 0; i < count; i++) {
            int start = readU16(rom, DICT_PTRS + i * 2);
            int end = readU16(rom, DICT_PTRS + i * 2 + 2);
            if (start < 0x8000 || end < start || end - start > 256 || BANK_0E + end - 0x8000 > ROM_SIZE)
                throw new RomException("Couldn't find the text dictionary in that ROM.");
            byte[] word = new byte[end - start];
            System.arraycopy(rom, BANK_0E + start - 0x8000, word, 0, word.length);
            words.add(word);
        }
        return words;
    }

    /**
     * Return a copy of the assets file with {@code lang} appended to the
     * kDialogue / kDialogueFont / kDialogueMap assets. The container layout
     * mirrors LoadAssets in main.c: an 88-byte header, per-asset size table,
     * name blob, then each asset's payload aligned to 4 bytes.
     */
    static byte[] addLanguage(byte[] dat, Language lang) throws RomException {
        if (dat.length < 88)
            throw new RomException("Assets file is corrupt.");
        int numAssets = (int) readU32(dat, 80);
        int namesLen = (int) readU32(dat, 84);
        if (numAssets < 97 || numAssets > 100000 || dat.length < 88 + numAssets * 4 + namesLen)
            throw new RomException("Assets file is corrupt.");

        byte[][] assets = new byte[numAssets][];
        int offset = 88 + numAssets * 4 + namesLen;
        for (int i = 0; i < numAssets; i++) {
            int size = (int) readU32(dat, 88 + i * 4);
            offset = (offset + 3) & ~3;
            if (size < 0 || offset + size > dat.length)
                throw new RomException("Assets file is corrupt.");
            assets[i] = new byte[size];
            System.arraycopy(dat, offset, assets[i], 0, size);
            offset += size;
        }

        // Assets 94..96 are kDialogue, kDialogueFont and kDialogueMap (see
        // assets.h); each is a packed array with one entry per language.
        List<byte[]> dialogues = unpack(assets[94]);
        List<byte[]> fonts = unpack(assets[95]);
        List<byte[]> map = unpack(assets[96]);
        int idx = dialogues.size();
        dialogues.add(lang.dialogueBlk);
        fonts.add(lang.fontBlk);
        // Name -> [dialogue index, font index, flags]; flag 2 marks a language
        // whose text no longer matches the US ROM (disables the emu-compare
        // path), flag 1 would mean PAL command encoding which we never emit.
        byte[] conf = { (byte) idx, (byte) idx, 2 };
        byte[] nameBytes;
        try {
            nameBytes = lang.code.getBytes("UTF-8");
        } catch (java.io.UnsupportedEncodingException e) {
            nameBytes = lang.code.getBytes();
        }
        map.add(pack(twoBlocks(nameBytes, conf)));
        assets[94] = pack(dialogues);
        assets[95] = pack(fonts);
        assets[96] = pack(map);

        int total = 88 + numAssets * 4 + namesLen;
        for (byte[] a : assets)
            total = ((total + 3) & ~3) + a.length;
        byte[] out = new byte[total];
        System.arraycopy(dat, 0, out, 0, 88);  // signature + counts unchanged
        for (int i = 0; i < numAssets; i++)
            writeU32(out, 88 + i * 4, assets[i].length);
        System.arraycopy(dat, 88 + numAssets * 4, out, 88 + numAssets * 4, namesLen);
        offset = 88 + numAssets * 4 + namesLen;
        for (byte[] a : assets) {
            offset = (offset + 3) & ~3;
            System.arraycopy(a, 0, out, offset, a.length);
            offset += a.length;
        }
        return out;
    }

    // ---- packed-array helpers (see FindIndexInMemblk in util.c) ----
    // Layout: n-1 offsets (of entries 1..n-1), the entries back to back, then
    // a 16-bit trailer holding n-1. Offsets are 16-bit when they fit, else
    // 32-bit with 8192 added to the trailer.

    private static List<byte[]> unpack(byte[] blk) throws RomException {
        List<byte[]> out = new ArrayList<byte[]>();
        if (blk.length < 2)
            throw new RomException("Assets file is corrupt.");
        int end = blk.length - 2;
        int mx = readU16(blk, end), width = 2;
        if (mx >= 8192) {
            mx -= 8192;
            width = 4;
        }
        int base = mx * width;
        if (base > end)
            throw new RomException("Assets file is corrupt.");
        int prev = 0;
        for (int i = 0; i <= mx; i++) {
            int next = (i == mx) ? end - base
                    : (width == 2 ? readU16(blk, i * 2) : (int) readU32(blk, i * 4));
            if (next < prev || base + next > end)
                throw new RomException("Assets file is corrupt.");
            byte[] entry = new byte[next - prev];
            System.arraycopy(blk, base + prev, entry, 0, entry.length);
            out.add(entry);
            prev = next;
        }
        return out;
    }

    private static byte[] pack(List<byte[]> arr) {
        int dataSize = 0, offs = 0;
        for (byte[] a : arr)
            dataSize += a.length;
        offs = dataSize - arr.get(arr.size() - 1).length;  // largest stored offset
        boolean wide = offs >= 65536 || arr.size() > 8192;
        int width = wide ? 4 : 2;
        byte[] out = new byte[(arr.size() - 1) * width + dataSize + 2];
        int pos = (arr.size() - 1) * width, cum = 0;
        for (int i = 0; i < arr.size(); i++) {
            if (i != 0) {
                if (wide)
                    writeU32(out, (i - 1) * 4, cum);
                else
                    writeU16(out, (i - 1) * 2, cum);
            }
            System.arraycopy(arr.get(i), 0, out, pos + cum, arr.get(i).length);
            cum += arr.get(i).length;
        }
        writeU16(out, out.length - 2, (arr.size() - 1) + (wide ? 8192 : 0));
        return out;
    }

    private static List<byte[]> twoBlocks(byte[] a, byte[] b) {
        List<byte[]> list = new ArrayList<byte[]>(2);
        list.add(a);
        list.add(b);
        return list;
    }

    private static byte[] toArray(List<Byte> list) {
        byte[] out = new byte[list.size()];
        for (int i = 0; i < out.length; i++)
            out[i] = list.get(i);
        return out;
    }

    private static int readU16(byte[] b, int off) {
        return (b[off] & 0xFF) | ((b[off + 1] & 0xFF) << 8);
    }

    private static long readU32(byte[] b, int off) {
        return (b[off] & 0xFFL)
                | ((b[off + 1] & 0xFFL) << 8)
                | ((b[off + 2] & 0xFFL) << 16)
                | ((b[off + 3] & 0xFFL) << 24);
    }

    private static void writeU16(byte[] b, int off, int v) {
        b[off] = (byte) v;
        b[off + 1] = (byte) (v >> 8);
    }

    private static void writeU32(byte[] b, int off, int v) {
        b[off] = (byte) v;
        b[off + 1] = (byte) (v >> 8);
        b[off + 2] = (byte) (v >> 16);
        b[off + 3] = (byte) (v >> 24);
    }

    private static String sha1Hex(byte[] data) {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-1").digest(data);
            StringBuilder sb = new StringBuilder(40);
            for (byte b : digest)
                sb.append(String.format("%02X", b));
            return sb.toString();
        } catch (NoSuchAlgorithmException e) {
            return "";
        }
    }
}
