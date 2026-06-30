"""Finalize gaia using user-confirmed candidate locations.

Stone & cliff are refined REGIONALLY: anchor on the approved candidate centroids
and grow the full feature with an inclusive rocky mask + morphological close, then
keep the connected component(s) touching each anchor. This captures whole cliff
ridges (the per-colour mask only grabbed fragments) without re-introducing
whole-map noise. Gold/water/trees are kept from the gaia.py pass.
"""
import cv2, numpy as np, json
import extract as E, gaia as G

APPROVED_STONE = {'S0', 'S1', 'S2', 'S3', 'S6', 'S7'}
APPROVED_CLIFF = {'K0', 'K1', 'K2', 'K3', 'K4', 'K6', 'K7', 'K8', 'K9', 'K10', 'K11'}

img, mapmask = E.load()
ef = G.elevation_field()
plates = G.player_plate_mask(img, mapmask)
b, g, r = img[:, :, 0].astype(int), img[:, :, 1].astype(int), img[:, :, 2].astype(int)
mx = np.maximum(np.maximum(b, g), r); mn = np.minimum(np.minimum(b, g), r); sat = mx - mn
M = (mapmask > 0) & (~plates)
cand = json.load(open('gaia_candidates.json'))


def region_grow(full_mask, anchors, close_k=7):
    """Keep connected components of full_mask that contain an anchor point."""
    cm = cv2.morphologyEx(full_mask.astype(np.uint8), cv2.MORPH_CLOSE,
                          np.ones((close_k, close_k), np.uint8))
    n, lab, st, _ = cv2.connectedComponentsWithStats(cm, 8)
    keep = np.zeros_like(cm, bool)
    for (ax, ay) in anchors:
        # search a small neighbourhood for a labelled component
        found = 0
        for dy in range(-4, 5):
            for dx in range(-4, 5):
                yy, xx = ay + dy, ax + dx
                if 0 <= yy < lab.shape[0] and 0 <= xx < lab.shape[1] and lab[yy, xx] > 0:
                    found = lab[yy, xx]; break
            if found:
                break
        if found:
            keep |= (lab == found)
    return keep & (full_mask > 0 if False else keep)  # keep mask itself


# inclusive rocky mask (grey-ish, mid brightness, not strongly brown) for cliffs
rocky = (sat < 50) & (np.abs(r - b) < 40) & (mx >= 55) & (mx < 168) & M
cliff_anchors = [(c['cx'], c['cy']) for c in cand['cliff'] if c['id'] in APPROVED_CLIFF]
cliff_mask = region_grow(rocky, cliff_anchors, close_k=9)

# light-grey stone, regionally anchored
stone_full = (sat < 42) & (np.abs(r - b) < 30) & (mx >= 135) & (mx < 215) & M
stone_anchors = [(c['cx'], c['cy']) for c in cand['stone'] if c['id'] in APPROVED_STONE]
stone_mask = region_grow(stone_full, stone_anchors, close_k=5)

print("cliff mask px:", int(cliff_mask.sum()), "anchors:", len(cliff_anchors))
print("stone mask px:", int(stone_mask.sum()), "anchors:", len(stone_anchors))

# forward-sample to tiles
d = dict(np.load('gaia_tiles.npz'))            # gold, water, tree from gaia.py
cliff_t = G.forward_sample(cliff_mask, ef, cover=0.15)
stone_t = G.forward_sample(stone_mask, ef, cover=0.25)
# priority: keep specific features, then trees; remove overlaps
claimed = np.zeros((G.N, G.N), bool)
final = {}
for k, t in [('water', d['water']), ('gold', d['gold']), ('stone', stone_t),
             ('cliff', cliff_t), ('tree', d['tree'])]:
    t = t & ~claimed
    claimed |= t
    final[k] = t
    print("  %-6s tiles = %d" % (k, int(t.sum())))

np.savez('gaia_final.npz', **final)
np.savetxt('water_grid.txt', final['water'].astype(np.uint8), fmt='%d', delimiter='')

palette = {'tree': (255, 0, 255), 'gold': (0, 255, 255), 'water': (255, 130, 0),
           'stone': (220, 220, 220), 'cliff': (0, 0, 255)}
comb = img.copy()
for k in ['tree', 'water', 'gold', 'stone', 'cliff']:
    comb = G.fill_diamonds(comb, final[k], ef, palette[k])
cv2.imwrite('gaia_final.png', cv2.resize(comb, None, fx=0.6, fy=0.6))
# cliff-only zoom verification overlay
cl = G.fill_diamonds(img, final['cliff'], ef, (0, 0, 255))
cv2.imwrite('cliff_final.png', cv2.resize(cl, None, fx=0.55, fy=0.55))
print("wrote gaia_final.npz, gaia_final.png, cliff_final.png, water_grid.txt")
