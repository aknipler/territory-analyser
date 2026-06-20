"""Town Center detection. A TC renders as a solid 2x2 plate at the 4x4 origin
(top-left) with a dotted player-colour ring tracing the full 4x4, extending
DOWN-SCREEN (+r,+c) from the plate. Houses have no such ring.

For each solved 2x2 building, sample the down-screen 4x4 perimeter tiles and test
for player-colour dots. High coverage -> Town Center (origin = the 2x2 origin).
"""
import cv2, numpy as np, json
import extract as E, pipeline3 as P3, gaia as G

AX, AY = G.AX, G.AY


def main():
    img, mapmask = E.load()
    ef = G.elevation_field()
    pmask = {p: P3.player_mask(img, mapmask, p) for p in E.PLAYER_COLORS}
    recs = json.load(open('unified_solution.json'))
    H, W = mapmask.shape

    def tile_frac(p, r, c):
        rr = int(np.clip(r, 0, G.N - 1)); cc = int(np.clip(c, 0, G.N - 1))
        sx, sy = G.tile_screen(r, c, ef[rr, cc])
        hit = tot = 0
        for fx, fy in G._DIAMOND_OFFS:
            x = int(round(sx + fx * AX)); y = int(round(sy + fy * AY))
            if 0 <= y < H and 0 <= x < W:
                tot += 1
                if pmask[p][y, x]:
                    hit += 1
        return hit / tot if tot else 0.0

    # 4x4 ring tiles outside the 2x2 plate (the dotted outline), + interior tiles
    RING = [(0, 2), (0, 3), (1, 3), (2, 3), (3, 3), (3, 2), (3, 1), (3, 0), (2, 0)]
    INTERIOR = [(1, 2), (2, 1), (2, 2)]
    cands = []
    for rc in recs:
        if rc.get('offmap') or rc['kind'] != 'building' or rc['k'] != 2:
            continue
        p = rc['p']; x0 = rc['x0']; y0 = rc['y0']
        if x0 + 4 > G.N or y0 + 4 > G.N:
            continue
        fr = [tile_frac(p, x0 + di, y0 + dj) for di, dj in RING]
        ndot = sum(1 for f in fr if 0.04 < f < 0.5)        # sparse dots
        nsolid = sum(1 for f in fr if f >= 0.5)            # solid neighbour plates
        int_solid = sum(1 for di, dj in INTERIOR if tile_frac(p, x0 + di, y0 + dj) >= 0.5)
        if ndot >= 5 and nsolid <= 1 and int_solid == 0:
            cands.append((rc, ndot - 0.5 * nsolid))

    cands.sort(key=lambda t: -t[1])
    print("TC candidates:", len(cands))
    crops = []
    out = []
    for rank, (rc, frac) in enumerate(cands):
        p, x0, y0 = rc['p'], rc['x0'], rc['y0']
        out.append(dict(id="T%d" % rank, p=p, x0=x0, y0=y0, frac=round(float(frac), 2)))
        # crop around the 4x4 for verification
        sx, sy = G.tile_screen(x0 + 2, y0 + 2, ef[x0, y0])
        R = 45
        crop = img[max(0, int(sy) - R):int(sy) + R, max(0, int(sx) - R):int(sx) + R]
        crop = cv2.resize(crop, (165, 165), interpolation=cv2.INTER_NEAREST)
        cv2.putText(crop, "T%d p%d %.2f" % (rank, p, frac), (3, 16),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1)
        cv2.rectangle(crop, (0, 0), (164, 164), (0, 200, 0), 1)
        crops.append(crop)
    if crops:
        while len(crops) % 4:
            crops.append(np.zeros((165, 165, 3), np.uint8))
        grid = np.vstack([np.hstack(crops[i:i + 4]) for i in range(0, len(crops), 4)])
        cv2.imwrite('tc_candidates.png', grid)
    json.dump(out, open('tc_candidates.json', 'w'), indent=1)
    import collections
    print("by player:", collections.Counter(o['p'] for o in out))
    print("ids:", [o['id'] for o in out])


if __name__ == '__main__':
    main()
