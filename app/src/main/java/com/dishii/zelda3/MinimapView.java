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
import android.view.MotionEvent;
import android.view.View;

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
 *    whole-world view) on the game's own menu-pattern background; engraved
 *    stone theme with real floor layouts in dungeons.
 *  - ITEMS / GEAR: full-width bottom buttons open menu-style black panels
 *    (like the game's own menu boxes) with big icons; tapping an item equips.
 *  - Sidebar: rupees/keys, the equipped item in a ring (tap = cycle to the
 *    next item), hearts and magic.
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

    private static final int TAB_MAP = 0, TAB_ITEMS = 1, TAB_GEAR = 2;

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
    private int viewFloorOffset = 0;
    private long viewFloorTouchedAt = 0;

    private boolean nativeBroken = false, assetsBroken = false, logged = false;
    private boolean artReady = false;
    private final byte[] probeBuf = new byte[1];

    // touch regions (recomputed during draw)
    private final RectF tabItemsR = new RectF(), tabGearR = new RectF();
    private final RectF mapAreaR = new RectF(), yRingR = new RectF();
    private final RectF[] plaqueR = new RectF[10];
    private int plaqueCount = 0;
    private final int[] plaqueFloor = new int[10];
    private int gridX, gridY, gridCellW;
    private long tapFlashUntil; private int tapFlashSlot = -1;

    private final Rect src = new Rect();
    private final RectF dst = new RectF();
    private final Rect faceSrc = new Rect(0, 0, 32, 32);
    private final Path triPath = new Path();
    private int uiMode = MODE_GAME;
    private int lastOutX, lastOutY, lastOutArea;
    private boolean hasLastOutdoor = false;

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

        if (assetsBroken) { fill.setColor(Color.BLACK); canvas.drawRect(0, 0, w, h, fill); return; }

        u = Math.min(w, h) / 720f;

        // generate the art from zelda3_assets.dat once the engine has loaded it
        if (!artReady && !nativeBroken) artReady = tryLoadNativeArt();

        // outside gameplay the minimap makes no sense: show a title card on the
        // intro/file-select screens and a quiet cinema frame during cutscenes;
        // the same screen also covers the brief window before the art is ready
        uiMode = modeForModule(module);
        if (module == 0x12 || module <= 0x05) hasLastOutdoor = false;   // death/file select: entrance unknown
        // houses and caves have no dungeon map (same test the game uses for X); keep the
        // overworld map with the marker frozen at the doorway Link came in through
        boolean inHouse = uiMode == MODE_GAME && indoors && (dungeonInfo & 0xFF) == 0xFF;
        if (inHouse && !hasLastOutdoor)
            uiMode = MODE_CINEMA;
        if (uiMode != MODE_GAME || !artReady) {
            drawCinemaScreen(canvas, w, h);
            if (isAttachedToWindow()) postInvalidateOnAnimation();
            return;
        }
        if (!indoors && (module == 0x09 || module == 0x0B)) {
            lastOutX = linkX; lastOutY = linkY; lastOutArea = area;
            hasLastOutdoor = true;
        } else if (inHouse) {
            dungeonMode = false;
            linkX = lastOutX; linkY = lastOutY; area = lastOutArea;
        }

        canvas.drawRect(0, 0, w, h, dungeonMode ? stonePaint : menuPaint);
        int tabH = (int) (96 * u);   // buttons sit above the system gesture zone
        int sideW = (int) (200 * u);
        mapAreaR.set(10 * u, 10 * u, w - sideW - 4 * u, h - tabH - 4 * u);

        if (tab == TAB_ITEMS) {
            drawItemsPanel(canvas, mapAreaR);
        } else if (tab == TAB_GEAR) {
            drawGearPanel(canvas, mapAreaR);
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

        float fx = ox + px * scale, fy = oy + py * scale;
        float bob = (float) Math.sin(System.nanoTime() / 3.0e8) * 2f * u;
        float fs = (wholeMap ? 1.2f : 1.6f) * u;
        drawSprite(c, linkFace, faceSrc, fx - 16 * fs, fy - 16 * fs + bob, fs);
        c.restore();

        // pixel-style zoom button (menu-box look)
        float bs2 = 56 * u;
        dst.set(r.left + 14 * u, r.top + 14 * u, r.left + 14 * u + bs2, r.top + 14 * u + bs2);
        fill.setColor(COL_BOX);
        c.drawRoundRect(dst, 8 * u, 8 * u, fill);
        stroke.setStrokeWidth(3 * u); stroke.setColor(COL_BOX_BORDER2);
        c.drawRoundRect(dst.left + 3 * u, dst.top + 3 * u, dst.right - 3 * u, dst.bottom - 3 * u, 6 * u, 6 * u, stroke);
        fill.setColor(Color.WHITE);
        float cxb = dst.centerX(), cyb = dst.centerY(), arm = 14 * u, th = 5 * u;
        c.drawRect(cxb - arm, cyb - th / 2, cxb + arm, cyb + th / 2, fill);
        if (!wholeMap) c.drawRect(cxb - th / 2, cyb - arm, cxb + th / 2, cyb + arm, fill);
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

        if (viewFloorTouchedAt != 0 && System.nanoTime() - viewFloorTouchedAt > 6_000_000_000L) {
            viewFloorOffset = 0;
            viewFloorTouchedAt = 0;
        }
        int li = floor + viewFloorOffset + d.basements;
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
            stroke.setColor(sel ? Color.rgb(160, 200, 255) : COL_STONE_EDGE_L);
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
                stroke.setStrokeWidth(3 * u);
                stroke.setColor(COL_GOLD);
                c.drawRect(x + 1.5f * u, y + 1.5f * u, x + cell - 1.5f * u, y + cell - 1.5f * u, stroke);
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

    // ---------- sidebar ----------

    private void drawSidebar(Canvas c, float x, float y, float w, float h, boolean dungeonMode) {
        // counters chip (rupees + keys)
        float s = 3 * u;
        boolean showKeys = dungeonMode && sram(0x6F) != 0xFF;
        float chipH = showKeys ? 20 * s + 20 * u : 10 * s + 18 * u;
        dst.set(x, y, x + w, y + chipH);
        menuBox(c, dst, COL_BOX_BORDER);
        float rx = x + (w - 8 * s * 5) / 2;
        drawGlyph(c, "rupee", rx, y + 12 * u, s);
        drawNumber(c, Math.min(u16(0x62), 9999), 4, rx + 9 * s, y + 12 * u, s, false);
        if (showKeys) {
            float kx = x + (w - 8 * s * 2) / 2;
            drawGlyph(c, "key", kx, y + 12 * u + 10 * s, s);
            drawNumber(c, sram(0x6F), 1, kx + 9 * s, y + 12 * u + 10 * s, s, false);
        }

        // anchor the magic bar and hearts to the bottom of the column, then
        // center the equip ring in the space that remains — fills any aspect
        float hs = 19 * u;
        float barH = 18 * u;
        float my = y + h - barH - 6 * u;
        float hy = my - 2 * hs - 16 * u;

        // equipped item ring: tap cycles to the next owned item
        float ringR = 66 * u;
        float rcx = x + w / 2, rcy = ((y + chipH) + hy) / 2;
        yRingR.set(rcx - ringR, rcy - ringR, rcx + ringR, rcy + ringR);
        drawRing(c, rcx, rcy, ringR);
        int slot = nativeBroken ? 0 : GameState.getEquippedSlot();
        if (slot >= 1 && slot <= 20) {
            int i = slot - 1;
            int lv = "bottle".equals(ITEM_NAMES[i]) ? bottleLevel() : sram(0x40 + i);
            lv = Math.min(Math.max(lv, 0), ITEM_MAX_LEVEL[i]);
            if (lv > 0) drawIcon(c, ITEM_NAMES[i] + "_" + lv, rcx - 40 * u, rcy - 40 * u, 5 * u);
        }
        drawText(c, "Y", rcx + ringR - 20 * u, rcy - ringR + 4 * u, 2 * u);

        // hearts (live health)
        int cap = Math.min(sram(0x6C) >> 3, 20);
        int cur = sram(0x6D);
        float hx0 = x + (w - Math.min(cap, 10) * hs) / 2;
        for (int i = 0; i < cap; i++) {
            String k = i < (cur >> 3) ? "heart_full"
                    : (i == (cur >> 3) && (cur & 7) >= 4 ? "heart_half" : "heart_empty");
            drawGlyph(c, k, hx0 + (i % 10) * hs, hy + (i / 10) * hs, 2.2f * u);
        }

        // magic bar
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
        float half = w / 2f;
        tabGearR.set(8 * u, y, half - 5 * u, y + bh);
        tabItemsR.set(half + 5 * u, y, w - 8 * u, y + bh);
        drawTabButton(c, tabGearR, "GEAR", tab == TAB_GEAR);
        drawTabButton(c, tabItemsR, "ITEMS", tab == TAB_ITEMS);
    }

    private void drawTabButton(Canvas c, RectF r, String label, boolean active) {
        fill.setColor(active ? Color.rgb(40, 34, 12) : COL_BOX);
        c.drawRoundRect(r, 10 * u, 10 * u, fill);
        stroke.setStrokeWidth(4 * u);
        stroke.setColor(active ? COL_GOLD : COL_BOX_BORDER2);
        c.drawRoundRect(r.left + 3 * u, r.top + 3 * u, r.right - 3 * u, r.bottom - 3 * u, 8 * u, 8 * u, stroke);
        float s = 3 * u;
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
            if (i == tapFlashSlot && System.nanoTime() < tapFlashUntil) {
                fill.setColor(Color.argb(90, 255, 235, 160));
                c.drawRoundRect(dst, 10 * u, 10 * u, fill);
            }
            int lv = "bottle".equals(ITEM_NAMES[i]) ? bottleLevel() : sram(0x40 + i);
            if (lv <= 0) continue;
            lv = Math.min(lv, ITEM_MAX_LEVEL[i]);
            float is = Math.max(3 * u, Math.min(6 * u, (cellW - 24 * u) / 16f));
            drawIcon(c, ITEM_NAMES[i] + "_" + lv, x + (cellW - 16 * is) / 2, y + (cellW - 16 * is) / 2, is);
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
        if (ev.getAction() != MotionEvent.ACTION_DOWN) return true;
        if (uiMode != MODE_GAME) return true;   // no touch UI on title/cutscene screens
        float x = ev.getX(), y = ev.getY();

        if (tabItemsR.contains(x, y)) { tab = (tab == TAB_ITEMS) ? TAB_MAP : TAB_ITEMS; return true; }
        if (tabGearR.contains(x, y)) { tab = (tab == TAB_GEAR) ? TAB_MAP : TAB_GEAR; return true; }

        if (yRingR.contains(x, y)) {
            // cycle to the next owned item
            if (!nativeBroken) {
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

        if (tab == TAB_ITEMS && gridCellW > 0) {
            int col = (int) ((x - gridX) / gridCellW);
            int row = (int) ((y - gridY) / gridCellW);
            if (x >= gridX && y >= gridY && col >= 0 && col <= 4 && row >= 0 && row <= 3) {
                int i = row * 5 + col;
                if (slotOwned(i) && !nativeBroken) {
                    GameState.equipSlot(i + 1);
                    tapFlashSlot = i;
                    tapFlashUntil = System.nanoTime() + 250_000_000L;
                }
            }
            return true;
        }

        if (tab == TAB_MAP) {
            for (int i = 0; i < plaqueCount; i++) {
                if (plaqueR[i].contains(x, y)) {
                    int floor = (byte) (nativeBroken ? 0 : GameState.getDungeon() >> 8);
                    viewFloorOffset = plaqueFloor[i] - floor;
                    viewFloorTouchedAt = System.nanoTime();
                    return true;
                }
            }
            if (mapAreaR.contains(x, y)) {
                wholeMap = !wholeMap;
                return true;
            }
        }
        return true;
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        postInvalidateOnAnimation();
    }
}
