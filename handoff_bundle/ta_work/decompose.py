"""Decompose merged marker components (wall chains + attached buildings)."""
import cv2, numpy as np, json
import extract as E
import solve_final as SF

AX, AY = E.AX, E.AY
TILE = 2*AX

def diamond_kernel(k, inset=1.5):
    """binary diamond mask for a k-tile plate, width 2*AX*k - 2*inset"""
    w = int(round(TILE*k)); h = int(round(TILE*k/2))
    ker = np.zeros((h+1, w+1), np.uint8)
    cy, cx = h/2, w/2
    hw = TILE*k/2 - inset; hh = hw/2
    ys, xs = np.mgrid[0:h+1, 0:w+1]
    ker[(np.abs(xs-cx)/hw + np.abs(ys-cy)/hh) <= 1.0] = 1
    return ker

def comp_mask(img, mapmask, color, bbox, pad=4, tol=70):
    x,y,w,h = bbox
    x0,y0 = max(0,x-pad), max(0,y-pad)
    sub = img[y0:y+h+pad, x0:x+w+pad]
    c = np.array(color,int)
    d = np.abs(sub.astype(int)-c).sum(axis=2)
    mask = (d<tol).astype(np.uint8)
    mask = E.fill_holes(mask)
    return mask, (x0,y0), sub

def white_glyphs(sub, mask):
    """white glyph pixels inside plates"""
    g = (sub.astype(int).min(axis=2) > 170) & (mask>0)
    g = g.astype(np.uint8)
    n,lab,stats,cent = cv2.connectedComponentsWithStats(g, 8)
    out=[]
    for i in range(1,n):
        if stats[i,4] < 8: continue
        out.append((cent[i][0], cent[i][1], stats[i,4]))
    # merge glyph fragments within 6px
    merged=[]
    used=[False]*len(out)
    for i,(x,y,a) in enumerate(out):
        if used[i]: continue
        gx,gy,ga=[x],[y],[a]
        for j in range(i+1,len(out)):
            if used[j]: continue
            if abs(out[j][0]-x)<9 and abs(out[j][1]-y)<7:
                gx.append(out[j][0]); gy.append(out[j][1]); ga.append(out[j][2]); used[j]=True
        merged.append((float(np.average(gx,weights=ga)), float(np.average(gy,weights=ga)), int(sum(ga))))
    return merged

def find_1x1_plates(mask, exclude):
    """correlation with 1x1 diamond; return centers (local px)"""
    ker = diamond_kernel(1).astype(np.float32)
    free = (mask>0) & (~exclude)
    corr = cv2.filter2D(free.astype(np.float32), -1, ker, borderType=cv2.BORDER_CONSTANT)
    corr /= ker.sum()
    # peaks
    pts=[]
    c = corr.copy()
    H2,W2 = c.shape
    ys,xs = np.mgrid[0:H2, 0:W2]
    while True:
        idx = np.argmax(c)
        yy,xx = np.unravel_index(idx, c.shape)
        if c[yy,xx] < 0.70: break
        pts.append((float(xx),float(yy),float(c[yy,xx])))
        # suppress within 0.85-tile diamond around the peak (adjacent tiles survive)
        supp = (np.abs(xs-xx)/TILE + np.abs(ys-yy)/(TILE/2)) < 0.85
        c[supp] = 0
    return pts

def decompose(img, mapmask, all_m, good):
    out_markers=[]   # dicts: p, k, center_sx, center_sy, kind
    for m,g in zip(all_m, good):
        if g: continue
        p = m['p']; col = E.PLAYER_COLORS[p]
        mask,(ox,oy),sub = comp_mask(img, mapmask, col, m['bbox'])
        glyphs = white_glyphs(sub, mask)
        exclude = np.zeros(mask.shape, bool)
        for gx,gy,ga in glyphs:
            # building plate centered at glyph; k from local plate width: measure span of mask
            # row through glyph center
            k = 2  # default house in chains; refine by local width
            row = int(round(gy))
            if 0<=row<mask.shape[0]:
                xs = np.nonzero(mask[row])[0]
                # contiguous run containing gx
                runw = 0
                if len(xs):
                    runs=np.split(xs, np.nonzero(np.diff(xs)>1)[0]+1)
                    for r0 in runs:
                        if r0[0]-1 <= gx <= r0[-1]+1:
                            runw = len(r0); break
                kf = runw/TILE
                k = max(1, int(round(kf+0.15)))
            out_markers.append(dict(p=p, k=k, csx=gx+ox, csy=gy+oy, kind='glyph_building', garea=ga))
            hw = TILE*k/2
            ys,xs = np.mgrid[0:mask.shape[0], 0:mask.shape[1]]
            exclude |= (np.abs(xs-gx)/hw + np.abs(ys-gy)/(hw/2)) <= 1.05
        plates = find_1x1_plates(mask, exclude)
        for px,py,cc in plates:
            out_markers.append(dict(p=p, k=1, csx=px+ox, csy=py+oy, kind='wall', corr=cc))
    return out_markers

if __name__=='__main__':
    img, mapmask = E.load()
    all_m = json.load(open('markers_fit.json'))
    good = np.load('good_mask.npy')
    out = decompose(img, mapmask, all_m, good)
    import collections
    print(collections.Counter([(o['p'],o['kind']) for o in out]))
    json.dump(out, open('decomposed.json','w'))
