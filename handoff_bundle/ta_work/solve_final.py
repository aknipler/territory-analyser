"""Final marker solve: minimap scoring with margins, neighbor smoothing, non-overlap."""
import cv2, numpy as np, json
import extract as E

AX, AY = E.AX, E.AY
N = 168
M_AX, M_AY = 1.8363, 0.9167
MBX, MBY = 1942.0, 1283.0
BY0 = 697.5

def mm_masks(img):
    mm_colors = {1:[(241,42,27),(169,48,36)],2:[(28,28,236),(1,1,183)],
                 3:[(18,221,15),(0,165,0)],4:[(12,229,229),(0,202,202)]}
    region = np.zeros(img.shape[:2],bool); region[1125:1434, 1941:2559]=True
    out={}
    for p,cols in mm_colors.items():
        acc=np.zeros(img.shape[:2],bool)
        for c in cols:
            d=np.abs(img.astype(int)-np.array(c,int)).sum(axis=2)
            acc |= (d<90)
        out[p]=acc&region
    return out

def mm_score(mm, p, x0, y0, k):
    hits=0
    for rr in range(k):
        for cc in range(k):
            r=x0+rr+0.5; c=y0+cc+0.5
            xx=int(round(MBX+M_AX*(r+c))); yy=int(round(MBY+M_AY*(r-c)))
            if mm[p][yy,xx]: hits+=1
    return hits/(k*k)

def candidates(S, base, k, win=12):
    c0 = int(np.floor(base))-3
    out=[]
    for d in range(c0, c0+win):
        if (d-S)%2: continue
        x0=(S+d)//2; y0=(S-d)//2-k
        if x0<0 or y0<0 or x0+k>N or y0+k>N: continue
        out.append((d,x0,y0))
    return out

def solve_markers(img, all_m, good):
    mm = mm_masks(img)
    sxT=np.array([m['top'][0] for m in all_m]); syT=np.array([m['top'][1] for m in all_m])
    ks=np.array([m['k'] for m in all_m]); ps=np.array([m['p'] for m in all_m])
    phx = E.solve_phase(sxT[good], AX)
    n0 = round((26.0-phx)/AX)
    S = np.round((sxT-phx)/AX).astype(int) - n0
    phy = E.solve_phase(syT[good], AY)
    Dlat = np.round((syT-phy)/AY)
    base = (phy + AY*Dlat - BY0)/AY

    recs=[]
    for i in np.nonzero(good)[0]:
        k=int(ks[i]); p=int(ps[i])
        cands = candidates(int(S[i]), float(base[i]), k)
        scored=[]
        for d,x0,y0 in cands:
            sc = mm_score(mm,p,x0,y0,k)
            scored.append([sc,d,x0,y0])
        scored.sort(key=lambda t:(-t[0], abs(t[1]-base[i])))
        if not scored: continue
        top = scored[0]
        margin = top[0] - (scored[1][0] if len(scored)>1 else 0)
        # ambiguous if several candidates score equal
        ties = [t for t in scored if t[0]==top[0]]
        recs.append(dict(i=int(i), p=p, k=k, S=int(S[i]), base=float(base[i]),
                         cands=[[float(t[0]),int(t[1]),int(t[2]),int(t[3])] for t in scored[:8]],
                         d=int(top[1]), x0=int(top[2]), y0=int(top[3]),
                         score=float(top[0]), margin=float(margin), nties=len(ties)))
    # neighbor smoothing for ambiguous markers: prefer candidate whose eproxy (d-base)
    # is close to the median eproxy of nearby confident markers
    conf = [r for r in recs if r['nties']==1 and r['score']>=0.75]
    cpos = np.array([[r['x0'],r['y0']] for r in conf]); ce = np.array([r['d']-r['base'] for r in conf])
    for r in recs:
        if r['nties']<=1: continue
        pos = np.array([r['x0'],r['y0']])
        dd = np.abs(cpos-pos).sum(axis=1)
        sel = dd<24
        if sel.sum()>=2:
            target = np.median(ce[sel])
        else:
            target = 0.0
        best = min([c for c in r['cands'] if c[0]==r['score']],
                   key=lambda c: abs((c[1]-r['base'])-target))
        r['d'],r['x0'],r['y0'] = int(best[1]),int(best[2]),int(best[3])
        r['smoothed']=True
    # non-overlap resolution: greedy by confidence
    order = sorted(range(len(recs)), key=lambda j: (-recs[j]['score']*recs[j].get('margin',0.0), recs[j]['nties']))
    occ = np.zeros((N,N), np.int32); occ[:] = -1
    def fits(r):
        x0,y0,k = r['x0'],r['y0'],r['k']
        return not (occ[x0:x0+k, y0:y0+k]>=0).any()
    def place(r, j):
        occ[r['x0']:r['x0']+r['k'], r['y0']:r['y0']+r['k']] = j
    conflicts=0
    for j in order:
        r = recs[j]
        if fits(r): place(r,j); continue
        moved=False
        for c in r['cands']:
            r2 = dict(r); r2['d'],r2['x0'],r2['y0']=int(c[1]),int(c[2]),int(c[3])
            if not (occ[r2['x0']:r2['x0']+r2['k'], r2['y0']:r2['y0']+r2['k']]>=0).any():
                r.update(d=r2['d'],x0=r2['x0'],y0=r2['y0'],score=float(c[0]),resolved=True)
                place(r,j); moved=True; break
        if not moved:
            r['overlap_unresolved']=True; conflicts+=1; place(r,j)
    print("unresolved overlaps:", conflicts)
    return recs

def render_check(img, recs):
    """draw solved footprints onto an upscaled minimap and tile-space map"""
    mmimg = img[1125:1434, 1941:2559].copy()
    sc=3
    big = cv2.resize(mmimg, None, fx=sc, fy=sc, interpolation=cv2.INTER_NEAREST)
    for r in recs:
        k=r['k']
        pts=[]
        for (rr,cc) in [(0,0),(k,0),(k,k),(0,k)]:
            x = (MBX-1941 + M_AX*((r['x0']+rr)+(r['y0']+cc)))*sc
            y = (MBY-1125 + M_AY*((r['x0']+rr)-(r['y0']+cc)))*sc
            pts.append([x,y])
        pts=np.array(pts,np.int32)
        col = (255,255,255) if not r.get('overlap_unresolved') else (0,0,255)
        cv2.polylines(big,[pts],True,col,1)
    cv2.imwrite('check_minimap.png', big)

if __name__=='__main__':
    img, mapmask = E.load()
    all_m = json.load(open('markers_fit.json'))
    good = np.load('good_mask.npy')
    recs = solve_markers(img, all_m, good)
    sc=np.array([r['score'] for r in recs])
    print("solved:", len(recs), "mean score", sc.mean().round(3), "low(<0.5):", (sc<0.5).sum())
    print("ambiguous-smoothed:", sum(1 for r in recs if r.get('smoothed')),
          " conflict-resolved:", sum(1 for r in recs if r.get('resolved')))
    json.dump(recs, open('marker_solution.json','w'))
    render_check(img, recs)
