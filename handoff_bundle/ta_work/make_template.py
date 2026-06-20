import json
cl = json.load(open('glyph_clusters.json'))
L = [
    '# GLYPH LABELLING TEMPLATE  (fill the NAME column)',
    '# View glyph_grid_top.png / glyph_grid_bot.png (readable) or contact_sheet.png',
    '# Footprint hints:',
    '#   2x2 -> House / Mining Camp / Lumber Camp',
    '#   3x3 -> Barracks / Archery Range / Stable / Monastery / Blacksmith',
    '#   4x4 -> Town Center / Market / Siege Workshop / University / Castle',
    '#   1x1 -> Tower (Outpost)  [note: some 1x1 here are mis-sized Houses]',
    '# Known legend: axe=Lumber Camp, crossed swords=Barracks, house=House,',
    '#   grad cap=University, hammer+anvil=Blacksmith, catapult=Siege Workshop,',
    '#   scales=Market, pickaxe=Mining Camp',
    '',
    'cluster | footprint | count | players | NAME',
]
for c in cl:
    k = c['k']
    km = max(k, key=k.get) if k else '?'
    ps = ' '.join('p%s=%s' % (p, n) for p, n in sorted(c['players'].items()))
    L.append('C%-2s | %sx%s | %3d | %-22s | ____________' % (c['cluster'], km, km, c['count'], ps))
open('GLYPH_LABELS_TEMPLATE.txt', 'w').write('\n'.join(L) + '\n')
print('\n'.join(L))
