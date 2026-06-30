"""Territory-analyser test-case extraction from AoE2 DE screenshot with marker mod."""
import cv2, numpy as np, json, os

# Screenshot location. Override with TA_IMG env var; defaults to the bundled copy
# one directory up from ta_work (handoff_bundle/screenshot_1000029911.png).
_DEFAULT_IMG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                            'screenshot_1000029911.png')
IMG_PATH = os.environ.get('TA_IMG', _DEFAULT_IMG)
N = 168                      # map size (tiles)

# main-map lattice (sx = BX + AX*(r+c); sy = BY + AY*(r-c) - AY*elev)
AX = 7.4832                  # px per unit of (r+c), tile corners
AY = AX/2
BX = None                    # solved below
BY = None

PLAYER_COLORS = {1:(204,0,0), 2:(0,0,204), 3:(16,132,3), 4:(24,172,172)}  # BGR

def load():
    img = cv2.imread(IMG_PATH, cv2.IMREAD_COLOR)
    H,W = img.shape[:2]
    bg = img[0,0].astype(int)
    diff = np.abs(img.astype(int)-bg).sum(axis=2)
    mask = (diff>30).astype(np.uint8)
    mask[1100:,1900:] = 0; mask[1200:,:250] = 0
    n,lab,stats,cent = cv2.connectedComponentsWithStats(mask,8)
    i = 1+np.argmax(stats[1:,4])
    mapmask = (lab==i).astype(np.uint8)
    mapmask = cv2.morphologyEx(mapmask, cv2.MORPH_CLOSE, np.ones((9,9),np.uint8))
    return img, mapmask

def fill_holes(mask):
    H,W = mask.shape
    inv = ((1-mask)*255).astype(np.uint8)
    hm = np.zeros((H+2,W+2),np.uint8)
    ff = inv.copy(); cv2.floodFill(ff,hm,(0,0),0)
    return (mask|(ff>0)).astype(np.uint8)

def marker_components(img, mapmask, color, tol=70, min_area=40):
    c = np.array(color,int)
    d = np.abs(img.astype(int)-c).sum(axis=2)
    mask = ((d<tol)&(mapmask>0)).astype(np.uint8)
    filled = fill_holes(mask)
    n,lab,stats,cent = cv2.connectedComponentsWithStats(filled,8)
    comps=[]
    for i in range(1,n):
        x,y,w,h,a = stats[i]
        if a < min_area: continue
        comps.append((i,(x,y,w,h),a))
    return lab, comps

def diamond_fit(lab, idx, bbox):
    """Fit diamond via u=sx+2*sy, v=sx-2*sy extremes. Top edges (u_min, v_max)
    are rim-free; left corner from u_min & v_min; top corner from u_min & v_max."""
    x,y,w,h = bbox
    ys,xs = np.nonzero(lab[y:y+h, x:x+w]==idx)
    xs = xs+x; ys = ys+y
    u = xs + 2.0*ys; v = xs - 2.0*ys
    u_min = np.percentile(u, 1); u_max = np.percentile(u, 99)
    v_min = np.percentile(v, 1); v_max = np.percentile(v, 99)
    top  = ((u_min+v_max)/2, (u_min-v_max)/4)
    left = ((u_min+v_min)/2, (u_min-v_min)/4)
    bot  = ((u_max+v_min)/2, (u_max-v_min)/4)
    right= ((u_max+v_max)/2, (u_max-v_max)/4)
    width = (right[0]-left[0])
    height_noRim = (v_max-v_min)/2/2  # not used
    return dict(top=top,left=left,bot=bot,right=right,width=float(width),
                npx=len(xs))

def solve_phase(values, period):
    ph = np.angle(np.exp(1j*2*np.pi*np.asarray(values)/period))
    mean = np.angle(np.exp(1j*ph).mean())
    return mean/(2*np.pi)*period

if __name__ == '__main__':
    img, mapmask = load()
    all_m = []
    for p,col in PLAYER_COLORS.items():
        lab, comps = marker_components(img, mapmask, col)
        for idx,bbox,a in comps:
            f = diamond_fit(lab, idx, bbox)
            f['p']=p; f['bbox']=bbox; f['area']=a
            all_m.append(f)
    print("markers:", len(all_m))
    # footprint size from diamond width: width ~= k*2*AX - inset
    wpx = np.array([m['width'] for m in all_m])
    ks = wpx/(2*AX)
    print("k float distribution:")
    hist,edges = np.histogram(ks, bins=48, range=(0.5,6.5))
    for hh,e in zip(hist,edges):
        if hh: print(f"  {e:.2f}: {'#'*hh}")
    json.dump([{k:(v if not isinstance(v,tuple) else list(v)) for k,v in m.items()} for m in all_m],
              open('markers_fit.json','w'))
