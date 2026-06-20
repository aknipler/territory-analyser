"""Gaia extraction (trees, gold, stone, cliffs) + water grid.

Elevation handling: the x-axis S=(r+c) is elevation-free; only d=(r-c) is shifted
by terrain height. The 355 solved buildings give a sparse elevation field
e(r,c)=base-d (d-units). We interpolate it across the 168x168 grid and
FORWARD-SAMPLE: for each tile, compute its screen centre using the lattice +
interpolated elevation and read the colour masks there.

Tile centre screen position:
  sx = 26   + AX*(r + c + 1)
  sy = BY0  + AY*(r - c + e(r,c))
(26 = map left-corner sx; BY0 = 697.5 vertical reference; both from the calibrated
lattice in HANDOFF.md.)
"""
import cv2, numpy as np, json
import extract as E

AX, AY = E.AX, E.AY
TILE = 2 * AX
N = E.N
BY0 = 697.5
LEFTX = 26.0


def elevation_field():
    """Interpolate e(r,c) over the full grid from solved building positions."""
    recs = json.load(open('unified_solution.json'))
    pts = []; vals = []
    for r in recs:
        if r.get('offmap'):
            continue
        cx = r['x0'] + r['k'] / 2.0; cy = r['y0'] + r['k'] / 2.0
        e = r['base'] - r['d']
        pts.append((cx, cy)); vals.append(e)
    pts = np.array(pts); vals = np.array(vals)
    from scipy.interpolate import griddata
    gr, gc = np.mgrid[0:N, 0:N]
    lin = griddata(pts, vals, (gr, gc), method='linear')
    nn = griddata(pts, vals, (gr, gc), method='nearest')
    lin[np.isnan(lin)] = nn[np.isnan(lin)]
    return lin  # shape (N,N), indexed [r,c]


def tile_screen(r, c, e):
    sx = LEFTX + AX * (r + c + 1)
    sy = BY0 + AY * (r - c + e)
    return sx, sy


# dense sample offsets covering a tile diamond (|fx|+|fy| <= 0.85)
_DIAMOND_OFFS = [(fx, fy) for fx in np.linspace(-0.7, 0.7, 7)
                 for fy in np.linspace(-0.7, 0.7, 7) if abs(fx) + abs(fy) <= 0.85]


def forward_sample(mask, efield, cover=0.5):
    """Return tiles whose diamond footprint covers `mask` at >= `cover` fraction."""
    H, W = mask.shape
    out = np.zeros((N, N), bool)
    for r in range(N):
        for c in range(N):
            sx, sy = tile_screen(r, c, efield[r, c])
            hit = 0; tot = 0
            for fx, fy in _DIAMOND_OFFS:
                xx = int(round(sx + fx * AX)); yy = int(round(sy + fy * AY))
                if 0 <= yy < H and 0 <= xx < W:
                    tot += 1
                    if mask[yy, xx]:
                        hit += 1
            if tot and hit / tot >= cover:
                out[r, c] = True
    return out


def keep_large_components(mask, min_area):
    """Drop scattered noise: keep only connected components >= min_area px."""
    n, lab, st, _ = cv2.connectedComponentsWithStats(mask.astype(np.uint8), 8)
    out = np.zeros_like(mask, bool)
    for i in range(1, n):
        if st[i, 4] >= min_area:
            out[lab == i] = True
    return out


def overlay(img, tiles, efield, color, base=None):
    ov = img.copy() if base is None else base
    for r in range(N):
        for c in range(N):
            if not tiles[r, c]:
                continue
            e = efield[r, c]
            sx, sy = tile_screen(r, c, e)
            cv2.circle(ov, (int(round(sx)), int(round(sy))), 2, color, -1)
    return ov


def player_plate_mask(img, mapmask):
    """Union of all four players' plates (dilated) to exclude from gaia masks."""
    import pipeline3 as P3
    acc = np.zeros(mapmask.shape, np.uint8)
    for p in E.PLAYER_COLORS:
        acc |= P3.player_mask(img, mapmask, p)
    return cv2.dilate(acc, np.ones((5, 5), np.uint8)) > 0


def gaia_masks(img, mapmask):
    b, g, r = img[:, :, 0].astype(int), img[:, :, 1].astype(int), img[:, :, 2].astype(int)
    mx = np.maximum(np.maximum(b, g), r); mn = np.minimum(np.minimum(b, g), r)
    sat = mx - mn
    plates = player_plate_mask(img, mapmask)
    M = (mapmask > 0) & (~plates)              # gaia lives off the player plates
    masks = {}
    # trees: forest hexes are the SAME colour as grass but textured -> dense dark
    # canopy-shadow pixels. 'darkpx' is the per-pixel signal; density handled per-tile.
    masks['darkpx'] = (mx < 85) & M
    # gold: bright SATURATED yellow cubes (much brighter/purer than tan grass)
    masks['gold'] = (r > 195) & (g > 165) & (b < 70) & (sat > 120) & M
    masks['water'] = (b > 140) & (b - r > 55) & (b - g > 30) & M           # bright blue
    # cliffs/stone are NEUTRAL grey (r ~= b); brown forest/dirt has r >> b
    neutral = (sat < 40) & (np.abs(r - b) < 28) & M
    masks['stone'] = neutral & (mx >= 145) & (mx < 212)                    # light grey rounded
    masks['cliff'] = neutral & (mx >= 80) & (mx < 145)                     # darker grey rocky
    return masks


def fill_diamonds(img, tiles, ef, color, alpha=0.5):
    layer = img.copy()
    for r in range(N):
        for c in range(N):
            if not tiles[r, c]:
                continue
            sx, sy = tile_screen(r, c, ef[r, c])
            pts = np.array([[sx, sy - AY], [sx + AX, sy], [sx, sy + AY], [sx - AX, sy]], np.int32)
            cv2.fillConvexPoly(layer, pts, color)
    return cv2.addWeighted(layer, alpha, img, 1 - alpha, 0)


if __name__ == '__main__':
    img, mapmask = E.load()
    ef = elevation_field()
    print("elevation field: min %.2f max %.2f mean %.2f" % (ef.min(), ef.max(), ef.mean()))
    masks = gaia_masks(img, mapmask)
    palette = {'tree': (255, 0, 255), 'gold': (0, 255, 255), 'water': (255, 130, 0),
               'stone': (220, 220, 220), 'cliff': (0, 0, 255)}
    for k, m in masks.items():
        print("  mask %-7s px=%d" % (k, int(m.sum())))
    # localized resources: drop scattered colour noise via connected components
    gold_m = keep_large_components(masks['gold'], 18)
    stone_m = keep_large_components(masks['stone'], 30)
    cliff_m = keep_large_components(masks['cliff'], 50)
    water_m = keep_large_components(masks['water'], 60)
    tiles = {}
    # resolve overlaps by priority (water/gold/stone/cliff are specific; trees broad)
    claimed = np.zeros((N, N), bool)
    for k, m, cov in [('water', water_m, 0.5), ('gold', gold_m, 0.3),
                      ('stone', stone_m, 0.3), ('cliff', cliff_m, 0.18),
                      ('tree', masks['darkpx'], 0.22)]:
        t = forward_sample(m, ef, cover=cov)
        t &= ~claimed
        claimed |= t
        tiles[k] = t
        print("  tiles %-6s = %d" % (k, int(t.sum())))
    # per-type overlays + combined
    for k in tiles:
        ov = fill_diamonds(img, tiles[k], ef, palette[k])
        cv2.imwrite('gaia_%s.png' % k, cv2.resize(ov, None, fx=0.55, fy=0.55))
    comb = img.copy()
    for k in ['water', 'gold', 'stone', 'cliff', 'tree']:
        comb = fill_diamonds(comb, tiles[k], ef, palette[k])
    cv2.imwrite('gaia_combined.png', cv2.resize(comb, None, fx=0.6, fy=0.6))
    np.savez('gaia_tiles.npz', **{k: tiles[k] for k in tiles})
    # water grid output (168x168 0/1)
    wgrid = tiles['water'].astype(np.uint8)
    np.savetxt('water_grid.txt', wgrid, fmt='%d', delimiter='')
    print("wrote gaia_*.png, gaia_combined.png, gaia_tiles.npz, water_grid.txt")

    # --- sanity: forward-sample building footprints, expect them on player colour ---
    recs = json.load(open('unified_solution.json'))
    ov = img.copy()
    hit = 0; tot = 0
    for r in recs:
        if r.get('offmap') or r['kind'] != 'building':
            continue
        cx = r['x0'] + r['k'] / 2.0 - 0.5; cy = r['y0'] + r['k'] / 2.0 - 0.5
        e = ef[int(r['x0']), int(r['y0'])]
        sx, sy = tile_screen(cx, cy, e)
        cv2.circle(ov, (int(round(sx)), int(round(sy))), 3, (0, 255, 0), -1)
        tot += 1
    cv2.imwrite('elev_check.png', cv2.resize(ov, None, fx=0.42, fy=0.42))
    print("drew", tot, "building centres -> elev_check.png")
