package com.dishii.zelda3;

// Read-only bridge to the running game's state. The native side lives in
// app/jni/src/src/second_screen.c, compiled into libmain.so, which SDLActivity
// loads before MainActivity.onCreate finishes — so no loadLibrary needed here.
public class GameState {
    public static native int getLinkX();
    public static native int getLinkY();
    /** Overworld area index outdoors, dungeon room index indoors. */
    public static native int getArea();
    /** Low byte = main module (0x09 overworld, 0x07 dungeon), high byte = submodule. */
    public static native int getModule();
    public static native boolean isIndoors();
    /** Fills out (256 bytes) with g_ram[0xF300..0xF400): the save/equipment block. */
    public static native void readSram(byte[] out);
    /** Equipped Y-item as an inventory grid slot, 1..20 (0 = none). */
    public static native int getEquippedSlot();
    /** Low byte: palace index 0..13; high byte: current floor as int8 (0=1F, -1=B1). */
    public static native int getDungeon();
    /** Fills out (up to 0x500 bytes) with save_dung_info: uint16/room, low nibble = visited quadrants. */
    public static native void readDungFlags(byte[] out);
    /** Request equipping the item in grid slot 1..20; applied on the game thread. */
    public static native void equipSlot(int slot);

    // ---- runtime art generation from the user's zelda3_assets.dat ----
    // All return false (or -1) until the game has loaded its assets file.
    /** Fills out (512*512 ARGB) with the light or dark world map. */
    public static native boolean renderWorldMap(int[] out, boolean dark);
    /** Fills out (160x128 ARGB): the item icon sheet laid out per icons.json. */
    public static native boolean renderIconSheet(int[] out);
    /** Fills out (96x32 ARGB): the HUD glyph sheet laid out per glyphs.json. */
    public static native boolean renderGlyphSheet(int[] out);
    /** Fills out (128x16 ARGB): letters A-Z laid out per letters.json. */
    public static native boolean renderLetterSheet(int[] out);
    /** Fills out (16x16 ARGB) with a 16x16 chunk of Link's sprite sheet. */
    public static native boolean renderLinkFace(int[] out, int chunk);
    /** Fills out with the palace's 5x5-per-floor layout; returns floors | basements<<8, -1 if unavailable. */
    public static native int getDungeonLayout(int palace, byte[] out);

    // Offsets into the readSram block (g_ram+0xF300 == offset 0).
    public static final int OFF_ITEMS = 0x40;        // 20 item bytes, order matches menu
    public static final int OFF_BOTTLE_SELECT = 0x4F;
    public static final int OFF_GLOVES = 0x54;
    public static final int OFF_SWORD = 0x59;
    public static final int OFF_SHIELD = 0x5A;
    public static final int OFF_ARMOR = 0x5B;
    public static final int OFF_BOTTLES = 0x5C;      // 4 bottle content bytes
    public static final int OFF_RUPEES = 0x62;       // uint16 actual
    public static final int OFF_HEART_PIECES = 0x6B;
    public static final int OFF_HEALTH_CAP = 0x6C;
    public static final int OFF_HEALTH_CUR = 0x6D;
    public static final int OFF_MAGIC = 0x6E;
    public static final int OFF_KEYS = 0x6F;
    public static final int OFF_BOMB_UPGRADES = 0x70;
    public static final int OFF_ARROW_UPGRADES = 0x71;
    public static final int OFF_PENDANTS = 0x74;
    public static final int OFF_ARROWS = 0x77;
    public static final int OFF_CRYSTALS = 0x7A;
    public static final int OFF_MAGIC_CONSUMPTION = 0x7B;
}
