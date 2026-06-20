"""v2: classify player-color components into buildings/walls/foundations/wall-dots."""
import cv2, numpy as np, json
import extract as E

AX, AY = E.AX, E.AY
TILE = 2*AX
GOLD_COLS = [(15,172,210),(30,223,246),(34,144,190)]

def player_mask(img, mapmask, p):
    c = np.array(E.PLAYER_COLORS[p], int)
    d = np.abs(img.astype(int)-c).sum(axis=2)
    mask = (d<70)
    if p == 4:
        for gc in GOLD_COLS:
            dg = np.abs(img.astype(int)-np.array(gc,int)).sum(axis=2)
            mask &= (d < dg - 5)
    return (mask & (mapmask>0)).astype(np.uint8)

def fill_holes_pad(comp):
    c = np.pad(comp.astype(np.uint8), 2)
    f = E.fill_holes(c)
    return f[2:-2,2:-2]

def diamond_kernel(k, inset=1.5):
    w = int(round(TILE*k)); h = int(round(TILE*k/2))
    ys, xs = np.mgrid[0:h+1, 0:w+1]
    cy, cx = h/2, w/2
    hw = TILE*k/2 - inset; hh = hw/2
    return ((np.abs(xs-cx)/hw + np.abs(ys-cy)/hh) <= 1.0).astype(np.uint8)

def find_1x1(mask):
    ker = diamond_kernel(1).astype(np.float32)
    corr = cv2.filter2D((mask>0).astype(np.float32), -1, ker, borderType=cv2.BORDER_CONSTANT)/ker.sum()
    pts=[]; c=corr.copy()
    H2,W2=c.shape; ys,xs=np.mgrid[0:H2,0:W2]
    while True:
        yy,xx = np.unravel_index(np.argmax(c), c.shape)
        if c[yy,xx] < 0.70: break
        pts.append((float(xx),float(yy),float(c[yy,xx])))
        c[(np.abs(xs-xx)/TILE + np.abs(ys-yy)/(TILE/2)) < 0.85] = 0
    return pts

def glyph_clusters(glyph_mask):
    g = cv2.dilate(glyph_mask.astype(np.uint8), np.ones((5,9),np.uint8))
    n,lab,stats,cent = cv2.connectedComponentsWithStats(g,8)
    out=[]
    for i in range(1,n):
        sel = (lab==i)&(glyph_mask>0)
        a = int(sel.sum())
        if a < 8: continue
        ys,xs = np.nonzero(sel)
        out.append((float(xs.mean()), float(ys.mean()), a, (int(xs.min()),int(ys.min()),int(xs.max()),int(ys.max()))))
    return out

def run_width_at(filled, gx, gy):
    row = int(round(gy))
    if not (0 <= row < filled.shape[0]): return None
    xs0 = np.nonzero(filled[row])[0]
    if not len(xs0): return None
    runs = np.split(xs0, np.nonzero(np.diff(xs0)>1)[0]+1)
    for r0 in runs:
        if r0[0]-1 <= gx <= r0[-1]+1:
            return len(r0), (r0[0]+r0[-1])/2
    return None

def classify(img, mapmask):
    plates=[]
    white_full = img.astype(int).min(axis=2) > 170
    for p in E.PLAYER_COLORS:
        pm = player_mask(img, mapmask, p)
        pm_d = cv2.dilate(pm, np.ones((3,3),np.uint8))
        n,lab,stats,cent = cv2.connectedComponentsWithStats(pm_d, 8)
        for i in range(1,n):
            x,y,w,h,_ = stats[i]
            comp = (lab[y:y+h, x:x+w]==i) & (pm[y:y+h, x:x+w]>0)
            raw = int(comp.sum())
            if raw < 15: continue
            filled = fill_holes_pad(comp)
            farea = int(filled.sum())
            ys2,xs2 = np.nonzero(filled)
            u = xs2+2.0*ys2; v = xs2-2.0*ys2
            u_min,u_max = np.percentile(u,1), np.percentile(u,99)
            v_min,v_max = np.percentile(v,1), np.percentile(v,99)
            width = (u_max+v_max)/2 - (u_min+v_min)/2
            kf = width/TILE
            glyph = white_full[y:y+h, x:x+w] & (filled>0)
            garea = int(glyph.sum())
            base = dict(p=p, bbox=[int(x),int(y),int(w),int(h)], raw=raw, farea=farea,
                        kf=float(kf), garea=garea)
            # foundation ring: filling multiplies area, no glyph
            if garea < 10 and farea > 2.2*raw and kf > 1.5:
                k = max(2, int(round(kf+0.2)))
                top = ((u_min+v_max)/2, (u_min-v_max)/4)
                plates.append(dict(base, kind='foundation', k=int(k),
                                   sx_top=float(top[0]+x), sy_top=float(top[1]+y)))
                continue
            if raw < 35: 
                # tiny pure blob: wall foundation dot candidate
                purity = raw/max(1,w*h)
                if purity > 0.5:
                    plates.append(dict(base, kind='wall_dot', k=1,
                                       sx_top=float(x+w/2), sy_top=float(y+h/2 - TILE/4)))
                continue
            gcs = glyph_clusters(glyph)
            # single clean plate?
            diam_area = width*(width/2)/2
            fillr = farea/max(1.0,diam_area)
            k_round = max(1,int(round(kf+0.2)))
            if len(gcs)<=1 and garea>=8 and k_round<=4 and abs(kf+0.2-k_round)<0.32 and fillr>0.55:
                top = ((u_min+v_max)/2, (u_min-v_max)/4)
                plates.append(dict(base, kind='building', k=int(k_round),
                                   sx_top=float(top[0]+x), sy_top=float(top[1]+y),
                                   glyph_bbox=[int(x+gcs[0][3][0]),int(y+gcs[0][3][1]),
                                               int(x+gcs[0][3][2]),int(y+gcs[0][3][3])] if gcs else None))
                continue
            # merged: glyph-driven buildings + wall remainder
            exclude = np.zeros(filled.shape, bool)
            ys3,xs3 = np.mgrid[0:filled.shape[0],0:filled.shape[1]]
            cands=[]
            for gx,gy,ga,gb in gcs:
                rw = run_width_at(filled, gx, gy)
                if rw is None: continue
                runw, runcx = rw
                k2 = max(1,int(round(runw/TILE+0.15)))
                cands.append((runcx, gy, k2, ga, gb))
            # dedupe glyph plates whose centers coincide
            cands.sort(key=lambda t:-t[3])
            kept=[]
            for cx0,cy0,k2,ga,gb in cands:
                dup=False
                for cx1,cy1,k1,_,_ in kept:
                    if abs(cx0-cx1) < TILE*0.5*max(k1,k2) and abs(cy0-cy1) < TILE*0.25*max(k1,k2):
                        dup=True; break
                if not dup: kept.append((cx0,cy0,k2,ga,gb))
            for cx0,cy0,k2,ga,gb in kept:
                plates.append(dict(base, kind='building', k=int(k2), garea=int(ga),
                                   sx_top=float(cx0+x), sy_top=float(cy0+y - TILE*k2/4),
                                   from_center=True,
                                   glyph_bbox=[int(x+gb[0]),int(y+gb[1]),int(x+gb[2]),int(y+gb[3])]))
                exclude |= (np.abs(xs3-cx0)/(TILE*k2/2) + np.abs(ys3-cy0)/(TILE*k2/4)) <= 1.05
            rem = (filled>0)&(~exclude)
            for px,py,cc in find_1x1(rem.astype(np.uint8)):
                plates.append(dict(p=p, kind='wall', k=1, corr=float(cc), bbox=base['bbox'],
                                   sx_top=float(px+x), sy_top=float(py+y - TILE/4), from_center=True))
    return plates

if __name__=='__main__':
    img, mapmask = E.load()
    plates = classify(img, mapmask)
    import collections
    print(collections.Counter([(pl['p'],pl['kind']) for pl in plates]))
    print(collections.Counter([(pl['kind'],pl['k']) for pl in plates]))
    json.dump(plates, open('plates2.json','w'))
