"""Unified tile-position solve over ALL plates3 entries (buildings+walls+foundations).

Reuses the verified solve_final machinery (minimap colour scoring to defeat the
terrain-elevation d-ambiguity, neighbour smoothing for ties, greedy non-overlap),
but consumes pipeline3's plates3.json instead of just the 302 clean single plates.

Each plate carries its diamond TOP corner (sx_top, sy_top) and footprint k. The
global lattice phase (phx, phy) is anchored from the 302 reliable single-plate
top corners in markers_fit.json so absolute tile coords match marker_solution.json.

Output: unified_solution.json (per plate: p, kind, k, x0, y0, S, d, score, ...)
plus check_unified.png (footprints drawn over the upscaled minimap).
"""
import cv2, numpy as np, json, collections
import extract as E
import solve_final as SF

AX, AY = E.AX, E.AY
TILE = 2 * AX
N = E.N
BY0 = SF.BY0


def anchors(all_m, good):
    """Global lattice phase from the reliable single-plate top corners."""
    sxT = np.array([m['top'][0] for m in all_m])
    syT = np.array([m['top'][1] for m in all_m])
    phx = E.solve_phase(sxT[good], AX)
    n0 = round((26.0 - phx) / AX)
    phy = E.solve_phase(syT[good], AY)
    return phx, n0, phy


def solve(img, plates, phx, n0, phy):
    mm = SF.mm_masks(img)
    recs = []
    for idx, pl in enumerate(plates):
        k = int(pl['k']); p = int(pl['p'])
        sx, sy = pl['sx_top'], pl['sy_top']
        S = int(round((sx - phx) / AX) - n0)
        base = float((phy + AY * round((sy - phy) / AY) - BY0) / AY)
        cands = SF.candidates(S, base, k)
        if not cands:
            recs.append(dict(idx=idx, p=p, kind=pl['kind'], k=k, S=S, base=base,
                             x0=-1, y0=-1, d=0, score=0.0, margin=0.0, nties=0,
                             offmap=True))
            continue
        scored = []
        for d, x0, y0 in cands:
            scored.append([SF.mm_score(mm, p, x0, y0, k), d, x0, y0])
        scored.sort(key=lambda t: (-t[0], abs(t[1] - base)))
        top = scored[0]
        margin = top[0] - (scored[1][0] if len(scored) > 1 else 0)
        ties = [t for t in scored if t[0] == top[0]]
        recs.append(dict(idx=idx, p=p, kind=pl['kind'], k=k, S=S, base=base,
                         cands=[[float(t[0]), int(t[1]), int(t[2]), int(t[3])] for t in scored[:8]],
                         d=int(top[1]), x0=int(top[2]), y0=int(top[3]),
                         score=float(top[0]), margin=float(margin), nties=len(ties)))

    # --- neighbour smoothing for ties (elevation proxy from confident neighbours) ---
    placed = [r for r in recs if not r.get('offmap')]
    conf = [r for r in placed if r['nties'] == 1 and r['score'] >= 0.75]
    cpos = np.array([[r['x0'], r['y0']] for r in conf]) if conf else np.zeros((0, 2))
    ce = np.array([r['d'] - r['base'] for r in conf]) if conf else np.zeros(0)
    for r in placed:
        if r['nties'] <= 1:
            continue
        pos = np.array([r['x0'], r['y0']])
        if len(cpos):
            dd = np.abs(cpos - pos).sum(axis=1); sel = dd < 24
            target = float(np.median(ce[sel])) if sel.sum() >= 2 else 0.0
        else:
            target = 0.0
        best = min([c for c in r['cands'] if c[0] == r['score']],
                   key=lambda c: abs((c[1] - r['base']) - target))
        r['d'], r['x0'], r['y0'] = int(best[1]), int(best[2]), int(best[3])
        r['smoothed'] = True

    # --- greedy non-overlap (confident & larger first) ---
    order = sorted(range(len(placed)),
                   key=lambda j: (-placed[j]['score'] * placed[j].get('margin', 0.0),
                                  -placed[j]['k'], placed[j]['nties']))
    occ = np.full((N, N), -1, np.int32)
    def free(x0, y0, k):
        return not (occ[x0:x0 + k, y0:y0 + k] >= 0).any()
    conflicts = 0
    for j in order:
        r = placed[j]
        if free(r['x0'], r['y0'], r['k']):
            occ[r['x0']:r['x0'] + r['k'], r['y0']:r['y0'] + r['k']] = j
            continue
        moved = False
        for c in r.get('cands', []):
            x0, y0 = int(c[2]), int(c[3])
            if free(x0, y0, r['k']):
                r.update(d=int(c[1]), x0=x0, y0=y0, score=float(c[0]), resolved=True)
                occ[x0:x0 + r['k'], y0:y0 + r['k']] = j
                moved = True
                break
        if not moved:
            r['overlap_unresolved'] = True
            conflicts += 1
    print("unresolved overlaps:", conflicts)
    return recs


def render_check(img, recs, plates):
    mmimg = img[1125:1434, 1941:2559].copy()
    sc = 3
    big = cv2.resize(mmimg, None, fx=sc, fy=sc, interpolation=cv2.INTER_NEAREST)
    colmap = {'building': (255, 255, 255), 'wall': (0, 255, 255), 'foundation': (0, 128, 255)}
    for r in recs:
        if r.get('offmap'):
            continue
        k = r['k']; pts = []
        for (rr, cc) in [(0, 0), (k, 0), (k, k), (0, k)]:
            x = (SF.MBX - 1941 + SF.M_AX * ((r['x0'] + rr) + (r['y0'] + cc))) * sc
            y = (SF.MBY - 1125 + SF.M_AY * ((r['x0'] + rr) - (r['y0'] + cc))) * sc
            pts.append([x, y])
        col = colmap.get(r['kind'], (255, 255, 255))
        if r.get('overlap_unresolved'):
            col = (0, 0, 255)
        cv2.polylines(big, [np.array(pts, np.int32)], True, col, 1)
    cv2.imwrite('check_unified.png', big)


if __name__ == '__main__':
    img, mapmask = E.load()
    plates = json.load(open('plates3.json'))
    all_m = json.load(open('markers_fit.json'))
    good = np.load('good_mask.npy')
    phx, n0, phy = anchors(all_m, good)
    recs = solve(img, plates, phx, n0, phy)
    placed = [r for r in recs if not r.get('offmap')]
    sc = np.array([r['score'] for r in placed])
    print("plates:", len(recs), "placed:", len(placed),
          "offmap:", sum(1 for r in recs if r.get('offmap')))
    print("mean score:", round(sc.mean(), 3), "low(<0.5):", int((sc < 0.5).sum()))
    print("smoothed:", sum(1 for r in placed if r.get('smoothed')),
          "resolved:", sum(1 for r in placed if r.get('resolved')),
          "overlap_unresolved:", sum(1 for r in placed if r.get('overlap_unresolved')))
    print("by kind:", collections.Counter(r['kind'] for r in placed))
    json.dump(recs, open('unified_solution.json', 'w'))
    render_check(img, recs, plates)
    print("wrote unified_solution.json + check_unified.png")
