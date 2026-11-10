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
TAPA = 1.6           # grosor de la tapa trasera

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
IMAN_XS = [-8.5, 0.0, 8.5]
PIEL = 1.0            # plástico entre imán y ropa

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
    # apoyos de la placa: extremo antena (debajo del PCB, sin tocar la pantalla)
    ops += [(1, B(-IX, IX, LCD_Y1 + 0.6, IY1, IZ0, PCB_Z0 - 0.05))]
    # apoyos en las esquinas del lado USB (fuera de los botones) y topes del borde
    for sx in (-1, 1):
        ops += [(1, B(sx * 11.75, sx * IX, 4.7, 6.4, IZ0, PCB_Z0 - 0.05))]
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
    # ranuras de cierre en las paredes largas
    for sx in (-1, 1):
        ops += [(0, B(sx * IX, sx * (IX + 0.35), 18, 42, OZ1 - 1.05, OZ1 - 0.35))]
    return ops

# ------------------------------------------------------------------ tapa trasera
CLT = 0.12
def tapa():
    # se modela con la cara exterior en z=0 y hacia arriba (posición de impresión)
    W = IX - CLT
    ops = [(1, B(-W, W, IY0 + CLT, IY1 - CLT, 0, TAPA))]
    # pestañas de cierre
    for sx in (-1, 1):
        ops += [(1, B(sx * (W - 0.01), sx * (IX + 0.25), 19, 41, 0.45, 0.95))]
    # alojamientos de imanes (lado USB, lejos de la antena)
    pa = IMAN_D + 0.4; ph = IMAN_G + 0.3
    alto_boss = PIEL + ph + 0.6          # desde la cara exterior
    yc = 3.2
    for xc in IMAN_XS:
        ops += [(1, B(xc - pa / 2 - 0.8, xc + pa / 2 + 0.8, yc - pa / 2 - 0.8, yc + pa / 2 + 0.8, 0, alto_boss))]
    for xc in IMAN_XS:
        ops += [(0, B(xc - pa / 2, xc + pa / 2, yc - pa / 2, yc + pa / 2, PIEL, alto_boss + 1))]
    # marca de orientación (USB) en la cara interior
    ops += [(0, B(-1.5, 1.5, IY1 - 6, IY1 - 3, TAPA - 0.4, TAPA + 1))]
    return ops, alto_boss

def contraplaca():
    pa = IMAN_D + 0.4; ph = IMAN_G + 0.3
    L = (IMAN_XS[-1] - IMAN_XS[0]) + pa + 2 * 2.0
    H = pa + 2 * 2.0
    G = ph + PIEL
    ops = [(1, B(-L / 2, L / 2, -H / 2, H / 2, 0, G))]
    for xc in IMAN_XS:
        ops += [(0, B(xc - pa / 2, xc + pa / 2, -pa / 2, pa / 2, PIEL, G + 1))]
    return ops, (L, H, G)

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    c = triangulos(*solido(cuerpo()))
    stl(f"{out}/chapa_nh_cuerpo.stl", c, "Chapa NH cuerpo")
    t_ops, ab = tapa()
    t = triangulos(*solido(t_ops))
    stl(f"{out}/chapa_nh_tapa.stl", t, "Chapa NH tapa")
    p_ops, dims = contraplaca()
    p = triangulos(*solido(p_ops))
    stl(f"{out}/chapa_nh_contraplaca.stl", p, "Chapa NH contraplaca")
    print("Exterior caja: %.1f x %.1f x %.1f mm" % (2 * OX, OY1 - OY0, OZ1 - OZ0))
    print("Interior:      %.1f x %.1f x %.1f mm" % (2 * IX, IY1 - IY0, IZ1 - IZ0))
    print("Hueco bateria: %.1f mm de alto (bat %.1f)  y %.1f-%.1f (antena libre desde %.1f)" %
          (IZ1 - BAT_Z0, BAT_G, BAT_Y0, BAT_Y0 + BAT_L, BAT_Y0 + BAT_L))
    print("Alojamiento imanes: tapa boss %.1f mm, tope bateria en y=%.1f" % (ab, 3.2 + (IMAN_D + 0.4) / 2 + 0.8))
    print("Contraplaca: %.1f x %.1f x %.1f mm" % dims)
    print("Triangulos:", len(c), len(t), len(p))
