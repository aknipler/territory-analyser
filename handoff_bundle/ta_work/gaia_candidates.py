"""Render labelled candidate crops for the localized gaia (gold/stone/cliff) so the
user can confirm which clusters are real. Each candidate gets an id; reply with the
ids that are genuine and I keep only those."""
import cv2, numpy as np, json
import extract as E, gaia as G

img, mapmask = E.load()
plates = G.player_plate_mask(img, mapmask)
b, g, r = img[:, :, 0].astype(int), img[:, :, 1].astype(int), img[:, :, 2].astype(int)
mx = np.maximum(np.maximum(b, g), r); mn = np.minimum(np.minimum(b, g), r); sat = mx - mn
M = (mapmask > 0) & (~plates)

gold = ((r > 195) & (g > 165) & (b < 70) & (sat > 120) & M).astype(np.uint8)
stone = ((sat < 40) & (np.abs(r - b) < 28) & (mx >= 145) & (mx < 212) & M).astype(np.uint8)
# cliffs: broad neutral-grey, morphologically closed to connect textured rock
cliff = ((sat < 44) & (np.abs(r - b) < 34) & (mx >= 80) & (mx < 150) & M).astype(np.uint8)
cliff = cv2.morphologyEx(cliff, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))


def candidates(mask, prefix, min_area, topn, tint):
    n, lab, st, cent = cv2.connectedComponentsWithStats(mask, 8)
    comps = sorted([i for i in range(1, n) if st[i, 4] >= min_area],
                   key=lambda i: -st[i, 4])[:topn]
    cells = []
    info = []
    for rank, i in enumerate(comps):
        cx, cy = int(cent[i][0]), int(cent[i][1])
        a = int(st[i, 4])
        R = 55
        sub = img[max(0, cy - R):cy + R, max(0, cx - R):cx + R].copy()
        subm = (lab[max(0, cy - R):cy + R, max(0, cx - R):cx + R] == i)
        sub[subm] = (sub[subm] * 0.4 + np.array(tint) * 0.6).astype(np.uint8)
        sub = cv2.resize(sub, (165, 165), interpolation=cv2.INTER_NEAREST)
        cv2.putText(sub, "%s%d a%d" % (prefix, rank, a), (3, 16),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
        cv2.rectangle(sub, (0, 0), (164, 164), (0, 200, 0), 1)
        cells.append(sub)
        info.append(dict(id="%s%d" % (prefix, rank), cx=cx, cy=cy, area=a))
    return cells, info


allinfo = {}
for prefix, mask, ma, tn, tint, name in [
        ('G', gold, 10, 8, (0, 255, 255), 'gold'),
        ('S', stone, 16, 10, (220, 220, 220), 'stone'),
        ('K', cliff, 120, 12, (0, 0, 255), 'cliff')]:
    cells, info = candidates(mask, prefix, ma, tn, tint)
    allinfo[name] = info
    if not cells:
        print(name, 'NO candidates'); continue
    while len(cells) % 4:
        cells.append(np.zeros((165, 165, 3), np.uint8))
    grid = np.vstack([np.hstack(cells[i:i + 4]) for i in range(0, len(cells), 4)])
    cv2.imwrite('cand_%s.png' % name, grid)
    print(name, 'candidates:', [x['id'] for x in info])
json.dump(allinfo, open('gaia_candidates.json', 'w'), indent=1)
