import numpy as np, struct, matplotlib, importlib.util, sys
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

spec = importlib.util.spec_from_file_location("c", "caja_chapa_nh.py"); c = importlib.util.module_from_spec(spec); spec.loader.exec_module(c)
BOARD = sys.argv[1]

def load(p):
    b = open(p, "rb").read(); n = struct.unpack("<I", b[80:84])[0]
    return np.frombuffer(b[84:84 + n * 50], dtype=np.dtype([("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")]))["v"].reshape(-1, 3, 3).astype(float)

cu = load("stl/chapa_nh_cuerpo.stl"); ta = load("stl/chapa_nh_tapa.stl"); cp = load("stl/chapa_nh_contraplaca.stl")
bd = load(BOARD)
ta_b = ta.copy(); ta_b[..., 2] = c.OZ1 - ta_b[..., 2]
L = np.array([0.35, -0.45, 0.82]); L /= np.linalg.norm(L)

def draw(ax, t, col, alpha=1.0):
    n = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]); n /= np.linalg.norm(n, axis=1)[:, None] + 1e-9
    sh = 0.35 + 0.65 * np.abs(n @ L)
    rgb = np.array(matplotlib.colors.to_rgb(col))[None] * sh[:, None]
    ax.add_collection3d(Poly3DCollection(t, facecolors=np.c_[rgb, np.full(len(rgb), alpha)], edgecolors="none"))

def setup(ax, t, el, az, title):
    v = t.reshape(-1, 3); mn, mx = v.min(0), v.max(0); m = (mn + mx) / 2; r = (mx - mn).max() / 2
    ax.set_xlim(m[0] - r, m[0] + r); ax.set_ylim(m[1] - r, m[1] + r); ax.set_zlim(m[2] - r, m[2] + r)
    ax.view_init(el, az); ax.set_axis_off(); ax.set_box_aspect((1, 1, 1)); ax.set_title(title, fontsize=11)

fig = plt.figure(figsize=(15, 10), dpi=110)
ax = fig.add_subplot(2, 3, 1, projection="3d"); draw(ax, cu, "#2a9fb0"); setup(ax, cu, -60, -65, "Cuerpo: cara delantera")
ax = fig.add_subplot(2, 3, 2, projection="3d"); draw(ax, cu, "#2a9fb0"); setup(ax, cu, 60, -115, "Cuerpo: interior (visto desde atrás)")
ax = fig.add_subplot(2, 3, 3, projection="3d"); draw(ax, ta, "#e8a33a"); setup(ax, ta, 55, -60, "Tapa: cara interior, huecos de imanes")
ax = fig.add_subplot(2, 3, 4, projection="3d"); draw(ax, cu, "#2a9fb0"); draw(ax, bd, "#333333"); setup(ax, cu, 70, -100, "Placa colocada (sin tapa)")
ax = fig.add_subplot(2, 3, 5, projection="3d"); draw(ax, cp, "#e8a33a"); setup(ax, cp, 50, -60, "Contraplaca (por dentro de la ropa)")

# corte por x = centro del botón izquierdo
ax = fig.add_subplot(2, 3, 6); X = -9.29
for ops, col, zf in [(c.cuerpo(), "#2a9fb0", None), (c.tapa()[0], "#e8a33a", c.OZ1)]:
    xs, ys, zs, occ = c.solido(ops)
    i = np.searchsorted(xs, X) - 1
    for j in range(occ.shape[1]):
        for k in range(occ.shape[2]):
            if occ[i, j, k]:
                z0, z1 = zs[k], zs[k + 1]
                if zf is not None: z0, z1 = zf - z1, zf - z0
                ax.add_patch(plt.Rectangle((ys[j], z0), ys[j + 1] - ys[j], z1 - z0, color=col, lw=0))
r = np.random.default_rng(0).random((bd.shape[0], 60, 2)); m = r.sum(2) > 1; r[m] = 1 - r[m]
P = (bd[:, None, 0] + r[..., :1] * (bd[:, None, 1] - bd[:, None, 0]) + r[..., 1:] * (bd[:, None, 2] - bd[:, None, 0])).reshape(-1, 3)
s = np.abs(P[:, 0] - X) < 0.4
ax.scatter(P[s, 1], P[s, 2], s=0.4, c="#222")
ax.add_patch(plt.Rectangle((c.BAT_Y0, c.BAT_Z0), c.BAT_L, c.BAT_G, fill=False, ec="#c0392b", lw=1.2, ls="--"))
ax.text(c.BAT_Y0 + 12, c.BAT_Z0 + 3.5, "batería", color="#c0392b", fontsize=9)
ax.text(51, 4, "zona antena\n(libre)", fontsize=8, color="#555")
ax.set_aspect("equal"); ax.set_xlim(c.OY0 - 1, c.OY1 + 1); ax.set_ylim(c.OZ0 - 1, c.OZ1 + 1)
ax.set_title("Corte lateral por el botón (USB a la izquierda)", fontsize=11); ax.set_xlabel("mm"); ax.invert_yaxis()
plt.tight_layout(); plt.savefig("preview.png")
