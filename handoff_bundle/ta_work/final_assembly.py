"""Assemble the final updateBuilding test-case file (example1.txt style).

Codebase signature (no team arg): analyser.updateBuilding(x, y, "Name", player, "add");
Footprints come from buildingsDict.json; building names from the user-labelled glyph
clusters. Town Centers are NOT auto-detected reliably yet -> emitted as a separate,
clearly-marked TODO block for the user to confirm/place.
"""
import json, numpy as np
import collections

# ---- user-confirmed glyph cluster -> AoE2 building name ----
CLUSTER_NAME = {
    0: 'House', 1: 'House', 2: 'House', 3: 'Stable', 4: 'Market',
    5: 'Siege Workshop', 6: 'Lumber Camp', 7: 'Archery Range', 8: 'Archery Range',
    9: 'Blacksmith', 10: 'Castle', 11: 'House', 12: 'Barracks', 13: 'House',
    14: 'Barracks', 15: 'House', 16: 'Mill', 17: 'House', 18: 'House', 19: 'Barracks',
}
# clusters that actually hold an extra adjacent building (flagged, primary emitted)
SPLIT_NOTE = {17: 'two Houses side-by-side', 18: 'House + Mill'}

FOOT = {  # obstruction width/height from buildingsDict.json
    'House': 2, 'Mill': 2, 'Mining Camp': 2, 'Lumber Camp': 2,
    'Barracks': 3, 'Archery Range': 3, 'Stable': 3, 'Monastery': 3, 'Blacksmith': 3,
    'Town Center': 4, 'Market': 4, 'Siege Workshop': 4, 'University': 4, 'Castle': 4,
    'Tower': 1, 'Palisade Wall': 1,
}

PLAYER_NAME = {1: 'blue (NW)', 2: 'red (NE)', 3: 'green (SW)', 4: 'yellow (SE)'}


def line(x, y, name, player):
    return 'analyser.updateBuilding(%d,%d,"%s",%d, "add");' % (x, y, name, player)


def main():
    recs = json.load(open('unified_solution.json'))
    idx2cluster = {int(k): v for k, v in json.load(open('building_clusters.json')).items()}
    gaia = np.load('gaia_final.npz')

    out = ['# Initial building placement commands',
           '# Extracted from screenshot_1000029911.png  (AoE2 DE, 168x168 Medium map)',
           '# Coordinates: map LEFT corner = (0,0); footprint origin = screen-left corner of plate.',
           '']

    # ---------- GAIA ----------
    out.append('# ===== GAIA (player 0) =====')
    for key, name in [('tree', 'Tree'), ('gold', 'Gold Mine'),
                      ('stone', 'Stone Mine'), ('cliff', 'Cliff')]:
        tiles = np.argwhere(gaia[key])
        out.append('# %s  (%d tiles)' % (name, len(tiles)))
        for r, c in tiles:
            out.append(line(int(r), int(c), name, 0))
        out.append('')

    # ---------- PLAYERS ----------
    counts = collections.Counter()
    flags = []
    by_player = collections.defaultdict(list)
    for rc in recs:
        if rc.get('offmap') or rc['kind'] == 'foundation':
            continue
        p = rc['p']
        if rc['kind'] == 'wall':
            by_player[p].append(('Palisade Wall', rc['x0'], rc['y0']))
            counts[(p, 'Palisade Wall')] += 1
            continue
        cl = idx2cluster.get(rc['idx'])
        name = CLUSTER_NAME.get(cl, 'House')   # default unlabelled -> House
        if cl is None:
            flags.append('  building idx %d p%d had no glyph -> defaulted to House' % (rc['idx'], p))
        k_auth = FOOT[name]
        # adjust origin if authoritative footprint differs from the solved size
        y0 = rc['y0'] + (rc['k'] - k_auth)
        x0 = rc['x0']
        by_player[p].append((name, x0, y0))
        counts[(p, name)] += 1
        if cl in SPLIT_NOTE:
            flags.append('  cluster C%d (%s) at (%d,%d): %s -- extra building to add manually'
                         % (cl, name, x0, y0, SPLIT_NOTE[cl]))

    for p in (1, 2, 3, 4):
        out.append('# ===== Player %d  %s =====' % (p, PLAYER_NAME[p]))
        items = by_player[p]
        # buildings first (by name), then walls
        builds = sorted([t for t in items if t[0] != 'Palisade Wall'], key=lambda t: t[0])
        walls = [t for t in items if t[0] == 'Palisade Wall']
        cur = None
        for name, x0, y0 in builds:
            if name != cur:
                out.append('# %s' % name); cur = name
            out.append(line(x0, y0, name, p))
        if walls:
            out.append('# Palisade Wall (%d)' % len(walls))
            for name, x0, y0 in walls:
                out.append(line(x0, y0, name, p))
        out.append('')

    # ---------- TOWN CENTERS (pending) ----------
    out.append('# ===== TOWN CENTERS -- PENDING USER CONFIRMATION =====')
    out.append('# The numeral TC plates (2x2 core = origin of a 4x4 Town Center) are not yet')
    out.append('# reliably auto-located. Add: analyser.updateBuilding(x,y,"Town Center",player,"add");')
    out.append('')

    open('extracted_1000029911.txt', 'w').write('\n'.join(out) + '\n')

    # report
    print('wrote extracted_1000029911.txt  (%d lines)' % len(out))
    print('building/wall counts by player:')
    for p in (1, 2, 3, 4):
        pc = {n: v for (pp, n), v in counts.items() if pp == p}
        print('  p%d: %s' % (p, pc))
    tot = {k: int(gaia[k].sum()) for k in ['tree', 'gold', 'stone', 'cliff', 'water']}
    print('gaia tiles:', tot)
    if flags:
        print('FLAGS:')
        for f in flags:
            print(f)


if __name__ == '__main__':
    main()
