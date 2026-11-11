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

# Batería 103040 (10 x 30 x 40, 1200 mAh) con margen. v7: va AL LADO de la placa (lado x-),
# en plano, no detrás: la chapa queda más alta pero mucho más fina.
BAT_G, BAT_A, BAT_L = 10.3, 30.6, 40.8   # 103040 de 1200 mAh (10 x 30 x 40)
SEP = 1.2             # tabique entre placa y batería (solo hasta la cara trasera del PCB)

# Imanes (disco); el hueco es cuadrado, vale para discos
IMAN_D, IMAN_G = float(os.environ.get("IMAN_D", 6.0)), float(os.environ.get("IMAN_G", 3.0))
PIEL = 1.0            # plástico entre imán y ropa
# 5 imanes: 2 en cada extremo + 1 en el centro (x, y). Van dentro de la tapa,
# así que pueden ir encima de la batería; ninguno en la zona de la antena (y > 50)
TAPA = PIEL + IMAN_G + 0.3   # grosor de la tapa: piel + hueco del imán

# ------------------------------------------------------------------ derivadas
IX = LCD_X + HOLG                      # medio ancho interior
IY0 = -1.35                            # interior lado USB (el USB queda dentro)
IY1 = PCB_Y1 + HOLG
IZ0 = LCD_Z                            # el cristal apoya en el marco
# La batería va en el lado x+ (el del RST): con la pantalla arriba y la batería
# abajo, el USB queda a la derecha mirando la chapa de frente.
BX0, BX1 = IX + SEP, IX + SEP + BAT_A   # hueco de la batería
XI0, XI1 = -IX, BX1                     # interior completo
IZ1 = max(COMP_ATRAS + 0.3, IZ0 + BAT_G + 0.4)   # cara interior de la tapa
OX, OY0, OY1 = IX + PARED, IY0 - PARED, IY1 + PARED
XO0, XO1 = XI0 - PARED, XI1 + PARED
OZ0, OZ1 = IZ0 - FRENTE, IZ1 + TAPA
# 5 imanes: 2 en cada extremo + 1 en el centro, ninguno sobre la antena (placa, y > 50)
_xl, _xr = XI0 + 4.8, XI1 - 5.5
IMANES = [(_xl, 4.0), (_xr, 4.0), ((_xl + _xr) / 2, 25.5), (_xl, 47.0), (_xr, 47.0)]

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
    ops = [(1, B(XO0, XO1, OY0, OY1, OZ0, OZ1))]
    ops += [(0, B(-IX, IX, IY0, IY1, IZ0, OZ1 + 1))]                  # hueco de la placa
    ops += [(0, B(BX0, BX1, IY0, IY1, IZ0, OZ1 + 1))]           # hueco de la batería
    ops += [(0, B(XI0, XI1, IY0, IY1, PCB_Z0, OZ1 + 1))]               # paso del cable por detrás
    # ventana de la pantalla (zona visible + 0.25)
    ops += [(0, B(-VIS_X - 0.25, VIS_X + 0.25, VIS_Y0 - 0.25, VIS_Y1 + 0.25, OZ0 - 1, IZ0 + 0.01))]
    # USB-C: hueco para el conector y la funda del cable
    ucx = (USB["x0"] + USB["x1"]) / 2; ucz = (USB["z0"] + USB["z1"]) / 2
    ops += [(0, B(ucx - 5.6, ucx + 5.6, OY0 - 1, IY0 + 0.01, ucz - 2.6, ucz + 2.6))]
    # RST lateral (x+)
    # (el RST lateral queda hacia el hueco de la batería: se pulsa con la tapa abierta)
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
    # tapa corredera: carriles en las paredes largas y extremo de la antena abierto
    for sx, w in ((1, XI1), (-1, XI0)):
        ops += [(0, B(w - sx * 0.01, w + sx * RAIL_G, IY0, OY1 + 1, OZ1 - RAIL_Z1, OZ1 - RAIL_Z0))]
        # hueco del "clic" final (la tapa queda enganchada al terminar de entrar)
        ops += [(0, B(w - sx * 0.01, w + sx * (RAIL_G + 0.25), IY1 - 4.6, IY1 - 1.4, OZ1 - RAIL_Z1, OZ1 - RAIL_Z0))]
    ops += [(0, B(XI0, XI1, IY1 - 0.01, OY1 + 1, OZ1 - TAPA - 0.05, OZ1 + 1))]
    return ops

# ------------------------------------------------------------------ tapa trasera
CLT = 0.12
RAIL_Z0, RAIL_Z1 = 1.6, 3.0     # carril, medido desde la cara exterior de la tapa
RAIL_G = 0.8                    # profundidad del carril en la pared
def tapa():
    # cara exterior en z=0 (posición de impresión); los huecos se abren hacia dentro
    Y1 = OY1 - 0.05                                   # la tapa cierra también el extremo de la antena
    ops = [(1, B(XI0 + CLT, XI1 - CLT, IY0 + CLT, Y1, 0, TAPA))]
    for sx, w in ((1, XI1), (-1, XI0)):
        # lengüetas que corren por los carriles (holgura 0,15 arriba y abajo, 0,18 al fondo)
        ops += [(1, B(w - sx * (CLT + 0.01), w + sx * (RAIL_G - 0.18), IY0 + CLT + 0.5, Y1, RAIL_Z0 + 0.15, RAIL_Z1 - 0.15))]
        # topes del clic
        ops += [(1, B(w - sx * (CLT + 0.01), w + sx * (RAIL_G + 0.18), IY1 - 4.0, IY1 - 2.0, RAIL_Z0 + 0.15, RAIL_Z1 - 0.15))]
    cxt = (XI0 + XI1) / 2
    # ranura para la uña en la cara exterior, junto al extremo
    ops += [(0, B(cxt - 4.0, cxt + 4.0, Y1 - 3.2, Y1 - 2.0, -1, 0.6))]
    pa = IMAN_D + 0.4
    for xc, yc in IMANES:
        ops += [(0, B(xc - pa / 2, xc + pa / 2, yc - pa / 2, yc + pa / 2, PIEL, TAPA + 1))]
    # flecha hacia el USB en la cara interior
    ops += [(0, B(cxt - 1.0, cxt + 1.0, 12, 20, TAPA - 0.4, TAPA + 1))]
    ops += [(0, B(cxt - 2.5, cxt + 2.5, 10, 12, TAPA - 0.4, TAPA + 1))]
    return ops, TAPA

def contraplaca():
    # misma posición de imanes que la tapa (vista en espejo al ponerla enfrente)
    pa = IMAN_D + 0.4; G = IMAN_G + 0.3 + PIEL; m = 2.2
    xs = [x for x, _ in IMANES]; ys = [y for _, y in IMANES]
    x0, x1 = min(xs) - pa / 2 - m, max(xs) + pa / 2 + m
    y0, y1 = min(ys) - pa / 2 - m, max(ys) + pa / 2 + m
    ops = [(1, B(x0, x1, y0, y1, 0, G))]
    # aligerado: ventanas entre imanes
    xc = IMANES[2][0]
    ops += [(0, B(x0 + 3.5, xc - pa / 2 - 1.5, 4.0 + pa / 2 + 1.5, 47.0 - pa / 2 - 1.5, -1, G + 1))]
    ops += [(0, B(xc + pa / 2 + 1.5, x1 - 3.5, 4.0 + pa / 2 + 1.5, 47.0 - pa / 2 - 1.5, -1, G + 1))]
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
    piel = barrido(XO0, XO1, OY0, OY1, RADIO, _perfil(OZ0, OZ1, R_CANTO, R_CANTO, 8))
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

if __name__ == "__main__" and "--pie" not in sys.argv:
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
    print("Exterior caja: %.1f x %.1f x %.1f mm" % (XO1 - XO0, OY1 - OY0, OZ1 - OZ0))
    print("Hueco bateria: %.1f x %.1f x %.1f mm (bat %.1f x %.1f x %.1f)" %
          (BX1 - BX0, IY1 - IY0, IZ1 - IZ0, BAT_A, BAT_L, BAT_G))
    print("Imanes:", [(round(x, 1), y) for x, y in IMANES])
    print("Tapa: %.1f mm de grueso, 5 imanes" % ab)
    print("Contraplaca: %.1f x %.1f x %.1f mm" % dims)
    print("Triangulos:", len(c), len(t), len(p))

# ------------------------------------------------------------------ pie de mostrador
PIE_ANG = 15.0      # inclinación hacia atrás
def pie():
    """Soporte de mostrador: placa inclinada con 5 imanes (misma posición que la tapa),
    repisa abajo y base con nervios. Ejes: X = largo de la chapa, Y = hacia atrás, Z = arriba.
    La chapa se apoya con la batería abajo: punto (x, y) de la tapa -> (X = -y, altura = XO1 - x)."""
    import csg
    G = IMAN_G + 0.3 + PIEL; pa = IMAN_D + 0.4; H = XO1 - XO0
    U0, U1 = -OY1, -OY0
    a = np.radians(PIE_ANG); ca, sa = np.cos(a), np.sin(a)
    BASE = 3.0; REP = 3.0
    def malla(ops): return triangulos(*solido(ops))
    def gira(t, z0):
        t = t.copy(); u, v, w = t[..., 0].copy(), t[..., 1].copy(), t[..., 2].copy()
        t[..., 0] = u; t[..., 1] = w * ca + v * sa; t[..., 2] = v * ca - w * sa + z0
        return t[:, [0, 2, 1]]          # el cambio de ejes es una simetría: se invierte el giro de los triángulos
    z0 = BASE + REP * ca + 0.2
    # placa (u, v, w): w = 0 cara delantera (donde se pega la chapa)
    ops = [(1, B(U0, U1, -REP, H, 0, G))]
    ops += [(1, B(U0, U1, -REP, 0, -9.0, G))]                     # repisa donde apoya la chapa
    for xc, yc in IMANES:
        ops += [(0, B(-yc - pa / 2, -yc + pa / 2, XO1 - xc - pa / 2, XO1 - xc + pa / 2, PIEL, G + 1))]
    placa = csg.from_tris(gira(malla(ops), z0))
    delante_t = gira(malla([(1, B(U0 - 5, U1 + 5, -20, H + 20, -60, G - 0.5))]), z0)
    A = placa
    A = csg.union(A, csg.from_tris(malla([(1, B(U0, U1, -10.0, 32.0, 0, BASE))])))
    for r0 in (U0, -36.0 - 1.5, U1 - 3.0):                        # nervios fuera de los imanes
        rib = csg.from_tris(malla([(1, B(r0, r0 + 3.0, -5.0, 32.0, 0.5, 34.0))]))
        A = csg.union(A, csg.subtract(rib, csg.from_tris(delante_t)))  # csg invierte en sitio: copia nueva
    suelo = csg.from_tris(malla([(1, B(U0 - 20, U1 + 20, -40, 60, 0, 120))]))
    return csg.reparar_t(csg.to_tris(csg.intersect(A, suelo)))


def _casco(puntos):
    """Envolvente convexa -> triángulos con las normales hacia fuera."""
    from scipy.spatial import ConvexHull
    P = np.asarray(puntos, float); h = ConvexHull(P); c = P.mean(0); T = []
    for f in h.simplices:
        t = P[f]
        if np.dot(np.cross(t[1] - t[0], t[2] - t[0]), t.mean(0) - c) < 0: t = t[[0, 2, 1]]
        T.append(t)
    return np.array(T)

def pie_lowpoly():
    """Pie de mostrador facetado (low poly). La cara delantera es plana e inclinada PIE_ANG;
    los imanes van enrasados en ella (se pegan desde delante) para que la chapa agarre fuerte."""
    import csg
    pa = IMAN_D + 0.4; H = XO1 - XO0
    U0, U1 = -OY1, -OY0; L = U1 - U0
    a = np.radians(PIE_ANG); ca, sa = np.cos(a), np.sin(a)
    REP = 3.0
    z0 = REP * ca + 0.6
    def W(u, v, w):                       # (u, v, w) de la cara delantera -> mundo
        return (u, w * ca + v * sa, v * ca - w * sa + z0)
    ch = 9.0                              # chaflanes de las esquinas superiores
    E = 5.0                               # grueso de la placa
    # placa frontal biselada, con esquinas achaflanadas de forma asimétrica
    cara = [(U0, -REP), (U1, -REP), (U1, H - ch * 0.7), (U1 - ch * 1.3, H), (U0 + ch, H), (U0, H - ch)]
    cm = (np.mean([q[0] for q in cara]), np.mean([q[1] for q in cara]))
    trasera = [(u + (cm[0] - u) * 0.04, v + (cm[1] - v) * 0.05) for u, v in cara]   # bisel de ~1,5 mm
    placa = csg.from_tris(_casco([W(u, v, 0) for u, v in cara] + [W(u, v, E) for u, v in trasera]))
    # bloque trasero facetado, como una roca, que sujeta la placa hasta media altura
    hv = 0.52 * H
    P = [W(U0 + 3, hv - 4, E - 1), W(U0 + 0.4 * L, hv + 3, E - 1), W(U1 - 0.25 * L, hv - 1, E - 1), W(U1 - 2, hv - 7, E - 1),
         W(U0 + 1, 0, E - 1), W(U1 - 1, 0, E - 1)]
    P += [(U0 + 1.5, 27, 0), (U0 + 0.3 * L, 33, 0), (U0 + 0.62 * L, 35, 0), (U1 - 2, 29, 0),
          (U0, 12, 0), (U1, 10, 0)]
    P += [(U0 + 3, 21, 12), (U0 + 0.55 * L, 27, 15), (U1 - 4, 19, 10)]
    cuerpo = csg.union(placa, csg.from_tris(_casco(P)))
    # repisa donde apoya la chapa, con el frente biselado
    R = [W(u, v, w) for u in (U0 + 1.5, U1 - 1.5) for v, w in ((0, 0), (0, -9.5), (-REP, -7.0), (-REP, 0))]
    A = csg.union(cuerpo, csg.from_tris(_casco(R)))
    # huecos de los imanes, abiertos por delante (el imán queda enrasado con la cara)
    for xc, yc in IMANES:
        u, v = -yc, XO1 - xc
        caja = [W(u + du, v + dv, w) for du in (-pa / 2, pa / 2) for dv in (-pa / 2, pa / 2) for w in (-1, IMAN_G + 0.3)]
        A = csg.subtract(A, csg.from_tris(_casco(caja)))
    suelo = csg.from_tris(triangulos(*solido([(1, B(U0 - 20, U1 + 20, -40, 80, 0, 140))])))
    return csg.reparar_t(csg.to_tris(csg.intersect(A, suelo)))

def _losa_redonda(u0, u1, v0, v1, e, R=6.0, r=1.8, n=20, m=8):
    """Puntos de una losa (u0..u1, v0..v1, grueso e) con esquinas de radio R y cantos de radio r."""
    pts = []
    for cu, cv, a0 in ((u1 - R, v1 - R, 0), (u0 + R, v1 - R, 90), (u0 + R, v0 + R, 180), (u1 - R, v0 + R, 270)):
        for i in range(n + 1):
            t = np.radians(a0 + 90.0 * i / n)
            for j in range(m + 1):
                q = np.pi * j / m - np.pi / 2
                rr = (R - r) + r * np.cos(q)
                pts.append((cu + rr * np.cos(t), cv + rr * np.sin(t), e / 2 + (e / 2 - 0) * np.sin(q) * (r / r)))
    P = np.array(pts)
    P[:, 2] = np.clip(P[:, 2], 0, e)            # tubo aplastado al grueso de la losa
    return P

def pie_moderno():
    """Pie minimalista con todas las esquinas y cantos redondeados: placa, base y un tirante trasero.
    Imanes enrasados en la cara delantera."""
    import csg
    pa = IMAN_D + 0.4; H = XO1 - XO0
    U0, U1 = -OY1, -OY0; L = U1 - U0
    a = np.radians(PIE_ANG); ca, sa = np.cos(a), np.sin(a)
    E = 5.0; BASE = 4.0
    z0 = BASE - 0.8
    def W(P):                              # (u, v, w) -> mundo
        P = np.asarray(P, float); u, v, w = P[:, 0], P[:, 1], P[:, 2]
        return np.stack([u, w * ca + v * sa, v * ca - w * sa + z0], 1)
    # placa: w de 0 (cara delantera) a E
    placa = W(_losa_redonda(U0, U1, -4.0, H, E, R=7.0, r=2.0))
    # base apoyada en la mesa
    base = _losa_redonda(U0 + 2, U1 - 2, -12.0, 34.0, BASE, R=9.0, r=1.8)
    # repisa: barra redondeada delante, abajo
    rep = W(_losa_redonda(U0 + 3, U1 - 3, -4.0, 2.5, 9.5, R=1.6, r=1.5) - np.array([0, 0, 9.5]))
    # tirante trasero: de la mitad de la placa al fondo de la base
    hv = 0.55 * H
    t1 = W(_losa_redonda(U0 + 8, U1 - 8, hv - 8, hv, E, R=4, r=1.8) + np.array([0, 0, E - 1.0]))
    t2 = _losa_redonda(U0 + 8, U1 - 8, 26.0, 33.0, BASE, R=4, r=1.8)
    A = csg.from_tris(_casco(placa))
    for P in (base, rep, np.vstack([t1, t2])):
        A = csg.union(A, csg.from_tris(_casco(P)))
    for xc, yc in IMANES:
        u, v = -yc, XO1 - xc
        caja = W([(u + du, v + dv, w) for du in (-pa / 2, pa / 2) for dv in (-pa / 2, pa / 2) for w in (-1, IMAN_G + 0.3)])
        A = csg.subtract(A, csg.from_tris(_casco(caja)))
    suelo = csg.from_tris(triangulos(*solido([(1, B(U0 - 20, U1 + 20, -40, 80, 0, 140))])))
    return csg.reparar_t(csg.to_tris(csg.intersect(A, suelo)))

if __name__ == "__main__" and "--pie" in sys.argv:
    out = sys.argv[1]
    stl(f"{out}/chapa_nh_pie.stl", pie_moderno(), "Chapa NH pie")            # el principal
    if "--todos" in sys.argv:                                                 # alternativas
        stl(f"{out}/chapa_nh_pie_lowpoly.stl", pie_lowpoly(), "Chapa NH pie low poly")
        stl(f"{out}/chapa_nh_pie_basico.stl", pie(), "Chapa NH pie basico")
    print("pies listos")
