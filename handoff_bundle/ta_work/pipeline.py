"""Unified pipeline for player plates: buildings, walls, foundations."""
import cv2, numpy as np, json
import extract as E

AX, AY = E.AX, E.AY
TILE = 2*AX
N = 168
M_AX, M_AY = 1.8363, 0.9167
MBX, MBY = 1942.0, 1283.0
BY0 = 697.5
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

def diamond_kernel(k, inset=1.5):
    w = int(round(TILE*k)); h = int(round(TILE*k/2))
    ys, xs = np.mgrid[0:h+1, 0:w+1]
    cy, cx = h/2, w/2
    hw = TILE*k/2 - inset; hh = hw/2
    return ((np.abs(xs-cx)/hw + np.abs(ys-cy)/hh) <= 1.0).astype(np.uint8)

def find_1x1(mask):
    ker = diamond_kernel(1).astype(np.float32)
    corr = cv2.filter2D((mask>0).astype(np.float32), -1, ker, borderType=cv2.BORDER_CONSTANT)/ker.sum()
    pts=[]
    c = corr.copy()
    H2,W2 = c.shape
    ys,xs = np.mgrid[0:H2,0:W2]
    while True:
        yy,xx = np.unravel_index(np.argmax(c), c.shape)
        if c[yy,xx] < 0.70: break
        pts.append((float(xx),float(yy),float(c[yy,xx])))
        c[(np.abs(xs-xx)/TILE + np.abs(ys-yy)/(TILE/2)) < 0.85] = 0
    return pts

def classify_components(img, mapmask):
    """returns plates: list of dicts {p,k,sx_top,sy_top,kind,bbox,glyph_bbox}"""
    plates=[]
    white_full = img.astype(int).min(axis=2) > 170
    for p in E.PLAYER_COLORS:
        pm = player_mask(img, mapmask, p)
        pm_d = cv2.dilate(pm, np.ones((3,3),np.uint8))   # connect dotted outlines
        n,lab,stats,cent = cv2.connectedComponentsWithStats(pm_d, 8)
        for i in range(1, n):
            x,y,w,h,a = stats[i]
            if a < 30: continue
            comp = (lab[y:y+h, x:x+w] == i) & (pm[y:y+h, x:x+w] > 0)
            area = int(comp.sum())
            if area < 25: continue
            filled = E.fill_holes(comp.astype(np.uint8))
            f = E.diamond_fit(filled[None,:,:][0]*1, 1, (0,0,w,h)) if False else None
            # diamond fit on filled comp
            ys2, xs2 = np.nonzero(filled)
            u = xs2 + 2.0*ys2; v = xs2 - 2.0*ys2
            u_min,u_max = np.percentile(u,1), np.percentile(u,99)
            v_min,v_max = np.percentile(v,1), np.percentile(v,99)
            width = ((u_max+v_max)/2 - (u_min+v_min)/2)
            kf = width/TILE
            top_loc = ((u_min+v_max)/2, (u_min-v_max)/4)
            fillratio = filled.sum()/max(1.0,(width*width/4)/2*2)  # ~ area of diamond w*h/2, h=w/2
            diam_area = width*(width/2)/2
            fillratio = filled.sum()/max(1.0,diam_area)
            glyph = (white_full[y:y+h, x:x+w] & (filled>0))
            garea = int(glyph.sum())
            k = max(1, int(round(kf+0.2)))
            rec = dict(p=p, bbox=[int(x),int(y),int(w),int(h)], area=area, kf=float(kf),
                       garea=garea, fill=float(fillratio),
                       sx_top=float(top_loc[0]+x), sy_top=float(top_loc[1]+y))
            if fillratio < 0.42 and garea < 12 and kf > 1.6:
                rec['kind']='foundation'; rec['k']=k
                plates.append(rec); continue
            if garea >= 15 and k <= 4 and abs(kf+0.2-k) < 0.32 and fillratio > 0.55:
                rec['kind']='building'; rec['k']=k
                plates.append(rec); continue
            # otherwise: merged component -> decompose
            sub = img[y:y+h, x:x+w]
            # glyph clusters -> buildings
            gm = (glyph).astype(np.uint8)
            ng,gl,gs,gc = cv2.connectedComponentsWithStats(gm,8)
            gpts=[]
            for j in range(1,ng):
                if gs[j,4] < 8: continue
                gpts.append([gc[j][0], gc[j][1], gs[j,4]])
            # merge nearby glyph fragments
            merged=[]; used=[False]*len(gpts)
            for a1,(gx,gy,ga) in enumerate(gpts):
                if used[a1]: continue
                xsm,ysm,asm=[gx],[gy],[ga]
                for b1 in range(a1+1,len(gpts)):
                    if used[b1]: continue
                    if abs(gpts[b1][0]-gx)<9 and abs(gpts[b1][1]-gy)<7:
                        xsm.append(gpts[b1][0]); ysm.append(gpts[b1][1]); asm.append(gpts[b1][2]); used[b1]=True
                merged.append((float(np.average(xsm,weights=asm)), float(np.average(ysm,weights=asm)), sum(asm)))
            exclude = np.zeros(filled.shape, bool)
            ys3,xs3 = np.mgrid[0:filled.shape[0],0:filled.shape[1]]
            for gx,gy,ga in merged:
                # local plate width at glyph row
                row=int(round(gy)); k2=2
                if 0<=row<filled.shape[0]:
                    xs0=np.nonzero(filled[row])[0]
                    if len(xs0):
                        runs=np.split(xs0,np.nonzero(np.diff(xs0)>1)[0]+1)
                        for r0 in runs:
                            if r0[0]-1<=gx<=r0[-1]+1:
                                k2=max(1,int(round(len(r0)/TILE+0.15))); break
                plates.append(dict(p=p, kind='building', k=k2, garea=int(ga),
                                   bbox=[int(x),int(y),int(w),int(h)],
                                   sx_top=float(gx+x), sy_top=float(gy+y - TILE*k2/4), from_center=True))
                exclude |= (np.abs(xs3-gx)/(TILE*k2/2) + np.abs(ys3-gy)/(TILE*k2/4)) <= 1.05
            rem = (filled>0)&(~exclude)
            for px,py,cc in find_1x1(rem.astype(np.uint8)):
                plates.append(dict(p=p, kind='wall', k=1, corr=float(cc),
                                   bbox=[int(x),int(y),int(w),int(h)],
                                   sx_top=float(px+x), sy_top=float(py+y - TILE/4), from_center=True))
    return plates

if __name__=='__main__':
    img, mapmask = E.load()
    plates = classify_components(img, mapmask)
    import collections
    print(collections.Counter([(pl['p'],pl['kind']) for pl in plates]))
    print(collections.Counter([(pl['kind'],pl['k']) for pl in plates]))
    json.dump(plates, open('plates.json','w'))
