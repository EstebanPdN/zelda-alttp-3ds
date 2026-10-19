#include "stereo_terrain.h"
#include <string.h>

// Tile classes from the engine's collision attributes (see tile_detect.c).
enum { WALK, SOLID, LEDGE_N, LEDGE_S, LEDGE_EW, LEDGE_DN, LEDGE_DS, DEEP, PIT, STAIR };
enum { N = kTerrainSize, NN = N * N, MAX_NB = 6 };
// Depth scale: ground at height 0 sits at kFloor eighths (the floor of the
// pre-terrain build); every tile row of rise comes kRise eighths nearer.
enum { kFloor = 16, kRise = kTerrainRise, kCap = kTerrainCap };
// Solver budget: a full solve after an area/room change, a few warm sweeps per frame.
enum { kResetSweeps = 120, kChangeSweeps = 16, kWarmSweeps = 6, kFillPasses = 6 };
// Cost control for the Old 3DS: the terrain only changes with the map, so the
// heavy step runs every kTerrainEvery frames and a full solve is split into
// kSweepsPerFrame chunks. Sprite feet update every frame.
enum { kTerrainEvery = 4, kSweepsPerFrame = 15 };
// The solve covers the visible tiles plus this margin, as a plain rectangle: the
// tilemap wraps around, and off-screen rows hold stale data while streaming.
enum { kMargin = 6 };
static const float kLedgeStep = 2.0f, kHMin = -3.0f, kHMax = 6.0f;
static const float kOmega = 1.6f, kAnchorRate = 0.12f, kHLimit = 64.0f;
static const uint8_t kObjSizes[8][2] = {
  {8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}
};

static struct {
  float h[NN];                  // solved ground height, tile rows, unanchored
  uint8_t cls[NN], prio[NN];
  uint8_t inWin[NN];            // tile is inside the solve window
  int winX0, winY0, winW, winH;
  uint16_t nb[NN][MAX_NB];
  float nbOff[NN][MAX_NB];      // estimate of h[i] is h[nb] + nbOff
  uint8_t nbCount[NN];
  uint16_t nodes[NN];
  uint16_t comp[NN];            // connected ground patch of each node
  float compSum[NN], compDeg[NN];
  unsigned nodeCount, compCount, gw, gh;
  float anchor;
  bool anchored, primed;
  unsigned frameNo, pendingSweeps;
  float hb[NN], ht[NN];         // per-tile bottom/top heights, anchored
  uint8_t known[NN];
} s;

static uint8_t Classify(unsigned a) {
  if (a == 0x01 || a == 0x02 || a == 0x03 || a == 0x26 || a == 0x43 || a == 0x27 || a == 0x46 ||
      a == 0x57 || a == 0x63 || a == 0x0b || a == 0x04 || (a >= 0x58 && a <= 0x5d) ||
      (a >= 0x6c && a <= 0x6f) || (a >= 0x50 && a <= 0x56))
    return SOLID;
  if (a == 0x28) return LEDGE_N;            // jump north
  if (a == 0x29) return LEDGE_S;            // jump south
  if (a == 0x2a || a == 0x2b) return LEDGE_EW;
  if (a == 0x2c || a == 0x2e) return LEDGE_DN;
  if (a == 0x2d || a == 0x2f) return LEDGE_DS;
  if (a == 0x08) return DEEP;
  if (a == 0x20 || (a >= 0xb0 && a <= 0xbd)) return PIT;
  if (a == 0x1d || a == 0x1e || a == 0x1f || a == 0x22 || (a >= 0x30 && a <= 0x37)) return STAIR;
  return WALK;
}

static bool IsGround(uint8_t c) { return c != SOLID && c != PIT; }
// Ground tile inside the window (the only tiles the solve and the scans use).
static bool G(unsigned i) { return s.inWin[i] && IsGround(s.cls[i]); }

static uint16_t TileWord(const Ppu *p, unsigned tx, unsigned ty) {
  const BgLayer *bg = &p->bgLayer[1];
  unsigned adr = (bg->tilemapAdr + (ty & 31) * 32 + (tx & 31) + (tx >= 32 ? 0x400 : 0) +
                  (ty >= 32 ? (bg->tilemapWider ? 0x800 : 0x400) : 0)) & 0x7fff;
  return p->vram[adr];
}

// Desired h[a] - h[b] for ground tile a directly north of ground tile b.
static float PairOffset(uint8_t a, uint8_t b) {
  float o = 0.0f;
  if (a == LEDGE_S || a == LEDGE_DS) o = kLedgeStep;
  if (b == LEDGE_N || b == LEDGE_DN) o = -kLedgeStep;
  if (a == STAIR || b == STAIR) o = 1.0f;
  return o;
}

static void AddNb(unsigned i, unsigned j, float delta) {
  if (s.nbCount[i] < MAX_NB) {
    s.nb[i][s.nbCount[i]] = (uint16_t)j;
    s.nbOff[i][s.nbCount[i]++] = delta;
  }
}

static void BuildGraph(void) {
  unsigned gw = s.gw, gh = s.gh;
  memset(s.nbCount, 0, sizeof(s.nbCount));
  s.nodeCount = 0;
  for (unsigned y = 0; y < gh; y++)
    for (unsigned x = 0; x < gw; x++) {
      unsigned i = y * N + x;
      uint8_t c = s.cls[i];
      if (!G(i)) continue;
      unsigned n = ((y + gh - 1) % gh) * N + x, so = ((y + 1) % gh) * N + x;
      unsigned e = y * N + (x + 1) % gw, w = y * N + (x + gw - 1) % gw;
      if (G(n)) AddNb(i, n, -PairOffset(s.cls[n], c));
      if (G(so)) AddNb(i, so, PairOffset(c, s.cls[so]));
      if (c != LEDGE_EW) {
        // ponytail: east/west ledge direction is unknown, so the field decides it
        if (G(e) && s.cls[e] != LEDGE_EW) AddNb(i, e, 0.0f);
        if (G(w) && s.cls[w] != LEDGE_EW) AddNb(i, w, 0.0f);
      }
      s.nodes[s.nodeCount++] = (uint16_t)i;
    }
  // Cliff faces: a 2-6 row band of plain solid tiles, at least 3 tiles wide, with
  // ground directly above and below is a step of its row count.
  for (int lx = 0; lx < s.winW; lx++)
    for (int ly = 1; ly < s.winH - 1;) {
      unsigned x = (unsigned)(s.winX0 + lx) & (gw - 1);
      unsigned y = (unsigned)(s.winY0 + ly) & (gh - 1), i = y * N + x;
      if (!(s.cls[i] == SOLID && !s.prio[i])) { ly++; continue; }
      int ly0 = ly;
      while (ly < s.winH - 1) {
        unsigned j = ((unsigned)(s.winY0 + ly) & (gh - 1)) * N + x;
        if (!(s.cls[j] == SOLID && !s.prio[j])) break;
        ly++;
      }
      unsigned run = (unsigned)(ly - ly0);
      unsigned y0 = (unsigned)(s.winY0 + ly0) & (gh - 1);
      unsigned above = ((y0 + gh - 1) & (gh - 1)) * N + x, below = ((unsigned)(s.winY0 + ly) & (gh - 1)) * N + x;
      if (run >= 2 && run <= 6 && G(above) && G(below)) {
        unsigned mid = ((y0 + run / 2) & (gh - 1)) * N;
        if (s.cls[mid + ((x + gw - 1) & (gw - 1))] == SOLID && s.cls[mid + ((x + 1) & (gw - 1))] == SOLID) {
          AddNb(above, below, (float)run);
          AddNb(below, above, -(float)run);
        }
      }
    }
  // Connected patches: a patch walled off from the rest can sit at any height
  // without breaking a rule, so each one is re-centred after solving.
  static uint16_t queue[NN];
  static uint8_t seen[NN];
  memset(seen, 0, sizeof(seen));
  s.compCount = 0;
  for (unsigned n = 0; n < s.nodeCount; n++) {
    unsigned start = s.nodes[n];
    if (seen[start]) continue;
    unsigned head = 0, tail = 0;
    queue[tail++] = (uint16_t)start; seen[start] = 1;
    while (head < tail) {
      unsigned i = queue[head++];
      s.comp[i] = (uint16_t)s.compCount;
      for (unsigned m = 0; m < s.nbCount[i]; m++) {
        unsigned j = s.nb[i][m];
        if (!seen[j]) { seen[j] = 1; queue[tail++] = (uint16_t)j; }
      }
    }
    s.compCount++;
  }
}

// Keep each patch's degree-weighted mean height at zero (the least-squares
// solution's free constant), so separate patches do not drift apart.
static void Recentre(void) {
  memset(s.compSum, 0, sizeof(float) * s.compCount);
  memset(s.compDeg, 0, sizeof(float) * s.compCount);
  for (unsigned n = 0; n < s.nodeCount; n++) {
    unsigned i = s.nodes[n];
    s.compSum[s.comp[i]] += s.h[i] * (float)s.nbCount[i];
    s.compDeg[s.comp[i]] += (float)s.nbCount[i];
  }
  for (unsigned n = 0; n < s.nodeCount; n++) {
    unsigned i = s.nodes[n], c = s.comp[i];
    if (s.compDeg[c] > 0.0f) s.h[i] -= s.compSum[c] / s.compDeg[c];
  }
}

static void Sweep(unsigned count) {
  for (unsigned k = 0; k < count; k++)
    for (unsigned n = 0; n < s.nodeCount; n++) {
      unsigned i = s.nodes[n], c = s.nbCount[i];
      if (!c) continue;
      float sum = 0.0f;
      for (unsigned m = 0; m < c; m++) sum += s.h[s.nb[i][m]] + s.nbOff[i][m];
      float v = s.h[i] + kOmega * (sum / (float)c - s.h[i]);
      s.h[i] = v > kHLimit ? kHLimit : v < -kHLimit ? -kHLimit : v;
    }
}

static float Anchored(unsigned i) {
  float v = s.h[i] - s.anchor;
  return v < kHMin ? kHMin : v > kHMax ? kHMax : v;
}

static uint8_t Px(float hh) {
  float v = (float)kFloor - (float)kRise * hh;
  int r = (int)(v + 0.5f);
  return (uint8_t)(r < 1 ? 1 : r > 30 ? 30 : r);
}

void StereoTerrain_Reset(void) { s.primed = false; s.pendingSweeps = 0; }

static void TerrainStep(PicaTerrain *t, const Ppu *p, const StereoTerrainSource *src) {
  const BgLayer *bg = &p->bgLayer[1];
  unsigned gw = bg->tilemapWider ? 64 : 32, gh = bg->tilemapHigher ? 64 : 32;
  bool reshaped = !s.primed || gw != s.gw || gh != s.gh;
  s.gw = gw; s.gh = gh;

  // 1. Solve window (visible tiles plus a margin), tile classes and BG priority.
  int side = p->extraLeftRight;
  int winX0 = (((int)bg->hScroll - side) >> 3) - kMargin, winY0 = (((int)bg->vScroll + 1) >> 3) - kMargin;
  int winW = ((256 + 2 * side) >> 3) + 1 + 2 * kMargin, winH = (240 >> 3) + 1 + 2 * kMargin;
  if (winW > (int)gw - 1) winW = (int)gw - 1;
  if (winH > (int)gh - 1) winH = (int)gh - 1;
  bool moved = winX0 != s.winX0 || winY0 != s.winY0 || winW != s.winW || winH != s.winH;
  if (moved || reshaped) {
    s.winX0 = winX0; s.winY0 = winY0; s.winW = winW; s.winH = winH;
    memset(s.inWin, 0, sizeof(s.inWin));
    for (int ly = 0; ly < winH; ly++)
      for (int lx = 0; lx < winW; lx++)
        s.inWin[((unsigned)(winY0 + ly) & (gh - 1)) * N + ((unsigned)(winX0 + lx) & (gw - 1))] = 1;
  }
  unsigned changed = 0;
  for (unsigned ty = 0; ty < gh; ty++)
    for (unsigned tx = 0; tx < gw; tx++) {
      unsigned i = ty * N + tx;
      if (!s.inWin[i]) continue;
      uint16_t w = TileWord(p, tx, ty);
      unsigned a = src->dungeonAttr ? src->dungeonAttr[i] : src->overworldAttr[w & 0x1ff];
      uint8_t c = Classify(a), pr = (w & 0x2000) != 0;
      if (c != s.cls[i] || pr != s.prio[i]) { changed++; s.cls[i] = c; s.prio[i] = pr; }
    }

  // 2. Height field, warm-started from the previous step. A new area or room
  // restarts it; that solve is spread over the next few frames.
  unsigned sweeps = kWarmSweeps;
  if (reshaped || changed > (unsigned)(winW * winH) / 4) {
    memset(s.h, 0, sizeof(s.h));
    s.anchored = false;
    s.pendingSweeps = kResetSweeps;
  } else if (changed && s.pendingSweeps < kChangeSweeps) {
    s.pendingSweeps = kChangeSweeps;
  }
  if (s.pendingSweeps) {
    sweeps = s.pendingSweeps < kSweepsPerFrame ? s.pendingSweeps : kSweepsPerFrame;
    s.pendingSweeps -= sweeps;
  }
  if (changed || reshaped || moved) BuildGraph();
  s.primed = true;
  Sweep(sweeps);
  Recentre();

  // 3. Anchor: the visible ground's mean height sits at the floor depth. Eased so
  // walking onto new terrain does not jolt the whole scene.
  unsigned mx = gw * 8 - 1, my = gh * 8 - 1, width = 256 + 2 * side;
  float sum = 0.0f; unsigned cnt = 0;
  for (unsigned sy = 0; sy < 224; sy += 8)
    for (unsigned sx = 0; sx < width; sx += 8) {
      unsigned wx = (unsigned)((int)sx - side + (int)bg->hScroll) & mx;
      unsigned wy = (sy + 1 + bg->vScroll) & my;
      unsigned i = (wy >> 3) * N + (wx >> 3);
      if (G(i)) { sum += s.h[i]; cnt++; }
    }
  if (cnt) {
    float mean = sum / (float)cnt;
    if (!s.anchored) { s.anchor = mean; s.anchored = true; }
    else s.anchor += kAnchorRate * (mean - s.anchor);
  }

  // 4. Per-tile bottom/top heights. Solid tiles stand on the nearest ground below
  // them, inside one object: a non-priority tile above a priority run belongs to
  // something behind it. Cliff faces blend up to the ground above them.
  memset(s.known, 0, sizeof(s.known));
  for (unsigned ty = 0; ty < gh; ty++)
    for (unsigned tx = 0; tx < gw; tx++) {
      unsigned i = ty * N + tx;
      if (!s.inWin[i]) continue;
      if (G(i)) { s.hb[i] = s.ht[i] = Anchored(i); s.known[i] = 1; continue; }
      if (s.cls[i] == PIT) continue;
      int k = 0; unsigned yy = ty; bool seen = s.prio[i], found = false; float base = 0.0f;
      while (k < kCap + 4) {
        yy = (yy + 1) % gh; k++;
        unsigned j = yy * N + tx;
        if (!s.inWin[j]) break;
        if (G(j)) { base = Anchored(j); found = true; break; }
        if (s.prio[j] && !seen) break;
        seen = seen || s.prio[j];
      }
      if (!found) continue;
      bool haveTop = false; float topGround = 0.0f; int m = 0;
      if (!s.prio[i]) {
        yy = ty;
        while (m < kCap) {
          yy = (yy + gh - 1) % gh; m++;
          unsigned j = yy * N + tx;
          if (!s.inWin[j]) break;
          if (G(j)) { topGround = Anchored(j); haveTop = true; break; }
          if (s.cls[j] != SOLID || s.prio[j]) break;
        }
      }
      int rows = k + (haveTop ? m - 1 : 0);
      float top = base + (float)(rows < kCap ? rows : kCap);
      if (haveTop) {
        float minTop = base + (float)(rows < 1 ? rows : 1);
        top = topGround > minTop ? topGround : minTop;
      }
      float denom = (float)(rows > 1 ? rows : 1);
      s.hb[i] = base + (top - base) * (float)(k - 1) / denom;
      s.ht[i] = base + (top - base) * (float)(k < rows ? k : rows) / denom;
      s.known[i] = 1;
    }
  // Unknown tiles (walls behind objects, pits) copy a known neighbour in the row.
  static const int kFill[4] = {-1, 1, -2, 2};
  for (unsigned pass = 0; pass < kFillPasses; pass++) {
    bool any = false;
    for (unsigned ty = 0; ty < gh; ty++)
      for (unsigned tx = 0; tx < gw; tx++) {
        unsigned i = ty * N + tx;
        if (s.known[i] || !s.inWin[i]) continue;
        for (unsigned d = 0; d < 4; d++) {
          unsigned j = ty * N + (unsigned)((int)tx + (int)gw + kFill[d]) % gw;
          if (s.known[j] == 1) { s.hb[i] = s.hb[j]; s.ht[i] = s.ht[j]; s.known[i] = 2; any = true; break; }
        }
      }
    if (!any) break;
    for (unsigned i = 0; i < NN; i++) if (s.known[i] == 2) s.known[i] = 1;
  }
  for (unsigned i = 0; i < NN; i++) {
    if (!s.inWin[i]) continue;
    t->top[i] = Px(s.known[i] ? s.ht[i] : 0.0f);
    t->bottom[i] = Px(s.known[i] ? s.hb[i] : 0.0f);
  }
  t->changed = (uint16_t)changed;
  t->sweeps = (uint16_t)sweeps;
}

// Sprites: touching OAM rects form one object; it stands on the ground under
// its lowest edge. Runs every frame because sprites move every frame.
static void SpriteStep(PicaTerrain *t, const Ppu *p) {
  const BgLayer *bg = &p->bgLayer[1];
  int side = p->extraLeftRight;
  unsigned mx = s.gw * 8 - 1, my = s.gh * 8 - 1;

  int x0[128], y0[128], x1[128], y1[128], par[128];
  bool vis[128];
  for (unsigned e = 0; e < 128; e++) {
    unsigned index = e * 2;
    uint16_t lo = p->oam[index];
    t->objFeet[e] = kTerrainNoFeet; vis[e] = false; par[e] = (int)e;
    if ((lo >> 8) == 0xf0) continue;
    unsigned high = p->oam[0x100 + (index >> 4)] >> (index & 15);
    int size = kObjSizes[p->objSize & 7][(high >> 1) & 1];
    int x = (lo & 255) + (high & 1) * 256;
    x -= (x >= 256 + side) * 512; x += p->renderObjXOffset;
    int y = (int)(lo >> 8) + p->renderObjYOffset;
    if (y >= 240) y -= 256;
    x0[e] = x + side; y0[e] = y; x1[e] = x + side + size; y1[e] = y + size; vis[e] = true;
  }
  for (unsigned a = 0; a < 128; a++) {
    if (!vis[a]) continue;
    for (unsigned b = 0; b < a; b++) {
      if (!vis[b]) continue;
      if (x0[a] <= x1[b] && x0[b] <= x1[a] && y0[a] <= y1[b] && y0[b] <= y1[a]) {
        int ra = (int)a, rb = (int)b;
        while (par[ra] != ra) ra = par[ra];
        while (par[rb] != rb) rb = par[rb];
        if (ra != rb) par[ra] = rb;
      }
    }
  }
  int feet[128];
  for (unsigned e = 0; e < 128; e++) feet[e] = -10000;
  for (unsigned e = 0; e < 128; e++) {
    if (!vis[e]) continue;
    int r = (int)e; while (par[r] != r) r = par[r];
    if (y1[e] > feet[r]) feet[r] = y1[e];
  }
  for (unsigned e = 0; e < 128; e++) {
    if (!vis[e]) continue;
    int r = (int)e; while (par[r] != r) r = par[r];
    int f = feet[r], sx = (x0[e] + x1[e]) / 2, sy = f - 2;
    if (sy < 0) sy = 0;
    if (sy > 239) sy = 239;
    unsigned wx = (unsigned)(sx - side + (int)bg->hScroll) & mx;
    unsigned wy = (unsigned)(sy + 1 + (int)bg->vScroll) & my;
    unsigned i = (wy >> 3) * N + (wx >> 3);
    t->objFeet[e] = (int16_t)f;
    t->objGround[e] = Px(G(i) ? Anchored(i) : s.known[i] ? s.hb[i] : 0.0f);
  }
}

void StereoTerrain_Update(PicaTerrain *t, const Ppu *p, const StereoTerrainSource *src) {
  if (!src || (!src->overworldAttr && !src->dungeonAttr)) { t->valid = false; return; }
  s.frameNo++;
  if (!s.primed || s.pendingSweeps || s.frameNo % kTerrainEvery == 0) {
    memset(t->top, Px(0.0f), sizeof(t->top));
    memset(t->bottom, Px(0.0f), sizeof(t->bottom));
    TerrainStep(t, p, src);
  } else {
    t->sweeps = 0;
  }
  SpriteStep(t, p);
  t->valid = true;
}
