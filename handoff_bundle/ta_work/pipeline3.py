"""v3 classify: robust separation of buildings / walls / foundations.

Key rule learned from the pipeline2 wall regression: a building plate ALWAYS
contains a white glyph; a pure palisade-wall chain contains none. So we never
shed 1x1 'walls' out of a glyph-bearing component (that was the source of the
phantom p2/p3/p4 walls, where find_1x1 fired on the solid interior of large
clustered house plates). Foundations are glyph-free *hollow* dotted rings,
detected by a strong morphological close that fills a large interior.

Outputs plates3.json with entries:
  building:   {p, kind:'building', k, sx_top, sy_top, glyph_bbox, comp_bbox, garea}
  wall:       {p, kind:'wall', k:1, sx_top, sy_top, corr, comp_bbox}
  foundation: {p, kind:'foundation', k, sx_top, sy_top, comp_bbox}
sx_top/sy_top are full-image pixel coords of the diamond TOP corner.
"""
import cv2, numpy as np, json, collections
import extract as E

AX, AY = E.AX, E.AY
TILE = 2 * AX
N = E.N
GOLD_COLS = [(15, 172, 210), (30, 223, 246), (34, 144, 190)]


def player_mask(img, mapmask, p):
    c = np.array(E.PLAYER_COLORS[p], int)
    d = np.abs(img.astype(int) - c).sum(axis=2)
    mask = (d < 70)
    if p == 4:  # olive p4 collides with gold piles; require strictly closer to p4
        for gc in GOLD_COLS:
            dg = np.abs(img.astype(int) - np.array(gc, int)).sum(axis=2)
            mask &= (d < dg - 5)
    return (mask & (mapmask > 0)).astype(np.uint8)


def fill_holes_pad(comp):
    c = np.pad(comp.astype(np.uint8), 2)
    return E.fill_holes(c)[2:-2, 2:-2]


def diamond_kernel(k, inset=1.5):
    w = int(round(TILE * k)); h = int(round(TILE * k / 2))
    ys, xs = np.mgrid[0:h + 1, 0:w + 1]
    cy, cx = h / 2, w / 2
    hw = TILE * k / 2 - inset; hh = hw / 2
    return ((np.abs(xs - cx) / hw + np.abs(ys - cy) / hh) <= 1.0).astype(np.uint8)


def find_1x1(mask):
    ker = diamond_kernel(1).astype(np.float32)
    corr = cv2.filter2D((mask > 0).astype(np.float32), -1, ker,
                        borderType=cv2.BORDER_CONSTANT) / ker.sum()
    pts = []; c = corr.copy()
    H2, W2 = c.shape; ys, xs = np.mgrid[0:H2, 0:W2]
    while True:
        yy, xx = np.unravel_index(np.argmax(c), c.shape)
        if c[yy, xx] < 0.70: break
        pts.append((float(xx), float(yy), float(c[yy, xx])))
        c[(np.abs(xs - xx) / TILE + np.abs(ys - yy) / (TILE / 2)) < 0.85] = 0
    return pts


def diamond_metrics(filled):
    """u/v percentile fit -> (width, kf, top_corner_local)."""
    ys2, xs2 = np.nonzero(filled)
    u = xs2 + 2.0 * ys2; v = xs2 - 2.0 * ys2
    u_min, u_max = np.percentile(u, 1), np.percentile(u, 99)
    v_min, v_max = np.percentile(v, 1), np.percentile(v, 99)
    width = (u_max + v_max) / 2 - (u_min + v_min) / 2
    kf = width / TILE
    top = ((u_min + v_max) / 2, (u_min - v_max) / 4)
    return width, kf, top


def glyph_clusters(glyph_mask):
    """Merge glyph fragments (dilate) and return cluster centroids + bboxes."""
    g = cv2.dilate(glyph_mask.astype(np.uint8), np.ones((5, 9), np.uint8))
    n, lab, stats, cent = cv2.connectedComponentsWithStats(g, 8)
    out = []
    for i in range(1, n):
        sel = (lab == i) & (glyph_mask > 0)
        a = int(sel.sum())
        if a < 8: continue
        ys, xs = np.nonzero(sel)
        out.append((float(xs.mean()), float(ys.mean()), a,
                    (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max()))))
    return out


def run_width_at(filled, gx, gy):
    row = int(round(gy))
    if not (0 <= row < filled.shape[0]): return None
    xs0 = np.nonzero(filled[row])[0]
    if not len(xs0): return None
    runs = np.split(xs0, np.nonzero(np.diff(xs0) > 1)[0] + 1)
    for r0 in runs:
        if r0[0] - 1 <= gx <= r0[-1] + 1:
            return len(r0), (r0[0] + r0[-1]) / 2
    return None


def _add_building(plates, p, k, sx_top, sy_top, garea, cbbox, glyph_bbox, from_center):
    """Append a building plate and return its (center_x, center_y, k) for exclusion.
    Top corner sits AY*k px above the plate centre at the same x."""
    rec = dict(p=p, kind='building', k=int(min(max(k, 1), 4)),
               sx_top=float(sx_top), sy_top=float(sy_top),
               garea=int(garea), comp_bbox=cbbox, glyph_bbox=glyph_bbox)
    if from_center:
        rec['from_center'] = True
    plates.append(rec)
    return (sx_top, sy_top + AY * rec['k'], rec['k'])


def classify(img, mapmask):
    plates = []
    white_full = img.astype(int).min(axis=2) > 170
    for p in E.PLAYER_COLORS:
        pm = player_mask(img, mapmask, p)
        pm_d = cv2.dilate(pm, np.ones((3, 3), np.uint8))
        n, lab, stats, cent = cv2.connectedComponentsWithStats(pm_d, 8)

        building_centers = []     # (cx, cy, k) for the global wall-exclusion pass
        foundation_boxes = []     # (x, y, w, h) to also exclude from walls

        for i in range(1, n):
            x, y, w, h, _ = stats[i]
            comp = (lab[y:y + h, x:x + w] == i) & (pm[y:y + h, x:x + w] > 0)
            raw = int(comp.sum())
            if raw < 15: continue
            filled = fill_holes_pad(comp)
            glyph = white_full[y:y + h, x:x + w] & (filled > 0)
            garea = int(glyph.sum())
            cbbox = [int(x), int(y), int(w), int(h)]

            # ---- GLYPH-FREE component: foundation candidate, else defer to walls ----
            if garea < 8:
                # Conservative hollow-ring test for dotted foundation outlines: needs
                # real size, and a strong close must fill a large interior. Solid wall
                # tiles and tiny noise specks fail this and fall through to the wall pass.
                closed = cv2.morphologyEx(comp.astype(np.uint8),
                                          cv2.MORPH_CLOSE, np.ones((9, 9), np.uint8))
                filled_c = fill_holes_pad(closed)
                width, kf, top = diamond_metrics(filled_c if filled_c.sum() else comp)
                if raw >= 120 and filled_c.sum() > 1.9 * raw and kf >= 1.8:
                    k = max(2, int(round(kf + 0.2)))
                    plates.append(dict(p=p, kind='foundation', k=int(min(k, 5)),
                                       sx_top=float(top[0] + x), sy_top=float(top[1] + y),
                                       comp_bbox=cbbox, raw=raw))
                    foundation_boxes.append((x, y, w, h))
                continue  # walls handled globally below

            # ---- GLYPH-BEARING component: one building per glyph cluster ----
            gcs = glyph_clusters(glyph)
            width, kf, top = diamond_metrics(filled)
            if len(gcs) <= 1:
                k = max(1, int(round(kf + 0.2)))
                gb = gcs[0][3] if gcs else None
                gbox = ([int(x + gb[0]), int(y + gb[1]), int(x + gb[2]), int(y + gb[3])]
                        if gb else None)
                building_centers.append(
                    _add_building(plates, p, k, top[0] + x, top[1] + y, garea, cbbox, gbox, False))
                continue
            # merged cluster: per-glyph local-width k, dedupe coincident centres
            cands = []
            for gx, gy, ga, gb in gcs:
                rw = run_width_at(filled, gx, gy)
                runw = rw[0] if rw else TILE * 2
                k2 = max(1, min(4, int(round(runw / TILE + 0.15))))
                cands.append((gx, gy, k2, ga, gb))
            cands.sort(key=lambda t: -t[3])
            kept = []
            for gx, gy, k2, ga, gb in cands:
                if any(abs(gx - kx) < TILE * 0.5 * max(kk, k2) and
                       abs(gy - ky) < TILE * 0.25 * max(kk, k2) for kx, ky, kk, _, _ in kept):
                    continue
                kept.append((gx, gy, k2, ga, gb))
            for gx, gy, k2, ga, gb in kept:
                gbox = [int(x + gb[0]), int(y + gb[1]), int(x + gb[2]), int(y + gb[3])]
                building_centers.append(
                    _add_building(plates, p, k2, gx + x, gy + y - TILE * k2 / 4,
                                  ga, cbbox, gbox, True))

        # ---- GLOBAL WALL PASS: walls = player pixels outside any building/foundation ----
        # Walls that physically touch base buildings merge into a glyph-bearing
        # component; detecting them per-component drops them. Instead, stamp every
        # building's diamond (generously) into an exclusion mask and decompose the
        # leftover player pixels into 1x1 wall tiles.
        excl = np.zeros(pm.shape, bool)
        H2, W2 = pm.shape
        ys, xs = np.mgrid[0:H2, 0:W2]
        for cx, cy, k in building_centers:
            # diamond of half-size (AX*k, AY*k); pad +0.18 tile so interiors are fully covered
            hx = AX * k + 0.18 * TILE
            hy = AY * k + 0.09 * TILE
            excl |= (np.abs(xs - cx) / hx + np.abs(ys - cy) / hy) <= 1.0
        for fx, fy, fw, fh in foundation_boxes:
            excl[max(0, fy - 3):fy + fh + 3, max(0, fx - 3):fx + fw + 3] = True
        wall_remainder = (pm > 0) & (~excl)
        # A real palisade wall is a 1x1 diamond. Numeral foundation-flags and any
        # building the glyph detector missed are 2x2+ solid plates: find_1x1 can peak
        # in a glyph-free corner of them. Reject any wall candidate that also fills a
        # 2x2 diamond in the (neighbour-complete) player mask -- a 1-wide wall chain
        # only partially fills a 2x2 kernel, a solid plate fills it almost entirely.
        pm_filled = fill_holes_pad(pm)
        ker2 = diamond_kernel(2).astype(np.float32)
        corr2 = cv2.filter2D(pm_filled.astype(np.float32), -1, ker2,
                             borderType=cv2.BORDER_CONSTANT) / ker2.sum()
        for px, py, cc in find_1x1(wall_remainder.astype(np.uint8)):
            ipy, ipx = int(round(py)), int(round(px))
            if 0 <= ipy < corr2.shape[0] and 0 <= ipx < corr2.shape[1] and corr2[ipy, ipx] > 0.62:
                continue  # sits inside a solid 2x2+ plate -> not a wall
            plates.append(dict(p=p, kind='wall', k=1, corr=float(cc),
                               sx_top=float(px), sy_top=float(py - TILE / 4)))
    return plates


if __name__ == '__main__':
    img, mapmask = E.load()
    plates = classify(img, mapmask)
    print("total plates:", len(plates))
    print(collections.Counter((pl['p'], pl['kind']) for pl in plates))
    print("k hist:", collections.Counter((pl['kind'], pl['k']) for pl in plates))
    json.dump(plates, open('plates3.json', 'w'))
