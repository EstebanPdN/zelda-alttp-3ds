package com.dishii.zelda3;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.BitmapShader;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.util.Log;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;

/**
 * Bottom-screen companion view (Ayn Thor), styled after A Link Between Worlds'
 * lower screen using authentic ALttP pixel art extracted from the game:
 *
 *  - MAP: the real mode-7 world map with a 2.6x follow-cam (tap to toggle the
 *    whole-world view, long press to drop/remove a personal pin) on the game's
 *    own menu-pattern background; engraved stone theme with real floor layouts
 *    in dungeons.
 *  - ITEMS / GEAR: full-width bottom buttons open menu-style black panels
 *    (like the game's own menu boxes) with big icons; tapping an item equips.
 *  - Sidebar: hearts and magic on top, the equipped item in a ring (tap =
 *    cycle to the next item), rupees/keys at the bottom.
 */
public class MinimapView extends View {

    private static final String TAG = "Zelda3SecondScreen";

    // ---------- palette ----------
    private static final int COL_GOLD = Color.rgb(232, 194, 96);
    private static final int COL_GOLD_DARK = Color.rgb(122, 88, 30);
    private static final int COL_OUTLINE = Color.rgb(30, 22, 10);
    private static final int COL_BOX = Color.rgb(12, 12, 12);            // menu box black
    private static final int COL_BOX_BORDER = Color.rgb(96, 200, 120);   // menu green border
    private static final int COL_BOX_BORDER2 = Color.rgb(224, 176, 60);  // menu gold border
    private static final int COL_STONE_EDGE_L = Color.rgb(134, 142, 158);
    private static final int COL_STONE_EDGE_D = Color.rgb(44, 50, 62);
    private static final int COL_STONE_INSET = Color.rgb(38, 44, 56);
    private static final int COL_PLAQUE = Color.rgb(88, 96, 112);
    private static final int COL_PLAQUE_SEL = Color.rgb(58, 108, 196);

    private static final int TAB_MAP = 0, TAB_ITEMS = 1, TAB_GEAR = 2, TAB_SETTINGS = 3;

    // what the second screen shows, derived from the game's main module
    private static final int MODE_GAME = 0, MODE_TITLE = 1, MODE_CINEMA = 2;

    // ---------- paints ----------
    private final Paint fill = new Paint();
    private final Paint stroke = new Paint();
    private final Paint bmp = new Paint();
    private final Paint menuPaint = new Paint();
    private final Paint parchPaint = new Paint();
    private final Paint stonePaint = new Paint();
    private final Paint aa = new Paint(Paint.ANTI_ALIAS_FLAG);

    // ---------- assets ----------
    private Bitmap mapLight, mapDark, icons, glyphs, letters, linkFace;
    private final Map<String, Rect> iconRects = new HashMap<>();
    private final Map<String, Rect> glyphRects = new HashMap<>();
    private final Map<String, Rect> letterRects = new HashMap<>();
    private Dungeon[] dungeons;

    private static class Dungeon {
        String name;
        int basements, floors, boss;
        byte[][] layout;
    }

    // ---------- live state ----------
    /** UI scale: design reference is a 1280x720 screen; everything scales with min(w,h). */
    private float u = 1f;
    private final byte[] sram = new byte[256];
    private final byte[] dungFlags = new byte[0x500];
    private final int[] dungFloorBuf = new int[80 * 80];
    private Bitmap dungFloorBmp;
    private final int[] mapIconBuf = new int[32 * 8];
    private Bitmap mapIconBmp;
    private int tab = TAB_MAP;
    private boolean wholeMap = false;
    // sticky floor preview: stays until Link changes floors/dungeons or the
    // current floor's plaque is tapped
    private static final int NO_FLOOR_SEL = Integer.MIN_VALUE;
    private int viewFloorSel = NO_FLOOR_SEL;
    private int viewFloorPalace = -1;   // dungeon the selection was made in
    private int viewFloorFrom = 0;      // Link's floor when the selection was made

    // player-dropped map pins ("come back for that heart piece"): world
    // coordinates plus the world they belong to, kept in map_pins.txt
    private static final int MAX_PINS = 20;
    private final int[] pinDark = new int[MAX_PINS];   // 0 = light world, 1 = dark
    private final int[] pinX = new int[MAX_PINS], pinY = new int[MAX_PINS];
    private int pinCount;
    // where the overworld map landed in the last draw, so a touch can be turned
    // back into world coordinates
    private final RectF mapViewR = new RectF(), mapZoomR = new RectF();
    private float mapOx, mapOy, mapScale;
    private boolean mapDarkWorld, mapLive;

    private boolean nativeBroken = false, assetsBroken = false, logged = false;
    private boolean artReady = false;
    private final byte[] probeBuf = new byte[1];

    // [Features] toggles offered on the settings screen: label, zelda3.ini
    // section + key, the kFeatures0_* bit (features.h) and whether the shown
    // ON/OFF is the inverse of the bit (the beep row shows the beep itself).
    private static final String[] FEAT_LABELS = {
        "ITEM SWITCH L R", "TURN WHILE DASH", "MIRROR TO DARK", "COLLECT BY SWORD",
        "SWORD BREAKS POTS", "LOW HP BEEP", "SKIP INTRO", "MAX ITEMS YELLOW",
        "MORE BOMBS", "CARRY 9999 RUPEES", "CANCEL BIRD RIDE", "DIM FLASHES",
    };
    private static final String[] FEAT_KEYS = {
        "ItemSwitchLR", "TurnWhileDashing", "MirrorToDarkworld", "CollectItemsWithSword",
        "BreakPotsWithSword", "DisableLowHealthBeep", "SkipIntroOnKeypress", "ShowMaxItemsInYellow",
        "MoreActiveBombs", "CarryMoreRupees", "CancelBirdTravel", "DimFlashes",
    };
    private static final String[] FEAT_SECTIONS = {
        "[Features]", "[Features]", "[Features]", "[Features]",
        "[Features]", "[Features]", "[Features]", "[Features]",
        "[Features]", "[Features]", "[Features]", "[Graphics]",
    };
    private static final int[] FEAT_MASKS = {
        2, 4, 8, 16, 32, 64, 128, 256, 512, 2048, 8192, 65536,
    };
    private static final boolean[] FEAT_INVERT = {
        false, false, false, false, false, true, false, false, false, false, false, false,
    };

    // touch regions (recomputed during draw)
    private final RectF tabItemsR = new RectF(), tabGearR = new RectF(), tabMapR = new RectF();
    private final RectF tabSettingsR = new RectF(), remapBackR = new RectF();
    private final RectF[] settingsRowR = new RectF[11 + FEAT_MASKS.length];
    private final RectF[] remapRowR = new RectF[12];
    private final RectF mapAreaR = new RectF(), yRingR = new RectF(), xRingR = new RectF();

    // settings state
    private float settingsScroll, settingsMaxScroll;
    private float settingsListTop, settingsListBot;
    private boolean settingsTouch, settingsScrolling;
    private float settingsTouchStartY, settingsTouchLastY;
    private boolean remapMode = false;
    private int remapArm = -1;          // command index waiting for a button press
    private long remapArmAt;
    private final int[] padControls = new int[12];
    private boolean hudPrefApplied = false;
    private boolean xRingFeatChecked = false;
    private boolean xRing = false;      // second ring with the X-assigned item (ItemSwitchLR)
    // which screen hosts the game; toggling takes effect on the next app start
    private boolean swapScreens, swapScreensApplied;
    private boolean autosave;
    // transient feedback on an action row ("SAVED"/"LOADED"/"EMPTY")
    private int settingsFlashRow = -1;
    private long settingsFlashUntil;
    private String settingsFlash = "";
    // MSU-1 audio packs; the emulator only reads EnableMSU at startup, so this
    // one also waits for a restart. msuOnValue keeps whichever flavour the ini
    // had (true/deluxe/opuz/deluxe-opuz) so turning it back on restores it.
    private boolean msuOn, msuOnApplied, msuPack;
    private String msuOnValue = "true";
    private int armedRing = 0;          // 1/2 = next items-grid tap assigns Y/X
    private static final String[] PAD_CMD_NAMES = {
        "UP", "DOWN", "LEFT", "RIGHT", "SELECT", "START", "A", "B", "X", "Y", "L", "R",
    };

    // save-state picker (SAVE STATES sub-screen): four slots of its own, from
    // saves/save2.sav up - slot 0 is the autosave and slot 1 the quick save.
    private static final int STATE_SLOT0 = 2, STATE_SLOTS = 4;
    private boolean statesMode = false;
    private final RectF statesBackR = new RectF();
    private final RectF[] stateSaveR = new RectF[STATE_SLOTS], stateLoadR = new RectF[STATE_SLOTS];
    private final Bitmap[] stateThumb = new Bitmap[STATE_SLOTS];
    private final long[] stateStamp = new long[STATE_SLOTS];   // .sav mtime, 0 = empty slot
    private final int[] thumbBuf = new int[GameState.THUMB_W * GameState.THUMB_H];
    private int thumbWantSlot = -1;      // a save waiting for its frame grab
    private long thumbWantAt;
    private int stateFlashSlot = -1;     // brief SAVED / LOADED banner on one card
    private String stateFlashMsg;
    private long stateFlashUntil;
    private static final String[] MONTHS = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    };
    private final RectF[] plaqueR = new RectF[10];
    private int plaqueCount = 0;
    private final int[] plaqueFloor = new int[10];
    private int gridX, gridY, gridCellW;
    private long tapFlashUntil; private int tapFlashSlot = -1;

    // map touch: the zoom toggle resolves on release so that holding the finger
    // down can drop a pin instead
    private boolean mapTouch, mapTouchMoved, mapLongFired;
    private float mapTouchX, mapTouchY;
    private final Runnable mapLongPress = new Runnable() {
        @Override
        public void run() {
            mapLongFired = togglePin(mapTouchX, mapTouchY);
            if (mapLongFired) performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
        }
    };

    private final Rect src = new Rect();
    private final RectF dst = new RectF();
    private final Rect faceSrc = new Rect(0, 0, 32, 32);
    private final Path triPath = new Path();
    private int uiMode = MODE_GAME;
    private int lastOutX, lastOutY, lastOutArea;
    private boolean hasLastOutdoor = false;
    private final int[] exitBuf = new int[3];   // exit-table {x, y, screen} for the current room

    private static final String[] ITEM_NAMES = {
        "bow", "boomerang", "hookshot", "bombs", "mushroom",
        "firerod", "icerod", "bombos", "ether", "quake",
        "torch", "hammer", "flute", "bugnet", "book",
        "bottle", "somaria", "byrna", "cape", "mirror",
    };
    private static final int[] ITEM_MAX_LEVEL = {
        4, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 7, 1, 1, 1, 3,
    };

    private static final int[][] PENDANT_MARKS = {   // {bit, x, y}
        {4, 3928, 1600},   // Courage - Eastern Palace  (verified vs Ch2 save)
        {2, 296, 3248},    // Power   - Desert Palace
        {1, 2160, 320},    // Wisdom  - Tower of Hera
    };
    private static final int[][] CRYSTAL_MARKS = {   // Ch7=0x12 verifies PoD+Swamp
        {2, 3960, 1600},   // Palace of Darkness
        {16, 1888, 3776},  // Swamp Palace
        {64, 208, 320},    // Skull Woods
        {32, 384, 1888},   // Thieves' Town
        {4, 3168, 3660},   // Ice Palace
        {1, 320, 3376},    // Misery Mire
        {8, 3800, 256},    // Turtle Rock
    };

    public MinimapView(Context context) {
        super(context);
        bmp.setFilterBitmap(false);
        stroke.setStyle(Paint.Style.STROKE);
        for (int i = 0; i < plaqueR.length; i++) plaqueR[i] = new RectF();
        for (int i = 0; i < remapRowR.length; i++) remapRowR[i] = new RectF();
        for (int i = 0; i < settingsRowR.length; i++) settingsRowR[i] = new RectF();
        for (int i = 0; i < STATE_SLOTS; i++) {
            stateSaveR[i] = new RectF();
            stateLoadR[i] = new RectF();
        }
        xRing = readIniBool("[General]", "SecondScreenXItemRing");
        swapScreens = swapScreensApplied = readIniBool("[General]", "SecondScreenSwap");
        autosave = readIniBool("[General]", "Autosave");
        String msu = readIni("[Sound]", "EnableMSU");
        msuOn = msuOnApplied = iniEnabled(msu);
        if (msuOn) msuOnValue = msu.trim();
        msuPack = msuPackPresent();
        loadPins();
        loadAssets(context);
    }

    // ================= assets =================

    private void loadAssets(Context context) {
        try {
            // layout manifests and synthesized theme textures ship in the APK;
            // all game pixel art is generated at runtime from zelda3_assets.dat
            manifest(context, "secondscreen/icons.json", iconRects);
            manifest(context, "secondscreen/glyphs.json", glyphRects);
            manifest(context, "secondscreen/letters.json", letterRects);
            texture(context, "secondscreen/tex_menu.png", menuPaint);
            texture(context, "secondscreen/tex_parchment.png", parchPaint);
            texture(context, "secondscreen/tex_stone.png", stonePaint);
        } catch (Exception e) {
            Log.e(TAG, "second screen asset load failed", e);
            assetsBroken = true;
        }
    }

    private static final String[] DUNGEON_NAMES = {
        "SEWERS", "HYRULE CASTLE", "EASTERN PALACE", "DESERT PALACE", "CASTLE TOWER",
        "SWAMP PALACE", "DARK PALACE", "MISERY MIRE", "SKULL WOODS", "ICE PALACE",
        "TOWER OF HERA", "THIEVES TOWN", "TURTLE ROCK", "GANONS TOWER",
    };
    private static final int[] DUNGEON_BOSS = {15, 15, 200, 51, 32, 6, 90, 144, 41, 222, 7, 172, 164, 13};
    private static final int[] DUNGEON_BOSS_POS = {   // x<<8|y of the skull inside its room (kDungMap_Tab37)
        -1, -1, 0x808, 8, 0, 8, 0x808, 8, 0x808, 0x800, 0x404, 0x808, 8, 8,
    };
    private static final int[] DOT_PALETTE = {0, 1, 2, 1};   // marker blink cycle (kDungMap_Tab38)

    /** Generate all pixel art from the game's loaded zelda3_assets.dat.
     *  Returns false (and stays cheap) until the game has loaded its assets. */
    private boolean tryLoadNativeArt() {
        try {
            // cheap probe: the assets file hasn't been parsed by the engine yet
            if (GameState.getDungeonLayout(0, probeBuf) < 0) return false;
            int[] map = new int[512 * 512];
            if (!GameState.renderWorldMap(map, false)) return false;
            mapLight = Bitmap.createBitmap(map, 512, 512, Bitmap.Config.ARGB_8888);
            GameState.renderWorldMap(map, true);
            mapDark = Bitmap.createBitmap(map, 512, 512, Bitmap.Config.ARGB_8888);

            int[] buf = new int[160 * 128];
            GameState.renderIconSheet(buf);
            icons = Bitmap.createBitmap(buf, 160, 128, Bitmap.Config.ARGB_8888);
            buf = new int[96 * 32];
            GameState.renderGlyphSheet(buf);
            glyphs = Bitmap.createBitmap(buf, 96, 32, Bitmap.Config.ARGB_8888);
            buf = new int[128 * 16];
            GameState.renderLetterSheet(buf);
            letters = Bitmap.createBitmap(buf, 128, 16, Bitmap.Config.ARGB_8888);
            buf = new int[16 * 16];
            GameState.renderLinkFace(buf, 0);
            linkFace = Bitmap.createScaledBitmap(
                    Bitmap.createBitmap(buf, 16, 16, Bitmap.Config.ARGB_8888), 32, 32, false);

            byte[] lay = new byte[200];
            Dungeon[] ds = new Dungeon[14];
            for (int i = 0; i < 14; i++) {
                int r = GameState.getDungeonLayout(i, lay);
                if (r < 0) return false;
                Dungeon d = new Dungeon();
                d.name = DUNGEON_NAMES[i];
                d.boss = DUNGEON_BOSS[i];
                d.floors = r & 0xFF;
                d.basements = (r >> 8) & 0xFF;
                d.layout = new byte[d.floors][25];
                for (int f = 0; f < d.floors; f++)
                    System.arraycopy(lay, f * 25, d.layout[f], 0, 25);
                ds[i] = d;
            }
            dungeons = ds;
            return true;
        } catch (UnsatisfiedLinkError e) {
            nativeBroken = true;
            return false;
        }
    }

    private static Bitmap bitmap(Context ctx, String path) throws Exception {
        BitmapFactory.Options o = new BitmapFactory.Options();
        o.inScaled = false;
        try (InputStream is = ctx.getAssets().open(path)) {
            return BitmapFactory.decodeStream(is, null, o);
        }
    }

    private static String readAsset(Context ctx, String path) throws Exception {
        StringBuilder sb = new StringBuilder();
        try (InputStream is = ctx.getAssets().open(path)) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = is.read(buf)) > 0) sb.append(new String(buf, 0, n, "UTF-8"));
        }
        return sb.toString();
    }

    private static void manifest(Context ctx, String path, Map<String, Rect> out) throws Exception {
        JSONObject root = new JSONObject(readAsset(ctx, path));
        int cell = root.getInt("cell");
        JSONObject map = root.getJSONObject("map");
        for (java.util.Iterator<String> it = map.keys(); it.hasNext(); ) {
            String k = it.next();
            int cx = map.getJSONArray(k).getInt(0) * cell;
            int cy = map.getJSONArray(k).getInt(1) * cell;
            out.put(k, new Rect(cx, cy, cx + cell, cy + cell));
        }
    }

    private void texture(Context ctx, String path, Paint p) throws Exception {
        Bitmap b = bitmap(ctx, path);
        BitmapShader sh = new BitmapShader(b, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT);
        Matrix m = new Matrix();
        m.setScale(2f, 2f);
        sh.setLocalMatrix(m);
        p.setShader(sh);
        p.setFilterBitmap(false);
    }

    // ================= draw =================

    @Override
    protected void onDraw(Canvas canvas) {
        int w = getWidth(), h = getHeight();
        if (!logged) { logged = true; Log.i(TAG, "MinimapView first draw " + w + "x" + h); }

        int linkX = 0, linkY = 0, area = 0, dungeonInfo = 0, module = 0x09;
        boolean indoors = false;
        if (!nativeBroken) {
            try {
                linkX = GameState.getLinkX();
                linkY = GameState.getLinkY();
                area = GameState.getArea();
                indoors = GameState.isIndoors();
                dungeonInfo = GameState.getDungeon();
                module = GameState.getModule() & 0xFF;
                GameState.readSram(sram);
                GameState.readDungFlags(dungFlags);
            } catch (UnsatisfiedLinkError e) {
                nativeBroken = true;
            }
        }
        boolean dungeonMode = indoors && !assetsBroken;
        if (!dungeonMode) viewFloorSel = NO_FLOOR_SEL;   // left the dungeon: forget the floor preview

        if (assetsBroken) { fill.setColor(Color.BLACK); canvas.drawRect(0, 0, w, h, fill); return; }

        u = Math.min(w, h) / 720f;
        mapLive = false;   // set again by drawOverworld; gates the pin long press

        // generate the art from zelda3_assets.dat once the engine has loaded it
        if (!artReady && !nativeBroken) artReady = tryLoadNativeArt();

        if (!nativeBroken) pollStateThumbnail();

        // re-apply the persisted top-screen HUD choice once per app start
        if (!hudPrefApplied && !nativeBroken) {
            hudPrefApplied = true;
            if (getContext().getSharedPreferences("secondscreen", 0).getBoolean("hideTopHud", false))
                GameState.setHudHidden(true);
        }

        // the X ring only works with the ItemSwitchLR feature; if the ini has the
        // ring on but the feature off (stale/edited config) the ring would show
        // but assignments would be dropped silently, so re-assert it once the
        // engine is up (artReady = config parsed and assets loaded)
        if (!xRingFeatChecked && xRing && artReady && !nativeBroken) {
            xRingFeatChecked = true;
            if ((GameState.getFeatures() & FEAT_MASKS[0]) == 0) {
                GameState.setFeature(FEAT_MASKS[0], true);
                updateIni(FEAT_SECTIONS[0], FEAT_KEYS[0], "1");
            }
        }

        // outside gameplay the minimap makes no sense: show a title card on the
        // intro/file-select screens and a quiet cinema frame during cutscenes;
        // the same screen also covers the brief window before the art is ready
        uiMode = modeForModule(module);
        if (module == 0x12 || module <= 0x05) hasLastOutdoor = false;   // death/file select: entrance unknown
        // houses and caves have no dungeon map (same test the game uses for X); keep the
        // overworld map with the marker frozen at the doorway Link came in through
        boolean inHouse = uiMode == MODE_GAME && indoors && (dungeonInfo & 0xFF) == 0xFF;
        // The special overworld screens (>= 0x80: Master Sword glade, Zora's Domain,
        // under the bridge) run in their own small coordinate space near the map
        // origin, which would park the marker in the Lost Woods (#23); freeze it at
        // the spot Link entered from, same as in houses.
        boolean special = uiMode == MODE_GAME && !indoors && area >= 0x80;
        // When the live "last outdoor" spot is unknown (fresh save-load, or the view
        // was rebuilt when the app regained focus) recover the doorway from the
        // engine (last tracked outdoor position, else its exit table) so the
        // overworld map still shows, HUD/tabs and all, instead of getting stuck on
        // the cinema card (#9).
        boolean haveExit = false;
        if ((inHouse || special) && !hasLastOutdoor && !nativeBroken) {
            try {
                haveExit = GameState.getIndoorExit(exitBuf);
            } catch (UnsatisfiedLinkError e) {
                nativeBroken = true;
            }
        }
        if ((inHouse || special) && !hasLastOutdoor && !haveExit)
            uiMode = MODE_CINEMA;
        if (uiMode != MODE_GAME || !artReady) {
            drawCinemaScreen(canvas, w, h);
            if (isAttachedToWindow()) postInvalidateOnAnimation();
            return;
        }
        if (!indoors && area < 0x80 && (module == 0x09 || module == 0x0B)) {
            lastOutX = linkX; lastOutY = linkY; lastOutArea = area;
            hasLastOutdoor = true;
        } else if (inHouse || special) {
            dungeonMode = false;
            if (hasLastOutdoor) {
                linkX = lastOutX; linkY = lastOutY; area = lastOutArea;
            } else {
                linkX = exitBuf[0]; linkY = exitBuf[1]; area = exitBuf[2];
            }
        }

        canvas.drawRect(0, 0, w, h, dungeonMode ? stonePaint : menuPaint);
        int tabH = (int) (96 * u);   // buttons sit above the system gesture zone
        int sideW = (int) (200 * u);
        mapAreaR.set(10 * u, 10 * u, w - sideW - 4 * u, h - tabH - 4 * u);

        if (tab == TAB_ITEMS) {
            drawItemsPanel(canvas, mapAreaR);
        } else if (tab == TAB_GEAR) {
            drawGearPanel(canvas, mapAreaR);
        } else if (tab == TAB_SETTINGS) {
            drawSettingsPanel(canvas, mapAreaR);
        } else if (dungeonMode) {
            drawDungeonMap(canvas, mapAreaR, linkX, linkY, area & 0xFF, dungeonInfo);
        } else {
            drawOverworld(canvas, mapAreaR, linkX, linkY, area);
        }
        drawSidebar(canvas, w - sideW + 4 * u, 10 * u, sideW - 14 * u, h - tabH - 14 * u, dungeonMode);
        drawTabBar(canvas, w, h, tabH);

        if (isAttachedToWindow()) postInvalidateOnAnimation();
    }

    // ---------- chrome helpers ----------

    /** ALttP menu-style box: black fill, colored double border with corner dots. */
    private void menuBox(Canvas c, RectF r, int borderColor) {
        fill.setColor(COL_BOX);
        c.drawRoundRect(r, 10 * u, 10 * u, fill);
        stroke.setStrokeWidth(4 * u);
        stroke.setColor(borderColor);
        c.drawRoundRect(r.left + 3 * u, r.top + 3 * u, r.right - 3 * u, r.bottom - 3 * u, 8 * u, 8 * u, stroke);
        stroke.setStrokeWidth(2 * u);
        stroke.setColor(Color.argb(160, 255, 255, 255));
        c.drawRoundRect(r.left + 7 * u, r.top + 7 * u, r.right - 7 * u, r.bottom - 7 * u, 6 * u, 6 * u, stroke);
        // corner dots (like the game's menu boxes)
        fill.setColor(Color.WHITE);
        float d = 3.5f * u;
        c.drawCircle(r.left + 8 * u, r.top + 8 * u, d, fill);
        c.drawCircle(r.right - 8 * u, r.top + 8 * u, d, fill);
        c.drawCircle(r.left + 8 * u, r.bottom - 8 * u, d, fill);
        c.drawCircle(r.right - 8 * u, r.bottom - 8 * u, d, fill);
    }

    private void drawSprite(Canvas c, Bitmap sheet, Rect s, float x, float y, float scale) {
        if (s == null) return;
        dst.set(x, y, x + s.width() * scale, y + s.height() * scale);
        c.drawBitmap(sheet, s, dst, bmp);
    }

    private void drawGlyph(Canvas c, String key, float x, float y, float s) {
        drawSprite(c, glyphs, glyphRects.get(key), x, y, s);
    }

    private void drawIcon(Canvas c, String key, float x, float y, float s) {
        drawSprite(c, icons, iconRects.get(key), x, y, s);
    }

    private void drawNumber(Canvas c, int value, int digits, float x, float y, float s, boolean yellow) {
        for (int i = digits - 1; i >= 0; i--) {
            drawGlyph(c, "digit" + (value % 10) + (yellow ? "y" : ""), x + i * 8 * s, y, s);
            value /= 10;
        }
    }

    private float drawText(Canvas c, String text, float x, float y, float s) {
        float cx = x;
        for (int i = 0; i < text.length(); i++) {
            char ch = text.charAt(i);
            if (ch == ' ') { cx += 5 * s; continue; }
            if (ch >= '0' && ch <= '9') { drawGlyph(c, "digit" + ch, cx, y, s); cx += 8 * s; continue; }
            drawSprite(c, letters, letterRects.get(String.valueOf(ch)), cx, y, s);
            cx += 8 * s;
        }
        return cx - x;
    }

    private float textWidth(String text, float s) {
        float w = 0;
        for (int i = 0; i < text.length(); i++) w += (text.charAt(i) == ' ' ? 5 : 8) * s;
        return w;
    }

    // ---------- overworld map ----------

    private void drawOverworld(Canvas c, RectF r, int linkX, int linkY, int area) {
        // parchment sheet with gold frame
        c.drawRect(r, parchPaint);
        stroke.setStrokeWidth(3 * u); stroke.setColor(COL_GOLD_DARK);
        c.drawRect(r.left + u, r.top + u, r.right - u, r.bottom - u, stroke);
        stroke.setStrokeWidth(2 * u); stroke.setColor(COL_GOLD);
        c.drawRect(r.left + 4 * u, r.top + 4 * u, r.right - 4 * u, r.bottom - 4 * u, stroke);

        float pad = 8 * u;
        RectF m = new RectF(r.left + pad, r.top + pad, r.right - pad, r.bottom - pad);

        boolean dark = (area & 0x40) != 0;
        Bitmap map = dark ? mapDark : mapLight;
        float px = 128f + (linkX / 4096f) * 256f;
        float py = 128f + (linkY / 4096f) * 256f;

        c.save();
        c.clipRect(m);
        float scale, ox, oy;
        if (wholeMap) {
            scale = Math.min(m.width(), m.height()) / 512f;
            ox = m.centerX() - 256f * scale;
            oy = m.centerY() - 256f * scale;
        } else {
            scale = 2.6f * u;
            float cxm = clamp(px, m.width() / scale / 2f, 512f - m.width() / scale / 2f);
            float cym = clamp(py, m.height() / scale / 2f, 512f - m.height() / scale / 2f);
            ox = m.centerX() - cxm * scale;
            oy = m.centerY() - cym * scale;
        }
        mapViewR.set(m);
        mapOx = ox; mapOy = oy; mapScale = scale;
        mapDarkWorld = dark;
        mapLive = true;
        src.set(0, 0, 512, 512);
        dst.set(ox, oy, ox + 512 * scale, oy + 512 * scale);
        c.drawBitmap(map, src, dst, bmp);

        int[][] marks = dark ? CRYSTAL_MARKS : PENDANT_MARKS;
        int owned = sram(dark ? 0x7A : 0x74);
        aa.setStyle(Paint.Style.STROKE);
        for (int[] mk : marks) {
            if ((owned & mk[0]) != 0) continue;
            float mx = ox + (128f + mk[1] / 4096f * 256f) * scale;
            float my = oy + (128f + mk[2] / 4096f * 256f) * scale;
            float rr = 8 * u;
            aa.setStrokeWidth(8 * u); aa.setColor(COL_OUTLINE);
            c.drawLine(mx - rr, my - rr, mx + rr, my + rr, aa);
            c.drawLine(mx - rr, my + rr, mx + rr, my - rr, aa);
            aa.setStrokeWidth(4.5f * u); aa.setColor(Color.rgb(224, 40, 32));
            c.drawLine(mx - rr, my - rr, mx + rr, my + rr, aa);
            c.drawLine(mx - rr, my + rr, mx + rr, my - rr, aa);
        }

        // the player's own pins, this world only, under Link's marker
        for (int i = 0; i < pinCount; i++) {
            if ((pinDark[i] != 0) != dark) continue;
            drawPin(c, ox + (128f + pinX[i] / 4096f * 256f) * scale,
                    oy + (128f + pinY[i] / 4096f * 256f) * scale,
                    (wholeMap ? 0.8f : 1.1f) * u);
        }

        float fx = ox + px * scale, fy = oy + py * scale;
        float bob = (float) Math.sin(System.nanoTime() / 3.0e8) * 2f * u;
        float fs = (wholeMap ? 1.2f : 1.6f) * u;
        drawSprite(c, linkFace, faceSrc, fx - 16 * fs, fy - 16 * fs + bob, fs);
        c.restore();

        // pixel-style zoom button (menu-box look)
        float bs2 = 56 * u;
        dst.set(r.left + 14 * u, r.top + 14 * u, r.left + 14 * u + bs2, r.top + 14 * u + bs2);
        mapZoomR.set(dst);   // a long press here is the zoom, not a pin
        fill.setColor(COL_BOX);
        c.drawRoundRect(dst, 8 * u, 8 * u, fill);
        stroke.setStrokeWidth(3 * u); stroke.setColor(COL_BOX_BORDER2);
        c.drawRoundRect(dst.left + 3 * u, dst.top + 3 * u, dst.right - 3 * u, dst.bottom - 3 * u, 6 * u, 6 * u, stroke);
        fill.setColor(Color.WHITE);
        float cxb = dst.centerX(), cyb = dst.centerY(), arm = 14 * u, th = 5 * u;
        c.drawRect(cxb - arm, cyb - th / 2, cxb + arm, cyb + th / 2, fill);
        if (wholeMap) c.drawRect(cxb - th / 2, cyb - arm, cxb + th / 2, cyb + arm, fill);

        // a hold-to-pin nudge along the bottom edge, since nothing else would
        // hint at it; it goes away as soon as the player has a pin anywhere
        if (pinCount == 0) {
            String hint = "HOLD TO PIN";
            float hs = 1.8f * u, hw = textWidth(hint, hs);
            dst.set(r.centerX() - hw / 2 - 12 * u, r.bottom - 44 * u,
                    r.centerX() + hw / 2 + 12 * u, r.bottom - 14 * u);
            fill.setColor(COL_BOX);
            c.drawRoundRect(dst, 6 * u, 6 * u, fill);
            stroke.setStrokeWidth(2 * u); stroke.setColor(COL_GOLD_DARK);
            c.drawRoundRect(dst, 6 * u, 6 * u, stroke);
            drawText(c, hint, dst.centerX() - hw / 2, dst.centerY() - 4 * hs, hs);
        }
    }

    /** A player-dropped pin: gold head on a dark stem, tip at the marked spot. */
    private void drawPin(Canvas c, float x, float y, float s) {
        float hy = y - 13 * s;   // the head sits above the spot it marks
        aa.setStyle(Paint.Style.STROKE);
        aa.setStrokeWidth(7 * s); aa.setColor(COL_OUTLINE);
        c.drawLine(x, y, x, hy, aa);
        aa.setStyle(Paint.Style.FILL);
        aa.setColor(COL_OUTLINE);
        c.drawCircle(x, hy, 8.5f * s, aa);
        aa.setStyle(Paint.Style.STROKE);
        aa.setStrokeWidth(3 * s); aa.setColor(COL_GOLD_DARK);
        c.drawLine(x, y, x, hy, aa);
        aa.setStyle(Paint.Style.FILL);
        aa.setColor(COL_GOLD);
        c.drawCircle(x, hy, 6.5f * s, aa);
        aa.setColor(COL_OUTLINE);
        c.drawCircle(x, hy, 2.5f * s, aa);
    }

    /**
     * Long press on the overworld map: remove the pin under the finger, else
     * drop a new one there. Screen pixels in, world coordinates out - the map
     * bitmap is 512x512 with the 4096x4096 world filling its middle 256x256.
     * Returns false when nothing happened (off the map, on the zoom button, or
     * the list is full).
     */
    private boolean togglePin(float x, float y) {
        if (!mapLive || !mapViewR.contains(x, y) || mapZoomR.contains(x, y)) return false;
        int dark = mapDarkWorld ? 1 : 0;
        float hit = 26 * u;
        for (int i = 0; i < pinCount; i++) {
            if (pinDark[i] != dark) continue;
            float sx = mapOx + (128f + pinX[i] / 4096f * 256f) * mapScale;
            float sy = mapOy + (128f + pinY[i] / 4096f * 256f) * mapScale;
            if (Math.abs(sx - x) > hit || Math.abs(sy - y) > hit) continue;
            for (int k = i; k < pinCount - 1; k++) {
                pinDark[k] = pinDark[k + 1];
                pinX[k] = pinX[k + 1];
                pinY[k] = pinY[k + 1];
            }
            pinCount--;
            savePins();
            return true;
        }
        if (pinCount >= MAX_PINS) return false;
        int wx = (int) (((x - mapOx) / mapScale - 128f) * 16f);
        int wy = (int) (((y - mapOy) / mapScale - 128f) * 16f);
        if (wx < 0 || wx > 4095 || wy < 0 || wy > 4095) return false;   // the map's blank border
        pinDark[pinCount] = dark;
        pinX[pinCount] = wx;
        pinY[pinCount] = wy;
        pinCount++;
        savePins();
        return true;
    }

    // Pins live in map_pins.txt next to zelda3.ini, one "world,x,y" per line.
    private void loadPins() {
        try {
            java.io.File f = new java.io.File(getContext().getExternalFilesDir(null), "map_pins.txt");
            java.io.BufferedReader in = new java.io.BufferedReader(new java.io.FileReader(f));
            String line;
            while ((line = in.readLine()) != null && pinCount < MAX_PINS) {
                String[] p = line.split(",");
                if (p.length != 3) continue;
                try {
                    int d = Integer.parseInt(p[0].trim());
                    int x = Integer.parseInt(p[1].trim());
                    int y = Integer.parseInt(p[2].trim());
                    if (d < 0 || d > 1 || x < 0 || x > 4095 || y < 0 || y > 4095) continue;
                    pinDark[pinCount] = d;
                    pinX[pinCount] = x;
                    pinY[pinCount] = y;
                    pinCount++;
                } catch (NumberFormatException e) {
                    // garbled line: skip it and keep the rest
                }
            }
            in.close();
        } catch (java.io.IOException e) {
            // no pins yet
        }
    }

    private void savePins() {
        try {
            java.io.File f = new java.io.File(getContext().getExternalFilesDir(null), "map_pins.txt");
            java.io.FileWriter out = new java.io.FileWriter(f);
            for (int i = 0; i < pinCount; i++)
                out.write(pinDark[i] + "," + pinX[i] + "," + pinY[i] + "\n");
            out.close();
        } catch (java.io.IOException e) {
            Log.w(TAG, "failed to save map pins", e);
        }
    }

    private static float clamp(float v, float lo, float hi) {
        return v < lo ? lo : Math.min(v, hi);
    }

    // ---------- dungeon map ----------

    private void drawDungeonMap(Canvas c, RectF r, int linkX, int linkY, int room, int dungeonInfo) {
        int palace = dungeonInfo & 0xFF;
        int floor = (byte) (dungeonInfo >> 8);
        Dungeon d = (dungeons != null && palace < dungeons.length) ? dungeons[palace] : null;

        float bs = 3 * u;
        String name = d != null ? d.name : "DUNGEON";
        float tw = textWidth(name, bs);
        float bx = r.centerX() - tw / 2, by = r.top + 20 * u;
        fill.setColor(COL_STONE_INSET);
        dst.set(bx - 20 * u, by - 9 * u, bx + tw + 20 * u, by + 8 * bs + 9 * u);
        c.drawRoundRect(dst, 8 * u, 8 * u, fill);
        stroke.setStrokeWidth(2 * u); stroke.setColor(COL_STONE_EDGE_L);
        c.drawRoundRect(dst, 8 * u, 8 * u, stroke);
        drawText(c, name, bx, by, bs);

        if (d == null) return;

        // drop the preview once Link changes floors or dungeons: follow him again
        if (viewFloorSel != NO_FLOOR_SEL && (viewFloorPalace != palace || viewFloorFrom != floor))
            viewFloorSel = NO_FLOOR_SEL;
        int li = (viewFloorSel != NO_FLOOR_SEL ? viewFloorSel : floor) + d.basements;
        li = Math.max(0, Math.min(li, d.floors - 1));
        int viewFloor = li - d.basements;

        plaqueCount = 0;
        float ph = 50 * u, pw = 100 * u, pgap = 8 * u;
        float px0 = r.left + 24 * u;
        float py0 = r.top + 78 * u;
        for (int f = d.floors - 1; f >= 0; f--) {
            int fl = f - d.basements;
            if (plaqueCount >= plaqueR.length) break;
            RectF pr = plaqueR[plaqueCount];
            pr.set(px0, py0, px0 + pw, py0 + ph);
            plaqueFloor[plaqueCount] = fl;
            plaqueCount++;
            boolean sel = (fl == viewFloor);
            fill.setColor(sel ? COL_PLAQUE_SEL : COL_PLAQUE);
            c.drawRoundRect(pr, 6 * u, 6 * u, fill);
            stroke.setStrokeWidth(2 * u);
            // gold edge keeps Link's floor recognizable while another one is previewed
            stroke.setColor(sel ? Color.rgb(160, 200, 255) : fl == floor ? COL_GOLD : COL_STONE_EDGE_L);
            c.drawRoundRect(pr, 6 * u, 6 * u, stroke);
            String label = fl >= 0 ? (fl + 1) + "F" : "B" + (-fl);
            drawText(c, label, pr.centerX() - textWidth(label, 2 * u) / 2 + 8 * u, pr.centerY() - 8 * u, 2 * u);
            if (fl == floor) {
                drawSprite(c, linkFace, faceSrc, pr.left + 4 * u, pr.centerY() - 13 * u, 0.85f * u);
            }
            py0 += ph + pgap;
        }

        float inset = 20 * u;
        float mx0 = px0 + pw + 28 * u, my0 = r.top + 74 * u;
        float availW = r.right - inset - mx0, availH = r.bottom - inset - my0;
        float msize = Math.min(availW, availH);
        // center the map square in whatever space the aspect ratio leaves over
        mx0 += (availW - msize) / 2;
        my0 += (availH - msize) / 2;
        RectF mp = new RectF(mx0, my0, mx0 + msize, my0 + msize);
        fill.setColor(COL_STONE_INSET);
        c.drawRoundRect(mp, 10 * u, 10 * u, fill);
        stroke.setStrokeWidth(3 * u); stroke.setColor(COL_STONE_EDGE_D);
        c.drawRoundRect(mp, 10 * u, 10 * u, stroke);

        byte[] lay = d.layout[li];
        float cell = (msize - 24 * u) / 5f;
        float gx = mp.left + 12 * u, gy = mp.top + 12 * u;

        // the floor's rooms drawn with the game's own map tiles
        if (!GameState.renderDungeonFloor(palace, li, dungFloorBuf)) return;
        if (dungFloorBmp == null) dungFloorBmp = Bitmap.createBitmap(80, 80, Bitmap.Config.ARGB_8888);
        dungFloorBmp.setPixels(dungFloorBuf, 0, 80, 0, 0, 80, 80);
        src.set(0, 0, 80, 80);
        dst.set(gx, gy, gx + 5 * cell, gy + 5 * cell);
        c.drawBitmap(dungFloorBmp, src, dst, bmp);

        // the game's own map overlay sprites: blinking room dot + boss skull
        boolean icons = GameState.renderMapIcons(palace, mapIconBuf);
        if (icons) {
            if (mapIconBmp == null) mapIconBmp = Bitmap.createBitmap(32, 8, Bitmap.Config.ARGB_8888);
            mapIconBmp.setPixels(mapIconBuf, 0, 32, 0, 0, 32, 8);
        }
        boolean hasCompass = (u16(0x64) & (0x8000 >> palace)) != 0;
        long frame = System.nanoTime() / 16_666_667L;
        float ms = cell / 16f;

        for (int i = 0; i < 25; i++) {
            int v = lay[i] & 0xFF;
            if (v == 0x0F) continue;
            int col = i % 5, row = i / 5;
            float x = gx + col * cell, y = gy + row * cell;
            boolean isCur = (v == (room & 0xFF)) && viewFloor == floor;

            if (icons && hasCompass && palace >= 2 && v == d.boss
                    && (dungFlag(v) & 0x800) == 0 && (frame & 0xF) < 10) {
                int pos = DUNGEON_BOSS_POS[palace];
                float sx = x + (pos >> 8) * ms, sy = y + (pos & 0xFF) * ms;
                src.set(24, 0, 32, 8);
                dst.set(sx, sy, sx + 8 * ms, sy + 8 * ms);
                c.drawBitmap(mapIconBmp, src, dst, bmp);
            }

            if (isCur) {
                // corner brackets rather than a full box: door pips sit
                // mid-edge on the room border, a full outline covers them (#7)
                stroke.setStrokeWidth(3 * u);
                stroke.setColor(COL_GOLD);
                float in = 1.5f * u, len = cell * 0.28f;
                float bx0 = x + in, by0 = y + in, bx1 = x + cell - in, by1 = y + cell - in;
                c.drawLine(bx0 - in, by0, bx0 + len, by0, stroke);
                c.drawLine(bx0, by0 - in, bx0, by0 + len, stroke);
                c.drawLine(bx1 + in, by0, bx1 - len, by0, stroke);
                c.drawLine(bx1, by0 - in, bx1, by0 + len, stroke);
                c.drawLine(bx0 - in, by1, bx0 + len, by1, stroke);
                c.drawLine(bx0, by1 + in, bx0, by1 - len, stroke);
                c.drawLine(bx1 + in, by1, bx1 - len, by1, stroke);
                c.drawLine(bx1, by1 + in, bx1, by1 - len, stroke);
                if (icons) {
                    int p = DOT_PALETTE[(int) (frame >> 2) & 3];
                    float sx = x + (((linkX & 0x1E0) >> 5) - 3) * ms;
                    float sy = y + (((linkY & 0x1E0) >> 5) - 3) * ms;
                    src.set(p * 8, 0, p * 8 + 8, 8);
                    dst.set(sx, sy, sx + 8 * ms, sy + 8 * ms);
                    c.drawBitmap(mapIconBmp, src, dst, bmp);
                }
            }
        }
    }

    private int dungFlag(int roomLowByte) {
        int off = roomLowByte * 2;
        if (off + 1 >= dungFlags.length) return 0;
        return (dungFlags[off] & 0xFF) | ((dungFlags[off + 1] & 0xFF) << 8);
    }

    // ---------- settings ----------

    private void drawSettingsPanel(Canvas c, RectF r) {
        menuBox(c, r, COL_BOX_BORDER);
        if (remapMode) {
            drawRemapPanel(c, r);
            return;
        }
        if (statesMode) {
            drawStatesPanel(c, r);
            return;
        }
        drawText(c, "SETTINGS", r.centerX() - textWidth("SETTINGS", 3 * u) / 2, r.top + 18 * u, 3 * u);

        boolean ws = false, crt = false, hudHidden = false;
        int feats = 0;
        if (!nativeBroken) {
            ws = GameState.isWidescreen();
            crt = GameState.isCrtFilter();
            hudHidden = GameState.isHudHidden();
            feats = GameState.getFeatures();
        }
        int n = settingsRowR.length;
        float rowH = 76 * u, gap = 18 * u;
        settingsListTop = r.top + 56 * u;
        settingsListBot = r.bottom - 16 * u;
        settingsMaxScroll = Math.max(0, n * (rowH + gap) - gap - (settingsListBot - settingsListTop));
        settingsScroll = clamp(settingsScroll, 0, settingsMaxScroll);

        c.save();
        c.clipRect(r.left + 12 * u, settingsListTop, r.right - 12 * u, settingsListBot);
        float y0 = settingsListTop - settingsScroll;
        for (int i = 0; i < n; i++) {
            RectF row = settingsRowR[i];
            row.set(r.left + 28 * u, y0 + i * (rowH + gap), r.right - 28 * u, y0 + i * (rowH + gap) + rowH);
            if (row.bottom < settingsListTop || row.top > settingsListBot) continue;
            fill.setColor(Color.rgb(28, 28, 28));
            c.drawRoundRect(row, 8 * u, 8 * u, fill);
            stroke.setStrokeWidth(3 * u); stroke.setColor(COL_GOLD_DARK);
            c.drawRoundRect(row, 8 * u, 8 * u, stroke);
            float ty = row.centerY() - 12 * u;
            String label, v;
            if (i == 0) { label = "SAVE STATES"; v = null; }
            else if (i == 1) { label = "REMAP BUTTONS"; v = null; }
            else if (i == 2) { label = "SAVE STATE"; v = rowFlash(i, ""); }
            else if (i == 3) { label = "LOAD STATE"; v = rowFlash(i, stateFile().exists() ? "" : "EMPTY"); }
            else if (i == 4) { label = "AUTOSAVE"; v = autosave ? "ON" : "OFF"; }
            else if (i == 5) { label = "WIDESCREEN"; v = ws ? "ON" : "OFF"; }
            else if (i == 6) { label = "CRT FILTER"; v = crt ? "ON" : "OFF"; }
            else if (i == 7) { label = "TOP SCREEN HUD"; v = hudHidden ? "OFF" : "ON"; }
            else if (i == 8) { label = "X ITEM RING"; v = xRing ? "ON" : "OFF"; }
            else if (i == 9) {
                label = "SWAP SCREENS";
                v = swapScreens != swapScreensApplied ? "RESTART" : (swapScreens ? "ON" : "OFF");
            }
            else if (i == 10) {
                label = "MSU-1 MUSIC";
                v = !msuPack ? "NO PACK"
                        : msuOn != msuOnApplied ? "RESTART" : (msuOn ? "ON" : "OFF");
            }
            else {
                int f = i - 11;
                label = FEAT_LABELS[f];
                v = (((feats & FEAT_MASKS[f]) != 0) ^ FEAT_INVERT[f]) ? "ON" : "OFF";
            }
            drawText(c, label, row.left + 22 * u, ty, 3 * u);
            if (v == null) {
                // chevron for the remap sub-screen
                aa.setStyle(Paint.Style.STROKE); aa.setStrokeWidth(5 * u); aa.setColor(COL_GOLD);
                float ax = row.right - 40 * u, ay = row.centerY();
                c.drawLine(ax - 8 * u, ay - 12 * u, ax + 6 * u, ay, aa);
                c.drawLine(ax + 6 * u, ay, ax - 8 * u, ay + 12 * u, aa);
            } else {
                drawText(c, v, row.right - 22 * u - textWidth(v, 3 * u), ty, 3 * u);
            }
        }
        c.restore();

        // scrollbar hint
        if (settingsMaxScroll > 0) {
            float span = settingsListBot - settingsListTop;
            float thumbH = Math.max(40 * u, span * span / (span + settingsMaxScroll));
            float thumbY = settingsListTop + (span - thumbH) * (settingsScroll / settingsMaxScroll);
            fill.setColor(Color.rgb(28, 28, 28));
            dst.set(r.right - 20 * u, settingsListTop, r.right - 14 * u, settingsListBot);
            c.drawRoundRect(dst, 3 * u, 3 * u, fill);
            fill.setColor(COL_GOLD_DARK);
            dst.set(r.right - 20 * u, thumbY, r.right - 14 * u, thumbY + thumbH);
            c.drawRoundRect(dst, 3 * u, 3 * u, fill);
        }
    }

    // A tap on the settings list (from onTouchEvent, once it's known not to be a drag).
    private void settingsTap(float x, float y) {
        if (nativeBroken || y < settingsListTop || y > settingsListBot) return;
        for (int i = 0; i < settingsRowR.length; i++) {
            if (!settingsRowR[i].contains(x, y)) continue;
            if (i == 0) {
                scanStateSlots();
                statesMode = true;
            } else if (i == 1) {
                GameState.getGamepadControls(padControls);
                remapMode = true;
            } else if (i == 2) {
                stateFile().getParentFile().mkdirs();
                GameState.saveState();
                rowFlashSet(i, "SAVED");
            } else if (i == 3) {
                if (stateFile().exists()) {
                    GameState.loadState();
                    rowFlashSet(i, "LOADED");
                } else {
                    rowFlashSet(i, "EMPTY");
                }
            } else if (i == 4) {
                autosave = !autosave;
                GameState.setAutosave(autosave);
                updateIni("[General]", "Autosave", autosave ? "1" : "0");
            } else if (i == 5) {
                boolean on = !GameState.isWidescreen();
                GameState.setWidescreen(on);
                updateIni("[General]", "ExtendedAspectRatio", on ? "16:9" : "4:3");
            } else if (i == 6) {
                boolean on = !GameState.isCrtFilter();
                GameState.setCrtFilter(on);
                updateIni("[Graphics]", "CrtFilter", on ? "1" : "0");
            } else if (i == 7) {
                boolean hide = !GameState.isHudHidden();
                GameState.setHudHidden(hide);
                getContext().getSharedPreferences("secondscreen", 0)
                        .edit().putBoolean("hideTopHud", hide).apply();
            } else if (i == 8) {
                xRing = !xRing;
                armedRing = 0;
                updateIni("[General]", "SecondScreenXItemRing", xRing ? "1" : "0");
                // the X item only works with the ItemSwitchLR feature; enabling
                // the ring without it would show a dead circle
                if (xRing && (GameState.getFeatures() & FEAT_MASKS[0]) == 0) {
                    GameState.setFeature(FEAT_MASKS[0], true);
                    updateIni(FEAT_SECTIONS[0], FEAT_KEYS[0], "1");
                }
            } else if (i == 9) {
                // needs the game window rebuilt on the other display, which only
                // happens at activity launch - the row shows RESTART until then
                swapScreens = !swapScreens;
                updateIni("[General]", "SecondScreenSwap", swapScreens ? "1" : "0");
            } else if (i == 10) {
                // with no pack installed there is nothing to switch on, so the
                // row just reads NO PACK and does nothing
                if (!msuPack) return;
                // the MSU player only picks up EnableMSU when the emulator boots,
                // and switching it off mid-track would leave the current .pcm
                // playing over a paused SPC - so this waits for a restart too
                msuOn = !msuOn;
                updateIni("[Sound]", "EnableMSU", msuOn ? msuOnValue : "false");
            } else {
                int f = i - 11;
                boolean on = (GameState.getFeatures() & FEAT_MASKS[f]) == 0;
                GameState.setFeature(FEAT_MASKS[f], on);
                updateIni(FEAT_SECTIONS[f], FEAT_KEYS[f], on ? "1" : "0");
                // the X ring depends on ItemSwitchLR: turning the feature off
                // would leave a ring that arms but assigns nothing
                if (f == 0 && !on && xRing) {
                    xRing = false;
                    armedRing = 0;
                    updateIni("[General]", "SecondScreenXItemRing", "0");
                }
            }
            return;
        }
    }

    private void drawRemapPanel(Canvas c, RectF r) {
        drawText(c, "REMAP BUTTONS", r.centerX() - textWidth("REMAP BUTTONS", 3 * u) / 2, r.top + 18 * u, 3 * u);
        remapBackR.set(r.left + 20 * u, r.top + 12 * u, r.left + 110 * u, r.top + 50 * u);
        fill.setColor(Color.rgb(28, 28, 28));
        c.drawRoundRect(remapBackR, 8 * u, 8 * u, fill);
        stroke.setStrokeWidth(3 * u); stroke.setColor(COL_GOLD_DARK);
        c.drawRoundRect(remapBackR, 8 * u, 8 * u, stroke);
        drawText(c, "BACK", remapBackR.centerX() - textWidth("BACK", 2.2f * u) / 2,
                remapBackR.centerY() - 9 * u, 2.2f * u);

        // resolve a pending capture from the game thread
        if (remapArm >= 0 && !nativeBroken) {
            int b = GameState.getCapturedButton();
            if (b >= 0) {
                padControls[remapArm] = b;
                GameState.setGamepadControls(padControls);
                writeIniGamepadControls();
                remapArm = -1;
            } else if (b == -1 || System.nanoTime() - remapArmAt > 8_000_000_000L) {
                GameState.armButtonCapture(false);
                remapArm = -1;
            }
        }

        float rowH = 58 * u, gap = 12 * u;
        float colW = (r.width() - 3 * 24 * u) / 2;
        float y0 = r.top + 70 * u;
        for (int i = 0; i < 12; i++) {
            int col = i / 6, rowI = i % 6;
            float x = r.left + 24 * u + col * (colW + 24 * u);
            float y = y0 + rowI * (rowH + gap);
            RectF row = remapRowR[i];
            row.set(x, y, x + colW, y + rowH);
            boolean armed = remapArm == i;
            fill.setColor(armed ? Color.rgb(58, 48, 12) : Color.rgb(28, 28, 28));
            c.drawRoundRect(row, 8 * u, 8 * u, fill);
            stroke.setStrokeWidth(3 * u); stroke.setColor(armed ? COL_GOLD : COL_GOLD_DARK);
            c.drawRoundRect(row, 8 * u, 8 * u, stroke);
            float ty = row.centerY() - 9 * u;
            drawText(c, PAD_CMD_NAMES[i], row.left + 14 * u, ty, 2.2f * u);
            String v = armed ? "PRESS KEY"
                    : (padControls[i] >= 0 && padControls[i] < GameState.PAD_BUTTON_LABEL.length
                       ? GameState.PAD_BUTTON_LABEL[padControls[i]] : "----");
            drawText(c, v, row.right - 14 * u - textWidth(v, 2.2f * u), ty, 2.2f * u);
        }
    }

    // The settings-screen save state; slot 0 is reserved for the Autosave option.
    private java.io.File stateFile() {
        return new java.io.File(getContext().getExternalFilesDir(null), "saves/save1.sav");
    }

    private void rowFlashSet(int row, String text) {
        settingsFlashRow = row;
        settingsFlash = text;
        settingsFlashUntil = System.nanoTime() + 1_200_000_000L;
    }

    private String rowFlash(int row, String dflt) {
        return settingsFlashRow == row && System.nanoTime() < settingsFlashUntil
                ? settingsFlash : dflt;
    }

    // ---------- save states ----------

    // The SAVE STATES sub-screen: four slots, each showing the frame it was
    // saved on with its own SAVE and LOAD button.
    private void drawStatesPanel(Canvas c, RectF r) {
        drawText(c, "SAVE STATES", r.centerX() - textWidth("SAVE STATES", 3 * u) / 2, r.top + 18 * u, 3 * u);
        statesBackR.set(r.left + 20 * u, r.top + 12 * u, r.left + 110 * u, r.top + 50 * u);
        fill.setColor(Color.rgb(28, 28, 28));
        c.drawRoundRect(statesBackR, 8 * u, 8 * u, fill);
        stroke.setStrokeWidth(3 * u); stroke.setColor(COL_GOLD_DARK);
        c.drawRoundRect(statesBackR, 8 * u, 8 * u, stroke);
        drawText(c, "BACK", statesBackR.centerX() - textWidth("BACK", 2.2f * u) / 2,
                statesBackR.centerY() - 9 * u, 2.2f * u);

        float pad = 14 * u, gap = 20 * u;
        float y0 = r.top + 66 * u;
        float colW = (r.width() - 3 * gap) / 2;
        float rowH = (r.bottom - 20 * u - y0 - gap) / 2;
        boolean flashing = System.nanoTime() < stateFlashUntil;
        for (int i = 0; i < STATE_SLOTS; i++) {
            float cx = r.left + gap + (i % 2) * (colW + gap);
            float cy = y0 + (i / 2) * (rowH + gap);
            boolean lit = flashing && stateFlashSlot == i;
            dst.set(cx, cy, cx + colW, cy + rowH);
            fill.setColor(Color.rgb(28, 28, 28));
            c.drawRoundRect(dst, 8 * u, 8 * u, fill);
            stroke.setStrokeWidth(3 * u); stroke.setColor(lit ? COL_GOLD : COL_GOLD_DARK);
            c.drawRoundRect(dst, 8 * u, 8 * u, stroke);

            drawText(c, "SLOT " + (i + 1), cx + pad, cy + pad, 2.4f * u);
            String note = lit ? stateFlashMsg : (stateStamp[i] == 0 ? "" : stamp(stateStamp[i]));
            drawText(c, note, cx + colW - pad - textWidth(note, 2 * u), cy + pad + 2 * u, 2 * u);

            // the moment the slot was written, in the game's own 8:7 shape;
            // narrow panels shrink it so the buttons keep a usable width
            float th = rowH - 2 * pad - 34 * u;
            float tw = th * GameState.THUMB_W / GameState.THUMB_H;
            float twMax = (colW - 3 * pad) * 0.55f;
            if (tw > twMax) { tw = twMax; th = tw * GameState.THUMB_H / GameState.THUMB_W; }
            float tx = cx + pad, ty = cy + pad + 34 * u;
            dst.set(tx, ty, tx + tw, ty + th);
            fill.setColor(Color.rgb(10, 10, 10));
            c.drawRect(dst, fill);
            if (stateThumb[i] != null) {
                src.set(0, 0, stateThumb[i].getWidth(), stateThumb[i].getHeight());
                c.drawBitmap(stateThumb[i], src, dst, bmp);
            } else {
                String e = stateStamp[i] == 0 ? "EMPTY" : "NO IMAGE";
                drawText(c, e, dst.centerX() - textWidth(e, 2 * u) / 2, dst.centerY() - 8 * u, 2 * u);
            }
            stroke.setStrokeWidth(2 * u); stroke.setColor(COL_GOLD_DARK);
            c.drawRect(dst, stroke);

            // SAVE is always live; LOAD only once the slot holds a state
            float bx = tx + tw + pad, bw = cx + colW - pad - bx, bh = (th - pad) / 2;
            stateSaveR[i].set(bx, ty, bx + bw, ty + bh);
            stateLoadR[i].set(bx, ty + bh + pad, bx + bw, ty + th);
            drawStateButton(c, stateSaveR[i], "SAVE", true);
            drawStateButton(c, stateLoadR[i], "LOAD", stateStamp[i] != 0);
        }
    }

    private void drawStateButton(Canvas c, RectF b, String label, boolean enabled) {
        fill.setColor(enabled ? Color.rgb(40, 40, 40) : Color.rgb(20, 20, 20));
        c.drawRoundRect(b, 8 * u, 8 * u, fill);
        stroke.setStrokeWidth(3 * u); stroke.setColor(enabled ? COL_GOLD_DARK : Color.rgb(52, 46, 30));
        c.drawRoundRect(b, 8 * u, 8 * u, stroke);
        bmp.setAlpha(enabled ? 255 : 90);
        drawText(c, label, b.centerX() - textWidth(label, 2.4f * u) / 2, b.centerY() - 10 * u, 2.4f * u);
        bmp.setAlpha(255);
    }

    // The pixel font only has A-Z and digits, so a stamp reads "JUL 24 14 32".
    private static String stamp(long ms) {
        java.util.Calendar cal = java.util.Calendar.getInstance();
        cal.setTimeInMillis(ms);
        return String.format(java.util.Locale.US, "%s %02d %02d %02d",
                MONTHS[cal.get(java.util.Calendar.MONTH)], cal.get(java.util.Calendar.DAY_OF_MONTH),
                cal.get(java.util.Calendar.HOUR_OF_DAY), cal.get(java.util.Calendar.MINUTE));
    }

    private java.io.File savesDir() {
        return new java.io.File(getContext().getExternalFilesDir(null), "saves");
    }

    /** Re-read which of the picker's slots hold a state, plus their thumbnails. */
    private void scanStateSlots() {
        java.io.File dir = savesDir();
        for (int i = 0; i < STATE_SLOTS; i++) {
            java.io.File sav = new java.io.File(dir, "save" + (STATE_SLOT0 + i) + ".sav");
            stateStamp[i] = sav.isFile() && sav.length() > 0 ? sav.lastModified() : 0;
            java.io.File png = new java.io.File(dir, "save" + (STATE_SLOT0 + i) + ".png");
            stateThumb[i] = stateStamp[i] != 0 && png.isFile()
                    ? BitmapFactory.decodeFile(png.getPath()) : null;
        }
    }

    private void requestState(int i, boolean save) {
        if (save) {
            GameState.requestSaveState(STATE_SLOT0 + i);
            thumbWantSlot = i;
            thumbWantAt = System.nanoTime();
        } else {
            GameState.requestLoadState(STATE_SLOT0 + i);
        }
        stateFlashSlot = i;
        stateFlashMsg = save ? "SAVED" : "LOADED";
        stateFlashUntil = System.nanoTime() + 1_200_000_000L;
    }

    // A save asks the game thread for a grab of the frame it was taken on; that
    // lands a frame or two later, so it is picked up here (from onDraw, so it
    // still completes if the panel is left right after tapping SAVE) and written
    // next to the .sav as saveN.png.
    private void pollStateThumbnail() {
        if (thumbWantSlot < 0) return;
        if (GameState.takeStateThumbnail(thumbBuf)) {
            int i = thumbWantSlot;
            thumbWantSlot = -1;
            java.io.File dir = savesDir();
            Bitmap b = Bitmap.createBitmap(thumbBuf, GameState.THUMB_W, GameState.THUMB_H,
                    Bitmap.Config.ARGB_8888);
            java.io.File png = new java.io.File(dir, "save" + (STATE_SLOT0 + i) + ".png");
            try (java.io.FileOutputStream out = new java.io.FileOutputStream(png)) {
                b.compress(Bitmap.CompressFormat.PNG, 100, out);
            } catch (java.io.IOException e) {
                Log.w(TAG, "failed to write " + png, e);
            }
            stateThumb[i] = b;
            stateStamp[i] = new java.io.File(dir, "save" + (STATE_SLOT0 + i) + ".sav").lastModified();
        } else if (System.nanoTime() - thumbWantAt > 3_000_000_000L) {
            thumbWantSlot = -1;   // no frame came (game paused?); the .sav is still there
            scanStateSlots();
        }
    }

    // Rewrite one `key = value` line inside a section of the user's zelda3.ini.
    private void updateIni(String section, String key, String value) {
        try {
            java.io.File f = new java.io.File(getContext().getExternalFilesDir(null), "zelda3.ini");
            java.io.BufferedReader in = new java.io.BufferedReader(new java.io.FileReader(f));
            StringBuilder sb = new StringBuilder();
            String line, cur = "";
            boolean done = false;
            while ((line = in.readLine()) != null) {
                String t = line.trim();
                if (t.startsWith("["))
                    cur = t;
                else if (!done && cur.equalsIgnoreCase(section)
                        && t.toLowerCase().startsWith(key.toLowerCase())
                        && t.substring(key.length()).trim().startsWith("=")) {
                    line = key + " = " + value;
                    done = true;
                }
                sb.append(line).append('\n');
            }
            in.close();
            if (!done) {
                int at = sb.indexOf(section + "\n");
                if (at >= 0) sb.insert(at + section.length() + 1, key + " = " + value + "\n");
                else sb.append(section).append('\n').append(key).append(" = ").append(value).append('\n');
            }
            java.io.FileWriter out = new java.io.FileWriter(f);
            out.write(sb.toString());
            out.close();
        } catch (java.io.IOException e) {
            Log.w(TAG, "failed to update zelda3.ini", e);
        }
    }

    // Read one `key = value` from a section of the user's zelda3.ini.
    private String readIni(String section, String key) {
        try {
            java.io.File f = new java.io.File(getContext().getExternalFilesDir(null), "zelda3.ini");
            java.io.BufferedReader in = new java.io.BufferedReader(new java.io.FileReader(f));
            String line, cur = "", v = null;
            while ((line = in.readLine()) != null) {
                String t = line.trim();
                if (t.startsWith("["))
                    cur = t;
                else if (cur.equalsIgnoreCase(section)
                        && t.toLowerCase().startsWith(key.toLowerCase())
                        && t.substring(key.length()).trim().startsWith("=")) {
                    v = t.substring(t.indexOf('=') + 1).trim();
                }
            }
            in.close();
            return v;
        } catch (java.io.IOException e) {
            return null;
        }
    }

    private boolean readIniBool(String section, String key) {
        String s = readIni(section, key);
        return s != null && (s.equals("1") || s.equalsIgnoreCase("on")
                || s.equalsIgnoreCase("true") || s.equalsIgnoreCase("yes"));
    }

    // EnableMSU also accepts deluxe/opuz/deluxe-opuz, so treat anything that
    // isn't an explicit "off" as enabled (config.c ParseBool does the same).
    private static boolean iniEnabled(String s) {
        if (s == null) return false;
        s = s.trim();
        return !(s.isEmpty() || s.equals("0") || s.equalsIgnoreCase("false")
                || s.equalsIgnoreCase("off") || s.equalsIgnoreCase("no"));
    }

    // Is there anything for the MSU player to load? MSUPath is a filename prefix
    // ("msu/alttp_msu-"), relative to the folder holding zelda3.ini, and the
    // player appends the track number plus .pcm or .opuz.
    private boolean msuPackPresent() {
        String p = readIni("[Sound]", "MSUPath");
        if (p == null || p.isEmpty()) p = "msu/alttp_msu-";
        java.io.File f = p.startsWith("/") ? new java.io.File(p)
                : new java.io.File(getContext().getExternalFilesDir(null), p);
        boolean isDir = p.endsWith("/");
        java.io.File dir = isDir ? f : f.getParentFile();
        String prefix = isDir ? "" : f.getName();
        String[] names = dir == null ? null : dir.list();
        if (names == null) return false;
        for (String n : names) {
            if (n.startsWith(prefix) && (n.endsWith(".pcm") || n.endsWith(".opuz")))
                return true;
        }
        return false;
    }

    private void writeIniGamepadControls() {
        StringBuilder v = new StringBuilder();
        for (int i = 0; i < 12; i++) {
            if (i > 0) v.append(", ");
            if (padControls[i] >= 0 && padControls[i] < GameState.PAD_BUTTON_INI.length)
                v.append(GameState.PAD_BUTTON_INI[padControls[i]]);
        }
        updateIni("[GamepadMap]", "Controls", v.toString());
    }

    // ---------- sidebar ----------

    private void drawSidebar(Canvas c, float x, float y, float w, float h, boolean dungeonMode) {
        // hearts + magic at the top (the most-glanced info, especially with the
        // top-screen HUD turned off), counters at the bottom, and the equip
        // ring centered in the space between — fills any aspect

        // hearts (live health): big, 5 per row, wrapping up to 4 rows
        int cap = Math.min(sram(0x6C) >> 3, 20);
        int cur = sram(0x6D);
        float hs = Math.min(28 * u, (w - 6 * u) / 5);   // per-heart cell
        float hy = y + 6 * u;
        float hx0 = x + (w - Math.min(cap, 5) * hs) / 2;
        for (int i = 0; i < cap; i++) {
            String k = i < (cur >> 3) ? "heart_full"
                    : (i == (cur >> 3) && (cur & 7) >= 4 ? "heart_half" : "heart_empty");
            drawGlyph(c, k, hx0 + (i % 5) * hs, hy + (i / 5) * hs, (hs - 2 * u) / 8);
        }
        int rows = Math.max((cap + 4) / 5, 1);

        // magic bar right under the hearts (with the HUD's 1/2 marker when the
        // upgrade is owned)
        float barH = 18 * u;
        boolean halfMagic = sram(0x7B) >= 1;
        float my = hy + rows * hs + 6 * u + (halfMagic ? 18 * u : 0);
        if (halfMagic) {
            // pixel-style "1/2": digit glyphs plus a stair-stepped slash,
            // white with a one-pixel black border so it reads on any backdrop
            float ts = 2 * u;
            float tx = x + (w - 22 * ts) / 2;
            float ty = my - 20 * u;
            bmp.setColorFilter(new android.graphics.PorterDuffColorFilter(
                    Color.BLACK, android.graphics.PorterDuff.Mode.SRC_IN));
            for (int k = 0; k < 9; k++) {
                if (k == 4) continue;   // the 8 outline directions
                float ox = (k % 3 - 1) * ts, oy = (k / 3 - 1) * ts;
                drawText(c, "1", tx + ox, ty + oy, ts);
                drawText(c, "2", tx + 14 * ts + ox, ty + oy, ts);
            }
            bmp.setColorFilter(null);
            fill.setColor(Color.BLACK);
            for (int k = 0; k < 5; k++)
                c.drawRect(tx + (8 + k) * ts, ty + (5 - 2 * k) * ts,
                           tx + (11 + k) * ts, ty + (9 - 2 * k) * ts, fill);
            drawText(c, "1", tx, ty, ts);
            drawText(c, "2", tx + 14 * ts, ty, ts);
            fill.setColor(Color.WHITE);
            for (int k = 0; k < 5; k++)
                c.drawRect(tx + (9 + k) * ts, ty + (6 - 2 * k) * ts,
                           tx + (10 + k) * ts, ty + (8 - 2 * k) * ts, fill);
        }
        int magic = Math.min(sram(0x6E), 128);
        dst.set(x + 16 * u, my, x + w - 16 * u, my + barH);
        fill.setColor(COL_BOX);
        c.drawRoundRect(dst, 5 * u, 5 * u, fill);
        float frac = magic / 128f;
        fill.setColor(Color.rgb(72, 208, 72));
        dst.set(x + 19 * u, my + 3 * u, x + 19 * u + (w - 38 * u) * frac, my + barH - 3 * u);
        if (frac > 0) c.drawRoundRect(dst, 3 * u, 3 * u, fill);
        stroke.setStrokeWidth(2.5f * u); stroke.setColor(COL_GOLD_DARK);
        dst.set(x + 16 * u, my, x + w - 16 * u, my + barH);
        c.drawRoundRect(dst, 5 * u, 5 * u, stroke);

        // counters chip (rupees + bombs + arrows, keys in dungeons) anchored
        // to the bottom of the column, above the tab bar
        float s = 3 * u;
        boolean showKeys = dungeonMode && sram(0x6F) != 0xFF;
        float chipH = (showKeys ? 40 : 30) * s + 20 * u;
        float cy = y + h - chipH;
        dst.set(x, cy, x + w, y + h);
        menuBox(c, dst, COL_BOX_BORDER);
        // icons centered in a 16px column on the left, numbers right-aligned
        float ry = cy + 12 * u;
        float ix = x + 10 * u;
        float ne = x + w - 10 * u;
        drawGlyph(c, "rupee", ix + 4 * s, ry, s);
        drawNumber(c, Math.min(u16(0x62), 9999), 4, ne - 32 * s, ry, s, false);
        boolean bombsMax = sram(0x43) >= new int[]{10,15,20,25,30,35,40,50}[sram(0x70) & 7];
        boolean arrowsMax = sram(0x77) >= new int[]{30,35,40,45,50,55,60,70}[sram(0x71) & 7];
        ry += 10 * s;
        drawGlyph(c, "bomb0", ix, ry, s); drawGlyph(c, "bomb1", ix + 8 * s, ry, s);
        drawNumber(c, sram(0x43), 2, ne - 16 * s, ry, s, bombsMax);
        ry += 10 * s;
        drawGlyph(c, "arrow0", ix, ry, s); drawGlyph(c, "arrow1", ix + 8 * s, ry, s);
        drawNumber(c, sram(0x77), 2, ne - 16 * s, ry, s, arrowsMax);
        if (showKeys) {
            ry += 10 * s;
            drawGlyph(c, "key", ix + 4 * s, ry, s);
            drawNumber(c, sram(0x6F), 1, ne - 8 * s, ry, s, false);
        }

        // equipped item ring, centered between the vitals and the chip: tap
        // cycles to the next owned item; with the X ITEM RING setting on, two
        // stacked rings (Y above X) that arm tap-to-assign on the items grid
        float vb = my + barH;   // bottom of the vitals cluster
        // lay the rings out against the tallest chip (dungeon, with keys) so
        // they don't jump when the keys row appears and disappears
        float cyS = y + h - 140 * u;
        float rcy = (vb + cyS) / 2;
        int slot = nativeBroken ? 0 : GameState.getEquippedSlot();
        if (xRing) {
            // two smaller rings stacked, capped so they stay inside the
            // column and clear of the counters chip and the vitals
            float ringR = Math.min(Math.min(44 * u, w / 2 - 12 * u), (cyS - vb) / 4 - 8 * u);
            float rcx = x + w / 2;
            float cyY = rcy - ringR - 6 * u, cyX = rcy + ringR + 6 * u;
            yRingR.set(rcx - ringR, cyY - ringR, rcx + ringR, cyY + ringR);
            xRingR.set(rcx - ringR, cyX - ringR, rcx + ringR, cyX + ringR);
            if (armedRing != 0) {
                // the armed ring breathes softly while it waits for a tap on
                // the items grid; drawn first so the badges stay on top
                float t = (System.nanoTime() % 2_400_000_000L) / 2_400_000_000f;
                int a = (int) (30 + 100 * (Math.sin(t * 2 * Math.PI) * 0.5 + 0.5));
                aa.setStyle(Paint.Style.STROKE);
                aa.setStrokeWidth(5 * u);
                aa.setColor(Color.argb(a, 232, 194, 96));
                c.drawCircle(rcx, armedRing == 1 ? cyY : cyX, ringR + 6 * u, aa);
            }
            drawItemRing(c, rcx, cyY, ringR, slot, "Y");
            drawItemRing(c, rcx, cyX, ringR, nativeBroken ? 0 : GameState.getEquippedSlotX(), "X");
        } else {
            float ringR = Math.min(66 * u, (cyS - vb) / 2 - 8 * u);
            float rcx = x + w / 2;
            yRingR.set(rcx - ringR, rcy - ringR, rcx + ringR, rcy + ringR);
            drawItemRing(c, rcx, rcy, ringR, slot, "Y");
        }
    }

    /** One item ring: the ring itself, the item in the given grid slot
     *  (icon sized to the ring), and a button letter at the top right. */
    private void drawItemRing(Canvas c, float cx, float cy, float r, int slot, String label) {
        drawRing(c, cx, cy, r);
        if (slot >= 1 && slot <= 20) {
            int i = slot - 1;
            int lv = "bottle".equals(ITEM_NAMES[i]) ? bottleLevel() : sram(0x40 + i);
            lv = Math.min(Math.max(lv, 0), ITEM_MAX_LEVEL[i]);
            float is = r / 13.2f;   // 5*u at the classic 66*u ring
            if (lv > 0) drawIcon(c, ITEM_NAMES[i] + "_" + lv, cx - 8 * is, cy - 8 * is, is);
        }
        // button letter in a small badge on the ring's top-right edge
        float bx = cx + r * 0.71f, by = cy - r * 0.71f;
        aa.setStyle(Paint.Style.FILL);
        aa.setColor(Color.rgb(12, 12, 12));
        c.drawCircle(bx, by, 12 * u, aa);
        aa.setStyle(Paint.Style.STROKE);
        aa.setStrokeWidth(2 * u); aa.setColor(COL_GOLD);
        c.drawCircle(bx, by, 12 * u, aa);
        drawText(c, label, bx - 8 * u, by - 8 * u, 2 * u);
    }

    private void drawRing(Canvas c, float cx, float cy, float r) {
        aa.setStyle(Paint.Style.FILL);
        aa.setColor(Color.rgb(12, 12, 12));
        c.drawCircle(cx, cy, r, aa);
        aa.setStyle(Paint.Style.STROKE);
        aa.setStrokeWidth(6 * u); aa.setColor(COL_GOLD_DARK);
        c.drawCircle(cx, cy, r, aa);
        aa.setStrokeWidth(2.5f * u); aa.setColor(COL_GOLD);
        c.drawCircle(cx, cy, r - 3 * u, aa);
    }

    // ---------- tab bar ----------

    private void drawTabBar(Canvas c, int w, int h, int tabH) {
        float y = h - tabH + 4 * u;
        float bh = tabH - 34 * u;   // keep clear of the bottom gesture inset
        float sq = bh;              // square settings button on the right
        tabSettingsR.set(w - 8 * u - sq, y, w - 8 * u, y + bh);
        float x0 = 8 * u, xr = tabSettingsR.left - 8 * u, tgap = 8 * u;
        float bw = (xr - x0 - 2 * tgap) / 3f;
        tabGearR.set(x0, y, x0 + bw, y + bh);
        tabMapR.set(x0 + bw + tgap, y, x0 + 2 * bw + tgap, y + bh);
        tabItemsR.set(x0 + 2 * (bw + tgap), y, x0 + 3 * bw + 2 * tgap, y + bh);
        drawTabButton(c, tabGearR, "GEAR", tab == TAB_GEAR);
        drawTabButton(c, tabMapR, "MAP", tab == TAB_MAP);
        drawTabButton(c, tabItemsR, "ITEMS", tab == TAB_ITEMS);
        drawTabButton(c, tabSettingsR, null, tab == TAB_SETTINGS);
        drawCog(c, tabSettingsR.centerX(), tabSettingsR.centerY(), bh * 0.28f);
    }

    private void drawCog(Canvas c, float cx, float cy, float r) {
        aa.setStyle(Paint.Style.FILL);
        aa.setColor(Color.WHITE);
        for (int i = 0; i < 8; i++) {
            double a = Math.PI / 4 * i;
            float tx = cx + (float) Math.cos(a) * r, ty = cy + (float) Math.sin(a) * r;
            c.drawCircle(tx, ty, r * 0.3f, aa);
        }
        c.drawCircle(cx, cy, r * 0.85f, aa);
        aa.setColor(COL_BOX);
        c.drawCircle(cx, cy, r * 0.38f, aa);
    }

    private void drawTabButton(Canvas c, RectF r, String label, boolean active) {
        fill.setColor(active ? Color.rgb(40, 34, 12) : COL_BOX);
        c.drawRoundRect(r, 10 * u, 10 * u, fill);
        stroke.setStrokeWidth(4 * u);
        stroke.setColor(active ? COL_GOLD : COL_BOX_BORDER2);
        c.drawRoundRect(r.left + 3 * u, r.top + 3 * u, r.right - 3 * u, r.bottom - 3 * u, 8 * u, 8 * u, stroke);
        float s = 3 * u;
        if (label != null)
            drawText(c, label, r.centerX() - textWidth(label, s) / 2, r.centerY() - 4 * s, s);
    }

    // ---------- title / cutscene screens ----------

    /**
     * Maps the game's main module to a second-screen mode.
     * 0x00-0x05: intro, file select, copy/erase/name file, load -> title card.
     * 0x12 game over, 0x14 attract story, 0x17 save&quit, 0x18-0x1A Ganon /
     * triforce room / credits -> cinema frame. Everything else is gameplay.
     */
    private static int modeForModule(int m) {
        if (m <= 0x05) return MODE_TITLE;
        if (m == 0x12 || m == 0x14 || m == 0x17 || (m >= 0x18 && m <= 0x1A)) return MODE_CINEMA;
        return MODE_GAME;
    }

    /** Quiet dark screen for the title, menus, and cutscenes: black with a
     *  thin gold frame and a small pulsing triforce. */
    private void drawCinemaScreen(Canvas c, int w, int h) {
        fill.setColor(Color.BLACK);
        c.drawRect(0, 0, w, h, fill);
        stroke.setStrokeWidth(2 * u); stroke.setColor(COL_GOLD_DARK);
        c.drawRect(12 * u, 12 * u, w - 12 * u, h - 12 * u, stroke);

        float t = (float) (System.nanoTime() / 1e9 % 3600.0);
        float pulse = (float) Math.sin(t * 1.5) * 0.5f + 0.5f;
        drawTriforce(c, w / 2f, h / 2f, Math.min(w, h) * 0.06f,
                Color.rgb(252, 214, 88), (int) (60 + 70 * pulse));
    }

    /** Three gold triangles; r = half the total height. */
    private void drawTriforce(Canvas c, float cx, float cy, float r, int color, int alpha) {
        float hb = r * 0.577f;   // half-base of one equilateral triangle
        triPath.reset();
        tri(cx, cy - r, hb, r);           // top
        tri(cx - hb, cy, hb, r);          // bottom left
        tri(cx + hb, cy, hb, r);          // bottom right
        aa.setStyle(Paint.Style.FILL);
        aa.setColor(color); aa.setAlpha(alpha);
        c.drawPath(triPath, aa);
        aa.setStyle(Paint.Style.STROKE);
        aa.setStrokeWidth(3 * u);
        aa.setColor(COL_GOLD_DARK); aa.setAlpha(alpha);
        c.drawPath(triPath, aa);
        aa.setAlpha(255);
    }

    private void tri(float apexX, float apexY, float hb, float th) {
        triPath.moveTo(apexX, apexY);
        triPath.lineTo(apexX + hb, apexY + th);
        triPath.lineTo(apexX - hb, apexY + th);
        triPath.close();
    }

    // ---------- items panel ----------

    private void drawItemsPanel(Canvas c, RectF r) {
        menuBox(c, r, COL_BOX_BORDER);
        drawText(c, "ITEMS", r.centerX() - textWidth("ITEMS", 3 * u) / 2, r.top + 18 * u, 3 * u);

        int cellW = (int) Math.min((r.width() - 70 * u) / 5, (r.height() - 100 * u) / 4);
        gridCellW = cellW;
        gridX = (int) (r.centerX() - cellW * 2.5f);
        // center the grid vertically below the title
        gridY = (int) (r.top + 40 * u + (r.height() - 40 * u - 4 * cellW) / 2);

        int equipped = nativeBroken ? 0 : GameState.getEquippedSlot();
        int equippedX = (xRing && !nativeBroken) ? GameState.getEquippedSlotX() : 0;
        // slow breathing highlight on the cell being assigned: the armed
        // ring's item, else the Y item
        int pulseSlot = (xRing && armedRing == 2) ? equippedX : equipped;
        float pt = (System.nanoTime() % 2_400_000_000L) / 2_400_000_000f;
        int pulseA = (int) (15 + 65 * (Math.sin(pt * 2 * Math.PI) * 0.5 + 0.5));
        for (int i = 0; i < 20; i++) {
            int col = i % 5, row = i / 5;
            float x = gridX + col * cellW, y = gridY + row * cellW;
            dst.set(x + 4 * u, y + 4 * u, x + cellW - 4 * u, y + cellW - 4 * u);
            if (i + 1 == equipped) {
                fill.setColor(Color.rgb(46, 40, 16));
                c.drawRoundRect(dst, 10 * u, 10 * u, fill);
                stroke.setStrokeWidth(4 * u); stroke.setColor(COL_GOLD);
                c.drawRoundRect(dst, 10 * u, 10 * u, stroke);
            }
            if (xRing && i + 1 == equippedX) {
                if (i + 1 != equipped) {
                    fill.setColor(Color.rgb(16, 30, 46));
                    c.drawRoundRect(dst, 10 * u, 10 * u, fill);
                }
                stroke.setStrokeWidth(4 * u); stroke.setColor(Color.rgb(120, 190, 255));
                dst.inset(6 * u, 6 * u);
                c.drawRoundRect(dst, 8 * u, 8 * u, stroke);
                dst.inset(-6 * u, -6 * u);
            }
            if (i + 1 == pulseSlot) {
                fill.setColor(Color.argb(pulseA, 255, 235, 160));
                c.drawRoundRect(dst, 10 * u, 10 * u, fill);
            }
            if (i == tapFlashSlot && System.nanoTime() < tapFlashUntil) {
                fill.setColor(Color.argb(90, 255, 235, 160));
                c.drawRoundRect(dst, 10 * u, 10 * u, fill);
            }
            int lv = "bottle".equals(ITEM_NAMES[i]) ? bottleLevel() : sram(0x40 + i);
            if (lv > 0) {
                lv = Math.min(lv, ITEM_MAX_LEVEL[i]);
                float is = Math.max(3 * u, Math.min(6 * u, (cellW - 24 * u) / 16f));
                drawIcon(c, ITEM_NAMES[i] + "_" + lv, x + (cellW - 16 * is) / 2, y + (cellW - 16 * is) / 2, is);
            }
            if (i + 1 == equipped) drawText(c, "Y", x + cellW - 20 * u, y + 8 * u, 1.6f * u);
            if (xRing && i + 1 == equippedX)
                drawText(c, "X", x + cellW - 20 * u, y + cellW - 22 * u, 1.6f * u);
        }
    }

    private int bottleLevel() {
        int sel = sram(0x4F);
        return sel > 0 ? Math.min(sram(0x5C + sel - 1), 7) : 0;
    }

    // ---------- gear panel ----------

    private void drawGearPanel(Canvas c, RectF r) {
        menuBox(c, r, COL_BOX_BORDER2);
        drawText(c, "GEAR", r.centerX() - textWidth("GEAR", 3 * u) / 2, r.top + 18 * u, 3 * u);

        float H = r.height();
        float x0 = r.left + 56 * u;
        float step = (r.width() - 112 * u) / 7f;
        float s = Math.min(4 * u, (step - 24 * u) / 16f);   // icon scale, capped so slots never collide
        float y0 = r.top + 0.14f * H;

        // row 1: sword shield armor gloves boots flippers pearl
        String[] gear = {
            swKey("sword_", 0x59, 4), swKey("shield_", 0x5A, 3), "armor_" + Math.min(sram(0x5B), 2),
            sram(0x54) > 0 ? "gloves_" + Math.min(sram(0x54), 2) : null,
            sram(0x55) > 0 ? "boots_1" : null,
            sram(0x56) > 0 ? "flippers_1" : null,
            sram(0x57) > 0 ? "moonpearl_1" : null,
        };
        for (int i = 0; i < gear.length; i++) {
            float x = x0 + i * step + (step - 16 * s) / 2;
            slotBg(c, x - 8 * u, y0 - 8 * u, 16 * s + 16 * u);
            if (gear[i] != null) drawIcon(c, gear[i], x, y0, s);
        }

        // row 2: bottles (left) + pendants (right); gaps spread with panel height
        float y1 = y0 + 16 * s + 0.115f * H;
        drawText(c, "BOTTLES", x0, y1 - 40 * u, 2.4f * u);
        for (int i = 0; i < 4; i++) {
            float x = x0 + i * step + (step - 16 * s) / 2;
            slotBg(c, x - 8 * u, y1 - 8 * u, 16 * s + 16 * u);
            int lv = Math.min(sram(0x5C + i), 7);
            if (lv > 0) drawIcon(c, "bottle_" + lv, x, y1, s);
        }
        float px = x0 + 4.3f * step;
        float rightW = r.right - 20 * u - px;
        drawText(c, "PENDANTS", px, y1 - 40 * u, 2.4f * u);
        int pend = sram(0x74);
        int[] pbit = {4, 2, 1};
        int[] pcol = {Color.rgb(64, 200, 88), Color.rgb(70, 110, 240), Color.rgb(230, 60, 60)};
        for (int i = 0; i < 3; i++) {
            float cxp = px + i * 66 * u + 24 * u, cyp = y1 + 30 * u;
            aa.setStyle(Paint.Style.FILL);
            aa.setColor((pend & pbit[i]) != 0 ? pcol[i] : Color.rgb(34, 34, 34));
            c.drawCircle(cxp, cyp, 22 * u, aa);
            aa.setStyle(Paint.Style.STROKE); aa.setStrokeWidth(4 * u); aa.setColor(COL_GOLD_DARK);
            c.drawCircle(cxp, cyp, 22 * u, aa);
        }
        // crystals under pendants (spacing capped to the column width)
        float cyC = y1 + 0.14f * H;
        drawText(c, "CRYSTALS", px, cyC, 2.4f * u);
        float cs = Math.min(40 * u, rightW / 7f);
        int nOwned = Integer.bitCount(sram(0x7A) & 0x7F);
        for (int i = 0; i < 7; i++) {
            float cxp = px + i * cs + 14 * u, cyp = cyC + 52 * u;
            boolean got = i < nOwned;
            aa.setStyle(Paint.Style.FILL);
            aa.setColor(got ? Color.rgb(110, 160, 255) : Color.rgb(34, 34, 34));
            dst.set(cxp - 13 * u, cyp - 18 * u, cxp + 13 * u, cyp + 18 * u);
            c.drawOval(dst, aa);
            aa.setStyle(Paint.Style.STROKE); aa.setStrokeWidth(3 * u);
            aa.setColor(got ? Color.rgb(200, 224, 255) : COL_STONE_EDGE_D);
            c.drawOval(dst, aa);
        }

        // row 3: hearts + magic (left), counters (bottom right)
        float y2 = y1 + 16 * s + 0.14f * H;
        drawText(c, "LIFE", x0, y2 - 40 * u, 2.4f * u);
        int cap = Math.min(sram(0x6C) >> 3, 20);
        int cur = sram(0x6D);
        float hs = Math.min(38 * u, (px - x0 - 24 * u) / 10f);   // keep 10 hearts inside the left column
        float hg = hs * 4f / 38f;
        for (int i = 0; i < cap; i++) {
            String k = i < (cur >> 3) ? "heart_full"
                    : (i == (cur >> 3) && (cur & 7) >= 4 ? "heart_half" : "heart_empty");
            drawGlyph(c, k, x0 + (i % 10) * hs, y2 + (i / 10) * hs, hg);
        }

        float y3 = y2 + 2 * hs + 20 * u;
        drawText(c, "MAGIC", x0, y3, 2.4f * u);
        if (sram(0x7B) >= 1)
            for (int i = 0; i < 3; i++) drawGlyph(c, "half" + i, x0 + 100 * u + i * 16 * u, y3 + 2 * u, 2 * u);
        int magic = Math.min(sram(0x6E), 128);
        float barL = x0 + 150 * u, barR = px - 24 * u;
        dst.set(barL, y3 - 2 * u, barR, y3 + 22 * u);
        fill.setColor(Color.rgb(30, 30, 30));
        c.drawRoundRect(dst, 6 * u, 6 * u, fill);
        fill.setColor(Color.rgb(72, 208, 72));
        dst.set(barL + 4 * u, y3 + 2 * u, barL + 4 * u + (barR - barL - 8 * u) * (magic / 128f), y3 + 18 * u);
        if (magic > 0) c.drawRoundRect(dst, 4 * u, 4 * u, fill);
        stroke.setStrokeWidth(3 * u); stroke.setColor(COL_GOLD_DARK);
        dst.set(barL, y3 - 2 * u, barR, y3 + 22 * u);
        c.drawRoundRect(dst, 6 * u, 6 * u, stroke);

        // counters below the crystals; glyph scale fitted so both groups span rightW
        float cg = Math.min(4 * u, rightW / 78f);
        float cx0 = px;
        float ax0 = px + 44 * cg;
        float yc = cyC + 100 * u;
        boolean bombsMax = sram(0x43) >= new int[]{10,15,20,25,30,35,40,50}[sram(0x70) & 7];
        boolean arrowsMax = sram(0x77) >= new int[]{30,35,40,45,50,55,60,70}[sram(0x71) & 7];
        drawGlyph(c, "bomb0", cx0, yc, cg); drawGlyph(c, "bomb1", cx0 + 8 * cg, yc, cg);
        drawNumber(c, sram(0x43), 2, cx0 + 18 * cg, yc, cg, bombsMax);
        drawGlyph(c, "arrow0", ax0, yc, cg); drawGlyph(c, "arrow1", ax0 + 8 * cg, yc, cg);
        drawNumber(c, sram(0x77), 2, ax0 + 18 * cg, yc, cg, arrowsMax);
        // heart pieces
        float yp = yc + 50 * u;
        drawGlyph(c, "heart_full", cx0, yp, 4 * u);
        int pieces = sram(0x6B) & 3;
        for (int i = 0; i < 4; i++) {
            float gx = cx0 + 48 * u + i * 30 * u, gy = yp + 4 * u;
            aa.setStyle(Paint.Style.FILL);
            aa.setColor(i < pieces ? Color.rgb(235, 80, 80) : Color.rgb(40, 34, 30));
            dst.set(gx, gy, gx + 20 * u, gy + 20 * u);
            c.drawRoundRect(dst, 5 * u, 5 * u, aa);
            aa.setStyle(Paint.Style.STROKE); aa.setStrokeWidth(2.5f * u); aa.setColor(COL_GOLD_DARK);
            c.drawRoundRect(dst, 5 * u, 5 * u, aa);
        }
    }

    private String swKey(String prefix, int off, int max) {
        int v = sram(off);
        if (v == 0 || v == 0xFF) return null;
        return prefix + Math.min(v, max);
    }

    private void slotBg(Canvas c, float x, float y, float size) {
        dst.set(x, y, x + size, y + size);
        fill.setColor(Color.rgb(30, 30, 30));
        c.drawRoundRect(dst, 10 * u, 10 * u, fill);
        stroke.setStrokeWidth(2.5f * u); stroke.setColor(Color.rgb(70, 70, 70));
        c.drawRoundRect(dst, 10 * u, 10 * u, stroke);
    }

    // ---------- state helpers ----------

    private int sram(int off) {
        return sram[off] & 0xFF;
    }

    private int u16(int off) {
        return (sram[off] & 0xFF) | ((sram[off + 1] & 0xFF) << 8);
    }

    private boolean slotOwned(int i) {
        return ("bottle".equals(ITEM_NAMES[i]) ? sram(0x4F) : sram(0x40 + i)) > 0;
    }

    // ================= touch =================

    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        if (uiMode != MODE_GAME) return true;   // no touch UI on title/cutscene screens
        float x = ev.getX(), y = ev.getY();
        int action = ev.getActionMasked();

        // the settings list scrolls, so its taps resolve on UP (a drag is not a tap)
        if (action == MotionEvent.ACTION_MOVE || action == MotionEvent.ACTION_UP
                || action == MotionEvent.ACTION_CANCEL) {
            if (settingsTouch) {
                if (action == MotionEvent.ACTION_MOVE) {
                    if (Math.abs(y - settingsTouchStartY) > 18 * u) settingsScrolling = true;
                    if (settingsScrolling)
                        settingsScroll = clamp(settingsScroll + (settingsTouchLastY - y), 0, settingsMaxScroll);
                    settingsTouchLastY = y;
                } else {
                    if (action == MotionEvent.ACTION_UP && !settingsScrolling
                            && tab == TAB_SETTINGS && !remapMode && !statesMode)
                        settingsTap(x, y);
                    settingsTouch = false;
                }
            }
            if (mapTouch) {
                if (action == MotionEvent.ACTION_MOVE) {
                    // a drag is neither a tap nor a long press
                    if (Math.abs(x - mapTouchX) > 18 * u || Math.abs(y - mapTouchY) > 18 * u) {
                        mapTouchMoved = true;
                        removeCallbacks(mapLongPress);
                    }
                } else {
                    removeCallbacks(mapLongPress);
                    if (action == MotionEvent.ACTION_UP && !mapTouchMoved && !mapLongFired)
                        wholeMap = !wholeMap;
                    mapTouch = false;
                }
            }
            return true;
        }
        if (action != MotionEvent.ACTION_DOWN) return true;

        if (tabItemsR.contains(x, y)) { tab = (tab == TAB_ITEMS) ? TAB_MAP : TAB_ITEMS; leaveSubPanel(); return true; }
        if (tabMapR.contains(x, y)) { tab = TAB_MAP; leaveSubPanel(); return true; }
        if (tabGearR.contains(x, y)) { tab = (tab == TAB_GEAR) ? TAB_MAP : TAB_GEAR; leaveSubPanel(); return true; }
        if (tabSettingsR.contains(x, y)) {
            tab = (tab == TAB_SETTINGS) ? TAB_MAP : TAB_SETTINGS;
            // re-scan here rather than per frame in case a pack was just copied over
            if (tab == TAB_SETTINGS) msuPack = msuPackPresent();
            leaveSubPanel();
            return true;
        }

        if (tab == TAB_SETTINGS) {
            if (statesMode) {
                if (statesBackR.contains(x, y)) { leaveSubPanel(); return true; }
                if (nativeBroken) return true;
                for (int i = 0; i < STATE_SLOTS; i++) {
                    if (stateSaveR[i].contains(x, y)) { requestState(i, true); return true; }
                    if (stateLoadR[i].contains(x, y) && stateStamp[i] != 0) {
                        requestState(i, false);
                        return true;
                    }
                }
            } else if (remapMode) {
                if (remapBackR.contains(x, y)) { leaveSubPanel(); return true; }
                for (int i = 0; i < 12; i++) {
                    if (remapRowR[i].contains(x, y) && !nativeBroken) {
                        if (remapArm == i) {
                            GameState.armButtonCapture(false);
                            remapArm = -1;
                        } else {
                            remapArm = i;
                            remapArmAt = System.nanoTime();
                            GameState.armButtonCapture(true);
                        }
                        return true;
                    }
                }
            } else if (mapAreaR.contains(x, y)) {
                // taps and drag-scrolling in the list are resolved on MOVE/UP
                settingsTouch = true;
                settingsScrolling = false;
                settingsTouchStartY = settingsTouchLastY = y;
            }
            return true;
        }

        if (yRingR.contains(x, y)) {
            if (xRing) {
                // arm (items screen only): the next grid tap assigns this button
                if (tab == TAB_ITEMS) armedRing = (armedRing == 1) ? 0 : 1;
            } else if (!nativeBroken) {
                // cycle to the next owned item
                int cur = GameState.getEquippedSlot();
                for (int k = 1; k <= 20; k++) {
                    int slot = ((cur - 1 + k) % 20) + 1;
                    if (slotOwned(slot - 1)) {
                        GameState.equipSlot(slot);
                        break;
                    }
                }
            }
            return true;
        }
        if (xRing && xRingR.contains(x, y)) {
            if (tab == TAB_ITEMS) armedRing = (armedRing == 2) ? 0 : 2;
            return true;
        }

        if (tab == TAB_ITEMS && gridCellW > 0) {
            int col = (int) ((x - gridX) / gridCellW);
            int row = (int) ((y - gridY) / gridCellW);
            if (x >= gridX && y >= gridY && col >= 0 && col <= 4 && row >= 0 && row <= 3) {
                int i = row * 5 + col;
                if (slotOwned(i) && !nativeBroken) {
                    if (xRing && armedRing == 2)
                        GameState.assignSlotX(i + 1);
                    else
                        GameState.equipSlot(i + 1);
                    armedRing = 0;
                    tapFlashSlot = i;
                    tapFlashUntil = System.nanoTime() + 250_000_000L;
                }
            }
            return true;
        }

        if (tab == TAB_MAP) {
            for (int i = 0; i < plaqueCount; i++) {
                if (plaqueR[i].contains(x, y)) {
                    int dinfo = nativeBroken ? 0 : GameState.getDungeon();
                    int floor = (byte) (dinfo >> 8);
                    if (plaqueFloor[i] == floor) {
                        viewFloorSel = NO_FLOOR_SEL;   // tap Link's floor: back to live
                    } else {
                        viewFloorSel = plaqueFloor[i];
                        viewFloorPalace = dinfo & 0xFF;
                        viewFloorFrom = floor;
                    }
                    return true;
                }
            }
            if (mapAreaR.contains(x, y)) {
                // the zoom toggle waits for the release: holding still drops or
                // removes a pin instead
                mapTouch = true;
                mapTouchMoved = mapLongFired = false;
                mapTouchX = x; mapTouchY = y;
                if (mapLive) postDelayed(mapLongPress, ViewConfiguration.getLongPressTimeout());
                return true;
            }
        }
        return true;
    }

    private void leaveSubPanel() {
        if (remapArm >= 0 && !nativeBroken) GameState.armButtonCapture(false);
        remapArm = -1;
        remapMode = false;
        statesMode = false;
        armedRing = 0;   // leaving/changing tabs cancels a pending assignment
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        postInvalidateOnAnimation();
    }
}
