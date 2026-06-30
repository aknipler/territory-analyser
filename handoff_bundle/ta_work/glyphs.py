"""Extract white glyph crops from solved building plates, cluster them by shape,
and emit a contact sheet for the user to label (glyph -> AoE2 building name).

Glyphs are white (min channel > 170) inside the player-coloured plate. We crop a
fixed window centred on each plate, take the white mask, tight-crop + resize to a
normalised binary descriptor, and agglomeratively cluster on (1 - IoU) distance
(IoU maximised over small +/-2px shifts to absorb centring jitter). Footprint k is
carried through so the user sees the size class alongside each glyph.
"""
import cv2, numpy as np, json, collections
import extract as E
import pipeline3 as P3

TILE = P3.TILE
DESC = 24  # normalised glyph descriptor side


def plate_box(pl):
    """Diamond centre and true bounding box of a k-plate (half-width AX*k, half-height AY*k)."""
    cx = pl['sx_top']; cy = pl['sy_top'] + P3.AY * pl['k']
    hx = P3.AX * pl['k']; hy = P3.AY * pl['k']
    return cx, cy, hx, hy


def disp_crop(img, pl, pad=3):
    """Colour crop tightly bounding the plate diamond, centred on the glyph centroid
    when known (keeps the glyph centred even if the plate fit is slightly off)."""
    cx, cy, hx, hy = plate_box(pl)
    if pl.get('gc'):
        cx, cy = pl['gc']
    H, W = img.shape[:2]
    x0 = max(0, int(cx - hx - pad)); x1 = min(W, int(cx + hx + pad))
    y0 = max(0, int(cy - hy - pad)); y1 = min(H, int(cy + hy + pad))
    return img[y0:y1, x0:x1]


def glyph_binary(img, white, pl):
    """Return (DESCxDESC binary glyph, (glyph_cx, glyph_cy)) for a building plate.

    Extracts ALL white pixels inside the (slightly inset) plate diamond -- the
    complete glyph, not a single bbox fragment -- so multi-stroke glyphs (Market,
    University, Castle) are captured whole and cluster correctly.
    """
    cx, cy, hx, hy = plate_box(pl)
    H, W = white.shape
    x0 = max(0, int(cx - hx - 2)); x1 = min(W, int(cx + hx + 2))
    y0 = max(0, int(cy - hy - 2)); y1 = min(H, int(cy + hy + 2))
    if x1 <= x0 or y1 <= y0:
        return None
    sub = white[y0:y1, x0:x1].astype(np.uint8)
    ys, xs = np.mgrid[y0:y1, x0:x1]
    inside = (np.abs(xs - cx) / (hx * 0.82) + np.abs(ys - cy) / (hy * 0.82)) <= 1.0
    g = (sub & inside).astype(np.uint8)
    # drop speckle / plate-rim white: keep components with area >= 3
    nL, lab, st, _ = cv2.connectedComponentsWithStats(g, 8)
    g = np.zeros_like(g)
    for i in range(1, nL):
        if st[i, 4] >= 3:
            g[lab == i] = 1
    ys2, xs2 = np.nonzero(g)
    if len(xs2) < 6:
        return None
    gcx = float(xs2.mean() + x0); gcy = float(ys2.mean() + y0)
    g = g[ys2.min():ys2.max() + 1, xs2.min():xs2.max() + 1]
    desc = cv2.resize(g * 255, (DESC, DESC), interpolation=cv2.INTER_AREA) > 90
    return desc, (gcx, gcy)


def _blur(a):
    f = cv2.GaussianBlur(a.astype(np.float32), (0, 0), 1.3)
    f -= f.mean()
    n = np.linalg.norm(f)
    return f / n if n else f


def iou_dist(a, b):
    """1 - best normalised cross-correlation over small shifts (blurred, zero-mean).
    More forgiving of anti-aliasing/stroke jitter than binary IoU, so instances of
    the same glyph merge while distinct glyphs stay apart."""
    fa = _blur(a)
    best = -1.0
    for dy in (-2, -1, 0, 1, 2):
        for dx in (-2, -1, 0, 1, 2):
            fb = _blur(np.roll(np.roll(b, dy, 0), dx, 1))
            best = max(best, float((fa * fb).sum()))
    return max(0.0, 1.0 - best)


def cluster(descs, thresh=0.60):
    """Simple agglomerative-ish greedy clustering on IoU distance."""
    n = len(descs)
    D = np.zeros((n, n))
    for i in range(n):
        for j in range(i + 1, n):
            D[i, j] = D[j, i] = iou_dist(descs[i], descs[j])
    from scipy.cluster.hierarchy import linkage, fcluster
    from scipy.spatial.distance import squareform
    Z = linkage(squareform(D, checks=False), method='average')
    labels = fcluster(Z, t=thresh, criterion='distance')
    return labels


def main():
    img, mapmask = E.load()
    white = (img.astype(int).min(axis=2) > 170)
    plates = json.load(open('plates3.json'))
    for i, pl in enumerate(plates):
        pl['_idx'] = i           # tag plates3 index for joining to unified_solution
    builds = [pl for pl in plates if pl['kind'] == 'building']
    descs = []; keep = []
    for pl in builds:
        res = glyph_binary(img, white, pl)
        if res is None:
            continue
        desc, gc = res
        pl['gc'] = gc            # glyph centroid (full-image px) for centred display
        descs.append(desc); keep.append(pl)
    print("buildings:", len(builds), "with usable glyph:", len(keep))
    # Cluster WITHIN each footprint size: a glyph's building type fixes its k, so
    # glyphs of different k are different buildings. This uses the footprint prior
    # and stops a House (k=2) glyph merging with a University (k=4) glyph.
    groups = {}
    next_lab = 0
    by_k = collections.defaultdict(list)
    for pl, g in zip(keep, descs):
        by_k[pl['k']].append((pl, g))
    for k in sorted(by_k):
        items = by_k[k]
        if len(items) == 1:
            groups[next_lab] = items; next_lab += 1; continue
        sub = cluster([g for _, g in items], thresh=0.50)
        for lab in sorted(set(sub)):
            groups[next_lab] = [items[i] for i in range(len(items)) if sub[i] == lab]
            next_lab += 1
    order = sorted(groups, key=lambda l: -len(groups[l]))
    print("clusters:", len(order))
    summary = []
    for rank, lab in enumerate(order):
        members = groups[lab]
        ks = collections.Counter(m[0]['k'] for m in members)
        ps = collections.Counter(m[0]['p'] for m in members)
        summary.append(dict(cluster=rank, count=len(members),
                            k=dict(ks), players=dict(ps)))
        print(f"  C{rank}: n={len(members):3d} k={dict(ks)} players={dict(ps)}")
    json.dump(summary, open('glyph_clusters.json', 'w'), indent=1)
    # per-building cluster assignment (plates3 index -> cluster rank) for final assembly
    idx2cluster = {}
    for rank, lab in enumerate(order):
        for pl, _ in groups[lab]:
            idx2cluster[pl['_idx']] = rank
    json.dump(idx2cluster, open('building_clusters.json', 'w'))

    # ---- contact sheet: one readable row per cluster ----
    # prototype = medoid (member minimising total IoU distance to the rest)
    GG = 84; cell = 56; pad = 6; strip_w = 190
    rows = []
    for rank, lab in enumerate(order):
        members = groups[lab]
        gs = [m[1] for m in members]
        if len(gs) == 1:
            medoid = gs[0]
        else:
            tot = [sum(iou_dist(gs[i], gs[j]) for j in range(len(gs)) if j != i)
                   for i in range(len(gs))]
            medoid = gs[int(np.argmin(tot))]
        # big black-on-white prototype
        proto = (255 - medoid.astype(np.uint8) * 255)
        proto = cv2.cvtColor(cv2.resize(proto, (GG, GG), interpolation=cv2.INTER_NEAREST),
                             cv2.COLOR_GRAY2BGR)
        cv2.rectangle(proto, (0, 0), (GG - 1, GG - 1), (0, 200, 0), 1)
        tiles = [proto]
        for pl, g in members[:6]:
            crop = disp_crop(img, pl)
            t = cv2.resize(crop, (GG, GG), interpolation=cv2.INTER_NEAREST) if crop.size else np.zeros((GG, GG, 3), np.uint8)
            tiles.append(t)
        while len(tiles) < 7:
            tiles.append(np.zeros((GG, GG, 3), np.uint8))
        rowimg = np.hstack([np.pad(t, ((0, 0), (0, pad), (0, 0))) for t in tiles])
        ks = collections.Counter(m[0]['k'] for m in members)
        kmode = ks.most_common(1)[0][0]
        ps = collections.Counter(m[0]['p'] for m in members)
        strip = np.zeros((GG, strip_w, 3), np.uint8)
        cv2.putText(strip, f"C{rank}  n={len(members)}", (4, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
        cv2.putText(strip, f"size {kmode}x{kmode}", (4, 46), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1)
        cv2.putText(strip, "p:" + ",".join(f"{k}:{v}" for k, v in sorted(ps.items())),
                    (4, 70), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (180, 180, 180), 1)
        rows.append(np.hstack([strip, rowimg]))
    sheet = np.vstack([np.pad(r, ((0, pad), (0, 0), (0, 0))) for r in rows])
    hdr = np.zeros((34, sheet.shape[1], 3), np.uint8)
    cv2.putText(hdr, "PROTOTYPE (green box) = the glyph to name | next cols = example plates | left = id / footprint / players",
                (4, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.42, (210, 210, 210), 1)
    sheet = np.vstack([hdr, sheet])
    cv2.imwrite('contact_sheet.png', sheet)

    # ---- compact grid: a high-zoom COLOUR crop of a representative plate per
    # cluster (the real glyph the user must name), labelled with id/size/count ----
    G = 120; cols = 6; barh = 26
    cells = []
    for rank, lab in enumerate(order):
        members = groups[lab]
        gs = [m[1] for m in members]
        if len(gs) == 1:
            mi = 0
        else:
            tot = [sum(iou_dist(gs[i], gs[j]) for j in range(len(gs)) if j != i)
                   for i in range(len(gs))]
            mi = int(np.argmin(tot))
        pl = members[mi][0]
        crop = disp_crop(img, pl)
        body = cv2.resize(crop, (G, G - barh), interpolation=cv2.INTER_NEAREST) if crop.size \
            else np.zeros((G - barh, G, 3), np.uint8)
        kmode = collections.Counter(m[0]['k'] for m in members).most_common(1)[0][0]
        bar = np.full((barh, G, 3), 30, np.uint8)
        cv2.putText(bar, f"C{rank} {kmode}x{kmode} n{len(members)}", (3, 18),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.42, (255, 255, 255), 1)
        cell = np.vstack([bar, body])
        cv2.rectangle(cell, (0, 0), (G - 1, G - 1), (0, 160, 0), 1)
        cells.append(cell)
    while len(cells) % cols:
        cells.append(np.zeros((G, G, 3), np.uint8))
    grid = np.vstack([np.hstack(cells[i:i + cols]) for i in range(0, len(cells), cols)])
    cv2.imwrite('glyph_grid.png', grid)
    print("wrote contact_sheet.png, glyph_grid.png, glyph_clusters.json")


if __name__ == '__main__':
    main()
