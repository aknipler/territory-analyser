"""Solve absolute tile coords for markers using minimap cross-reference."""
import cv2, numpy as np, json
import extract as E

N = 168
AX, AY = E.AX, E.AY

def minimap_masks(img):
    mm_colors = {
        1: [(241,42,27),(169,48,36)],
        2: [(28,28,236),(1,1,183)],
        3: [(18,221,15),(0,165,0)],
        4: [(12,229,229),(0,202,202)],
    }
    masks = {}
    for p, cols in mm_colors.items():
        acc = np.zeros(img.shape[:2], bool)
        for c in cols:
            d = np.abs(img.astype(int)-np.array(c,int)).sum(axis=2)
            acc |= (d < 90)
        masks[p] = acc
    return masks

def main():
    img, mapmask = E.load()
    all_m = json.load(open('markers_fit.json'))
    good = np.load('good_mask.npy')

    sxT = np.array([m['top'][0] for m in all_m])
    syT = np.array([m['top'][1] for m in all_m])
    ks  = np.array([m['k'] for m in all_m])
    ps  = np.array([m['p'] for m in all_m])

    # --- anchor X absolutely: lattice phase + map left corner at sx=26 ---
    phx = E.solve_phase(sxT[good], AX)
    n0 = round((26.0 - phx)/AX)
    BX = phx + AX*n0 - AX*n0  # BX = phx shifted so that corner index = 0
    # s integer for each marker: S = round((sx - phx)/AX) - n0... define base so corner=0
    S = np.round((sxT - phx)/AX).astype(int) - n0
    print("anchor: phx", round(phx,3), "n0", n0, " S range:", S[good].min(), S[good].max(), "(expect within 0..336)")

    # --- relative d lattice ---
    phy = E.solve_phase(syT[good], AY)
    Dfloat = (syT - phy)/AY     # relative units, unknown global integer offset g: d_true = Drel + g + e
    Drel = np.round(Dfloat).astype(int)
    resid = Dfloat - Drel
    print("d resid std:", resid[good].std())

    # parity of S vs Drel: d_true must satisfy d ≡ S (mod 2). With global offset g,
    # parity(Drel + g) == parity(S) for elevation-even markers.
    par = (S - Drel) % 2
    print("parity counts (g even fits the majority?):", np.bincount(par[good]))

    # --- minimap matching ---
    mm = minimap_masks(img)
    # minimap transform: sx = mbx + max*(r+c), sy = mby + may*(r-c); calibrate offsets by search
    m_ax, m_ay = 1.8363, 0.9167
    best = None
    for mbx in np.arange(1939.0, 1945.01, 0.5):
        for mby in np.arange(1276.0, 1282.01, 0.5):
            total = 0
            for i in np.nonzero(good)[0]:
                k = ks[i]; p = ps[i]
                s_corner = S[i]            # = x0+y0+k
                # candidate d_true for top corner over offsets
                cands = [Drel[i] + g for g in range(-2, 10)]
                cands = [d for d in cands if (d - S[i]) % 2 == 0]
                bestsc = 0
                for d in cands:
                    x0 = (s_corner - k + d + k)//2   # x0 = (S - k + d + k)/2 = (S+d)/2
                    y0 = (s_corner - k - (d + k))//2
                    # sample footprint tile centers on minimap
                    hits = 0; tot = 0
                    for rr in range(k):
                        for cc in range(k):
                            r = x0+rr+0.5; c = y0+cc+0.5
                            xx = int(round(mbx + m_ax*(r+c)))
                            yy = int(round(mby + m_ay*(r-c)))
                            tot += 1
                            if 0 <= yy < mm[p].shape[0] and 0 <= xx < mm[p].shape[1] and mm[p][yy,xx]:
                                hits += 1
                    bestsc = max(bestsc, hits/tot)
                total += bestsc
            if best is None or total > best[0]:
                best = (total, mbx, mby)
    print("mm calib best:", best)

if __name__ == '__main__':
    main()
