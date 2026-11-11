import numpy as np, struct, matplotlib, importlib.util, sys
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
spec = importlib.util.spec_from_file_location("c", "caja_chapa_nh.py"); c = importlib.util.module_from_spec(spec); spec.loader.exec_module(c)
def load(p):
    b = open(p, "rb").read(); n = struct.unpack("<I", b[80:84])[0]
    return np.frombuffer(b[84:84 + n * 50], dtype=np.dtype([("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")]))["v"].reshape(-1, 3, 3).astype(float)
cu = load("v7/chapa_nh_cuerpo.stl"); ta = load("v7/chapa_nh_tapa.stl"); pie = load("v7/chapa_nh_pie.stl")
bd = load(sys.argv[1])
ta_b = ta.copy(); ta_b[..., 2] = c.OZ1 - ta_b[..., 2]
bat = c.triangulos(*c.solido([(1, c.B(c.BX0 + 0.2, c.BX1 - 0.2, 10.0, 50.0, c.IZ0, c.IZ0 + 10.0))]))
L = np.array([0.35, -0.45, 0.82]); L /= np.linalg.norm(L)
def draw(ax, t, col, alpha=1.0):
    n = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]); n /= np.linalg.norm(n, axis=1)[:, None] + 1e-9
    sh = 0.35 + 0.65 * np.abs(n @ L)
    rgb = np.array(matplotlib.colors.to_rgb(col))[None] * sh[:, None]
    ax.add_collection3d(Poly3DCollection(t, facecolors=np.c_[rgb, np.full(len(rgb), alpha)], edgecolors="none"))
def setup(ax, t, el, az, title):
    v = t.reshape(-1, 3); mn, mx = v.min(0), v.max(0); m = (mn + mx) / 2; r = (mx - mn).max() / 2
    ax.set_xlim(m[0] - r, m[0] + r); ax.set_ylim(m[1] - r, m[1] + r); ax.set_zlim(m[2] - r, m[2] + r)
    ax.view_init(el, az); ax.set_axis_off(); ax.set_box_aspect((1, 1, 1)); ax.set_title(title, fontsize=12)
# chapa sobre el pie
a = np.radians(c.PIE_ANG); ca, sa = np.cos(a), np.sin(a); z0 = 3.0 + 3.0 * ca + 0.2
def al_pie(t):
    u = -t[..., 1]; v = c.XO1 - t[..., 0]; w = t[..., 2] - c.OZ1
    return np.stack([u, w * ca + v * sa, v * ca - w * sa + z0], -1)
fig = plt.figure(figsize=(16, 10), dpi=100)
ax = fig.add_subplot(2, 3, 1, projection="3d"); draw(ax, cu, "#2b2b2b"); draw(ax, bd[bd[..., 2].min(1) < -5.5], "#1d3557")
setup(ax, cu, -90, 180, "Cara delantera: pantalla arriba, batería abajo")
ax = fig.add_subplot(2, 3, 2, projection="3d"); draw(ax, cu, "#2a9fb0"); draw(ax, bat, "#c0392b"); draw(ax, bd, "#333")
setup(ax, cu, 65, -100, "Por dentro: placa y batería en plano (rojo)")
ax = fig.add_subplot(2, 3, 3, projection="3d"); draw(ax, ta, "#e8a33a"); setup(ax, ta, 55, -60, "Tapa corredera: 5 imanes")
# corte de grosor
ax = fig.add_subplot(2, 3, 4)
for ops, col, zf in [(c.cuerpo(), "#2a9fb0", None), (c.tapa()[0], "#e8a33a", c.OZ1)]:
    xs, ys, zs, occ = c.solido(ops); j = np.searchsorted(ys, 30.0) - 1
    for i in range(occ.shape[0]):
        for k in range(occ.shape[2]):
            if occ[i, j, k]:
                z0_, z1_ = zs[k], zs[k + 1]
                if zf is not None: z0_, z1_ = zf - z1_, zf - z0_
                ax.add_patch(plt.Rectangle((xs[i], z0_), xs[i + 1] - xs[i], z1_ - z0_, color=col, lw=0))
ax.add_patch(plt.Rectangle((c.BX0 + 0.2, c.IZ0), c.BAT_A - 0.4, 10.0, fill=False, ec="#c0392b", lw=1.5, ls="--"))
ax.text(c.BX0 + 6, c.IZ0 + 4.5, "batería 1200 mAh", color="#c0392b", fontsize=9)
ax.add_patch(plt.Rectangle((-c.PCB_X, c.LCD_Z), 2 * c.PCB_X, -c.LCD_Z + 2.0, fill=False, ec="#222", lw=1.2, ls="--"))
ax.text(-9, c.LCD_Z + 4.5, "placa + pantalla", fontsize=9)
ax.set_aspect("equal"); ax.set_xlim(c.XO0 - 2, c.XO1 + 2); ax.set_ylim(c.OZ0 - 2, c.OZ1 + 3); ax.invert_yaxis()
ax.set_title("Corte: %.1f mm de grosor (antes 22,6)" % (c.OZ1 - c.OZ0), fontsize=12); ax.set_xlabel("mm")
ax = fig.add_subplot(2, 3, 5, projection="3d"); draw(ax, pie, "#e8a33a"); setup(ax, pie, 25, 50, "Pie de mostrador (imanes por detrás)")
ax = fig.add_subplot(2, 3, 6, projection="3d"); draw(ax, pie, "#e8a33a")
ch = np.concatenate([al_pie(cu), al_pie(ta_b)]); draw(ax, ch, "#2b2b2b")
draw(ax, al_pie(bd[bd[..., 2].min(1) < -5.5]), "#1d3557"); setup(ax, np.concatenate([ch, pie]), 15, -70, "Chapa sobre el pie")
fig.suptitle("NH-04 Chapa v7 · %.1f x %.1f x %.1f mm" % (c.OY1 - c.OY0, c.XO1 - c.XO0, c.OZ1 - c.OZ0), fontsize=15)
plt.tight_layout(); plt.savefig("preview_v7.png")
