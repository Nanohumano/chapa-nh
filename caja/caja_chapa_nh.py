"""
Caja Chapa NH — NANOHUMANO LABS
Genera los STL de la caja para LILYGO T-Display S3 + LiPo 802540 (800 mAh) + imanes.

Medidas de la placa sacadas del modelo oficial de LILYGO (dimensions/t-display-s3-full.stl):
  PCB 25.5 x 60.78 x 1.2 mm, pantalla 25.96 x 49.87, zona visible 22.7 x 42.7
  USB-C y botones BOOT/KEY en la cara de la pantalla, extremo y=0
  Conectores de batería e I2C detrás, en el extremo del USB (alto 3.5 mm)
  Antena en el extremo contrario (y ~ 50-61): sin batería ni imanes ahí.

Ejes: x = ancho, y = largo (y=0 borde del PCB del lado USB), z = grueso
      z = 0 cara trasera del PCB, z negativa hacia la pantalla.

Todo son cajas alineadas a los ejes: se combinan con un CSG exacto por
compresión de coordenadas y se exporta STL binario sin dependencias.
"""
import numpy as np, struct, sys, os

# ------------------------------------------------------------------ parámetros
HOLG = 0.30          # holgura alrededor de la placa
PARED = 1.6          # paredes laterales
FRENTE = 1.2         # grosor del marco delantero

# Placa (modelo oficial)
PCB_X = 12.75; PCB_Y1 = 60.78; PCB_Z0 = -1.2
LCD_X = 12.98; LCD_Y0 = 6.66; LCD_Y1 = 56.53; LCD_Z = -6.38
VIS_X = 11.35; VIS_Y0 = 12.18; VIS_Y1 = 54.90
USB = dict(x0=-4.61, x1=4.34, z0=-4.36, z1=-1.20)
BOTONES = [(-11.39, -7.19), (6.71, 10.91)]   # x de cada botón, y 0.53-3.99, techo z=-2.7
BOT_Y0, BOT_Y1, BOT_TECHO = 0.53, 3.99, -3.70   # techo real del pulsador
RST = dict(y0=8.01, y1=12.61, z0=0.0, z1=1.8)  # pulsador lateral en x+
COMP_ATRAS = 2.0      # componente más alto detrás, fuera de los conectores

# Batería 802540 (8 x 25 x 40) con margen
BAT_G, BAT_A, BAT_L = 8.3, 25.6, 40.8
BAT_Y0 = 7.8

# Imanes (disco); el hueco es cuadrado, vale para discos
IMAN_D, IMAN_G = float(os.environ.get("IMAN_D", 6.0)), float(os.environ.get("IMAN_G", 3.0))
PIEL = 1.0            # plástico entre imán y ropa
# 5 imanes: 2 en cada extremo + 1 en el centro (x, y). Van dentro de la tapa,
# así que pueden ir encima de la batería; ninguno en la zona de la antena (y > 50)
IMANES = [(-8.5, 4.0), (8.5, 4.0), (0.0, 25.5), (-8.5, 47.0), (8.5, 47.0)]
TAPA = PIEL + IMAN_G + 0.3   # grosor de la tapa: piel + hueco del imán

# ------------------------------------------------------------------ derivadas
IX = LCD_X + HOLG                      # medio ancho interior
IY0 = -1.35                            # interior lado USB (el USB queda dentro)
IY1 = PCB_Y1 + HOLG
IZ0 = LCD_Z                            # el cristal apoya en el marco
BAT_Z0 = COMP_ATRAS + 0.2
IZ1 = BAT_Z0 + BAT_G + 0.2             # cara interior de la tapa
OX, OY0, OY1 = IX + PARED, IY0 - PARED, IY1 + PARED
OZ0, OZ1 = IZ0 - FRENTE, IZ1 + TAPA

def B(x0, x1, y0, y1, z0, z1):
    return (min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1), min(z0, z1), max(z0, z1))

RADIO = 2.5          # radio de las esquinas exteriores

def esquinas(x0, x1, y0, y1, z0, z1, R=RADIO, n=25):
    """Quita el material de fuera de un cuarto de círculo en las 4 esquinas
    verticales (escalones de R/n mm, más finos que lo que imprime la boquilla)."""
    ops = []
    if R <= 0: return ops
    for k in range(n):
        ua, ub = R * k / n, R * (k + 1) / n
        um = (ua + ub) / 2
        v = R - (R * R - (R - um) ** 2) ** 0.5     # cuánto sobra en esta franja
        if v <= 1e-4: continue
        for sx, ex in ((1, x0), (-1, x1)):
            for sy, ey in ((1, y0), (-1, y1)):
                ops.append((0, B(ex + sx * ua, ex + sx * ub, ey - sy * 0.01, ey + sy * v, z0 - 1, z1 + 1)))
    return ops

# ------------------------------------------------------------------ CSG exacto
def solido(ops):
    xs, ys, zs = set(), set(), set()
    for s, b in ops:
        xs |= {b[0], b[1]}; ys |= {b[2], b[3]}; zs |= {b[4], b[5]}
    xs, ys, zs = [np.array(sorted({round(v, 4) for v in a})) for a in (xs, ys, zs)]
    cx, cy, cz = [(a[:-1] + a[1:]) / 2 for a in (xs, ys, zs)]
    occ = np.zeros((len(cx), len(cy), len(cz)), bool)
    for s, b in ops:
        mx = (cx > b[0]) & (cx < b[1]); my = (cy > b[2]) & (cy < b[3]); mz = (cz > b[4]) & (cz < b[5])
        occ[np.ix_(mx, my, mz)] = s
    return xs, ys, zs, occ

def triangulos(xs, ys, zs, occ):
    tris = []
    P = np.pad(occ, 1)
    for ax in range(3):
        d = np.diff(P.astype(np.int8), axis=ax)      # +1 entra, -1 sale
        idx = np.argwhere(d != 0)
        for i in idx:
            sgn = d[tuple(i)]
            c = list(i)
            # c[ax] es el índice de la cara (plano) en la lista de coordenadas
            # las otras dos coordenadas son índices de celda desplazados por el pad
            o = [a for a in range(3) if a != ax]
            cs = [xs, ys, zs]
            k = c[ax]
            u0, u1 = cs[o[0]][c[o[0]] - 1], cs[o[0]][c[o[0]]]
            v0, v1 = cs[o[1]][c[o[1]] - 1], cs[o[1]][c[o[1]]]
            w = cs[ax][k]
            def pt(u, v):
                p = [0, 0, 0]; p[ax] = w; p[o[0]] = u; p[o[1]] = v; return p
            a, b_, c_, d_ = pt(u0, v0), pt(u1, v0), pt(u1, v1), pt(u0, v1)
            # normal hacia fuera: sale del sólido en +ax si sgn == -1
            fuera_pos = (sgn == -1)
            # (o0,o1,ax) diestro si ax==0:(1,2) ax==1:(0,2)->(2,0) ax==2:(0,1)
            diestro = (ax != 1)
            if fuera_pos == diestro:
                tris += [(a, b_, c_), (a, c_, d_)]
            else:
                tris += [(a, c_, b_), (a, d_, c_)]
    return np.array(tris, float)

def stl(path, tris, nombre):
    with open(path, "wb") as f:
        f.write(nombre.encode()[:80].ljust(80, b" "))
        f.write(struct.pack("<I", len(tris)))
        for t in tris:
            n = np.cross(t[1] - t[0], t[2] - t[0]); n = n / (np.linalg.norm(n) or 1)
            f.write(struct.pack("<12fH", *n, *t[0], *t[1], *t[2], 0))

# ------------------------------------------------------------------ cuerpo
def cuerpo():
    ops = [(1, B(-OX, OX, OY0, OY1, OZ0, OZ1))]
    ops += [(0, B(-IX, IX, IY0, IY1, IZ0, OZ1 + 1))]                  # hueco
    # ventana de la pantalla (zona visible + 0.25)
    ops += [(0, B(-VIS_X - 0.25, VIS_X + 0.25, VIS_Y0 - 0.25, VIS_Y1 + 0.25, OZ0 - 1, IZ0 + 0.01))]
    # USB-C: hueco para el conector y la funda del cable
    ucx = (USB["x0"] + USB["x1"]) / 2; ucz = (USB["z0"] + USB["z1"]) / 2
    ops += [(0, B(ucx - 5.6, ucx + 5.6, OY0 - 1, IY0 + 0.01, ucz - 2.6, ucz + 2.6))]
    # RST lateral (x+)
    ry = (RST["y0"] + RST["y1"]) / 2
    ops += [(0, B(IX - 0.01, OX + 1, ry - 1.4, ry + 1.4, RST["z0"] - 0.3, RST["z1"] + 0.5))]
    # topes del borde del PCB en el lado USB
    for sx in (-1, 1):
        ops += [(1, B(sx * 12.2, sx * IX, IY0, -0.15, IZ0, 0.0))]
    # botones flexibles en el marco delantero: lengüeta + tetón que baja al pulsador
    for bx0, bx1 in BOTONES:
        s = -1 if bx0 < 0 else 1
        ext = s * 11.6                      # punta exterior de la lengüeta
        piv = s * 4.9                       # zona de giro (encima del USB)
        ty0, ty1 = -0.6, 4.0
        r = 0.5                             # ancho del corte
        ops += [(0, B(piv, ext + s * r, ty1, ty1 + r, OZ0 - 1, IZ0 + 0.01))]   # corte superior
        ops += [(0, B(piv, ext + s * r, ty0 - r, ty0, OZ0 - 1, IZ0 + 0.01))]   # corte inferior
        ops += [(0, B(ext, ext + s * r, ty0 - r, ty1 + r, OZ0 - 1, IZ0 + 0.01))]  # corte del extremo
        bcx = (bx0 + bx1) / 2; bcy = (BOT_Y0 + BOT_Y1) / 2
        ops += [(1, B(bcx - 1.1, bcx + 1.1, bcy - 1.1, bcy + 1.1, IZ0 - 0.01, BOT_TECHO - 0.3))]  # tetón
    # ranuras de cierre: 2 por lado largo, a media altura de la tapa (pared entera ahí)
    for sx in (-1, 1):
        for ya, yb in CIERRES_Y:
            ops += [(0, B(sx * (IX - 0.01), sx * (IX + 0.6), ya - 0.5, yb + 0.5, OZ1 - 3.1, OZ1 - 1.9))]
    # muesca para abrir la tapa con la uña (extremo de la antena)
    ops += [(0, B(-3.0, 3.0, IY1 - 0.01, OY1 + 1, OZ1 - 1.0, OZ1 + 1))]
    return ops

# ------------------------------------------------------------------ tapa trasera
CLT = 0.12
CIERRES_Y = [(10.0, 20.0), (38.0, 48.0)]
def tapa():
    # cara exterior en z=0 (posición de impresión); los huecos se abren hacia dentro
    W = IX - CLT
    ops = [(1, B(-W, W, IY0 + CLT, IY1 - CLT, 0, TAPA))]
    # pestañas de cierre con rampa: entra primero la cara interior (z alta)
    for sx in (-1, 1):
        for ya, yb in CIERRES_Y:
            for za, zb, sale in ((2.0, 2.4, 0.45), (2.4, 2.6, 0.33), (2.6, 2.8, 0.21), (2.8, 3.0, 0.09)):
                ops += [(1, B(sx * (W - 0.01), sx * (IX + sale), ya, yb, za, zb))]
    pa = IMAN_D + 0.4
    for xc, yc in IMANES:
        ops += [(0, B(xc - pa / 2, xc + pa / 2, yc - pa / 2, yc + pa / 2, PIEL, TAPA + 1))]
    # flecha hacia el USB en la cara interior
    ops += [(0, B(-1.0, 1.0, 12, 20, TAPA - 0.4, TAPA + 1))]
    ops += [(0, B(-2.5, 2.5, 10, 12, TAPA - 0.4, TAPA + 1))]
    return ops, TAPA

def contraplaca():
    # misma posición de imanes que la tapa (vista en espejo al ponerla enfrente)
    pa = IMAN_D + 0.4; G = IMAN_G + 0.3 + PIEL; m = 2.2
    xs = [x for x, _ in IMANES]; ys = [y for _, y in IMANES]
    x0, x1 = min(xs) - pa / 2 - m, max(xs) + pa / 2 + m
    y0, y1 = min(ys) - pa / 2 - m, max(ys) + pa / 2 + m
    ops = [(1, B(x0, x1, y0, y1, 0, G))]
    # aligerado: ventanas entre imanes
    ops += [(0, B(x0 + 3.5, -pa / 2 - 1.5, 4.0 + pa / 2 + 1.5, 47.0 - pa / 2 - 1.5, -1, G + 1))]
    ops += [(0, B(pa / 2 + 1.5, x1 - 3.5, 4.0 + pa / 2 + 1.5, 47.0 - pa / 2 - 1.5, -1, G + 1))]
    for xc, yc in IMANES:
        ops += [(0, B(xc - pa / 2, xc + pa / 2, yc - pa / 2, yc + pa / 2, PIEL, G + 1))]
    return ops, (x1 - x0, y1 - y0, G)

# ------------------------------------------------------------------ redondeos
R_CANTO = 1.2      # cantos exteriores (frontal y trasero)
R_VENT = 1.0       # canto de la ventana de la pantalla
R_VENT_ESQ = 1.0   # esquinas de la ventana

def _anillo(cx0, cx1, cy0, cy1, r, nc):
    """Rectángulo redondeado (centros de esquina fijos) en sentido antihorario."""
    pts = []
    for (cx, cy, a0) in ((cx1, cy0, -90), (cx1, cy1, 0), (cx0, cy1, 90), (cx0, cy0, 180)):
        for k in range(nc + 1):
            a = np.radians(a0 + 90 * k / nc)
            pts.append((cx + r * np.cos(a), cy + r * np.sin(a)))
    return np.array(pts)

def _perfil(z0, z1, rf, rb, n):
    """(z, d): d = cuánto entra la superficie respecto al borde recto."""
    def f(t, r): return r - np.sqrt(max(r * r - (r - t) ** 2, 0))
    pf = [(z0 + rf * k / n, f(rf * k / n, rf)) for k in range(n + 1)]
    pb = [(z1 - rb * k / n, f(rb * k / n, rb)) for k in range(n, -1, -1)]
    return pf + pb

def barrido(x0, x1, y0, y1, R, perfil, nc=10):
    """Sólido cerrado: rectángulo redondeado de radio R cuyo contorno se desplaza
    hacia dentro d (o hacia fuera si d<0) a cada altura z del perfil."""
    cx0, cx1, cy0, cy1 = x0 + R, x1 - R, y0 + R, y1 - R
    anillos = [np.c_[_anillo(cx0, cx1, cy0, cy1, max(R - d, 0.01), nc), np.full(4 * (nc + 1), z)] for z, d in perfil]
    tris = []
    m = len(anillos[0])
    for a, b in zip(anillos[:-1], anillos[1:]):
        for i in range(m):
            j = (i + 1) % m
            tris += [(a[i], a[j], b[j]), (a[i], b[j], b[i])]
    c0 = anillos[0].mean(0); c1 = anillos[-1].mean(0)
    for i in range(m):
        j = (i + 1) % m
        tris += [(c0, anillos[0][j], anillos[0][i]), (c1, anillos[-1][i], anillos[-1][j])]
    return np.array(tris, float)

def redondear_cuerpo(tris):
    import csg
    piel = barrido(-OX, OX, OY0, OY1, RADIO, _perfil(OZ0, OZ1, R_CANTO, R_CANTO, 8))
    A = csg.intersect(csg.from_tris(tris), csg.from_tris(piel))
    # canto redondeado de la ventana: cortador que se abre hacia la cara frontal
    vx, vy0, vy1 = VIS_X + 0.25, VIS_Y0 - 0.25, VIS_Y1 + 0.25
    def g(t, r): return r - np.sqrt(max(r * r - (r - t) ** 2, 0))
    perfil = [(OZ0 - 1.0, -R_VENT)] + [(OZ0 + R_VENT * k / 8, -g(R_VENT * k / 8, R_VENT)) for k in range(9)] + [(IZ0 + 0.5, 0.0)]
    cort = barrido(-vx, vx, vy0, vy1, R_VENT_ESQ, perfil)
    A = csg.subtract(A, csg.from_tris(cort))
    return csg.reparar_t(csg.to_tris(A))

def redondear_contraplaca(tris, x0, x1, y0, y1, G):
    import csg
    piel = barrido(x0, x1, y0, y1, RADIO, _perfil(0, G, R_CANTO, R_CANTO, 8))
    return csg.reparar_t(csg.to_tris(csg.intersect(csg.from_tris(tris), csg.from_tris(piel))))

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    c = redondear_cuerpo(triangulos(*solido(cuerpo())))
    stl(f"{out}/chapa_nh_cuerpo.stl", c, "Chapa NH cuerpo")
    t_ops, ab = tapa()
    t = triangulos(*solido(t_ops))
    stl(f"{out}/chapa_nh_tapa.stl", t, "Chapa NH tapa")
    p_ops, dims = contraplaca()
    xs_, ys_, zs_, _ = solido(p_ops)
    p = redondear_contraplaca(triangulos(*solido(p_ops)), xs_[0], xs_[-1], ys_[0], ys_[-1], zs_[-1] - 1)
    stl(f"{out}/chapa_nh_contraplaca.stl", p, "Chapa NH contraplaca")
    print("Exterior caja: %.1f x %.1f x %.1f mm" % (2 * OX, OY1 - OY0, OZ1 - OZ0))
    print("Interior:      %.1f x %.1f x %.1f mm" % (2 * IX, IY1 - IY0, IZ1 - IZ0))
    print("Hueco bateria: %.1f mm de alto (bat %.1f)  y %.1f-%.1f (antena libre desde %.1f)" %
          (IZ1 - BAT_Z0, BAT_G, BAT_Y0, BAT_Y0 + BAT_L, BAT_Y0 + BAT_L))
    print("Tapa: %.1f mm de grueso, 5 imanes" % ab)
    print("Contraplaca: %.1f x %.1f x %.1f mm" % dims)
    print("Triangulos:", len(c), len(t), len(p))
