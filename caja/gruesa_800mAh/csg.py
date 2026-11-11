"""CSG por árbol BSP (traducción a Python de csg.js de Evan Wallace, licencia MIT).
Suficiente para unir/restar/intersecar mallas de unos pocos miles de polígonos."""
import numpy as np

EPS = 1e-5
COPLANAR, FRONT, BACK, SPANNING = 0, 1, 2, 3


class Plane:
    __slots__ = ("n", "w")

    def __init__(self, n, w):
        self.n = n; self.w = w

    @staticmethod
    def from_points(a, b, c):
        n = np.cross(b - a, c - a)
        L = np.linalg.norm(n)
        if L < 1e-12:
            return None
        n = n / L
        return Plane(n, float(n @ a))

    def flipped(self):
        return Plane(-self.n, -self.w)

    def split(self, poly, cop_front, cop_back, front, back):
        V = poly.v
        d = V @ self.n - self.w
        types = np.where(d < -EPS, BACK, np.where(d > EPS, FRONT, COPLANAR))
        ptype = 0
        for t in types:
            ptype |= int(t)
        if ptype == COPLANAR:
            (cop_front if self.n @ poly.plane.n > 0 else cop_back).append(poly)
        elif ptype == FRONT:
            front.append(poly)
        elif ptype == BACK:
            back.append(poly)
        else:
            f, b = [], []
            n = len(V)
            for i in range(n):
                j = (i + 1) % n
                ti, tj = types[i], types[j]
                vi, vj = V[i], V[j]
                if ti != BACK: f.append(vi)
                if ti != FRONT: b.append(vi)
                if (ti | tj) == SPANNING:
                    t = (self.w - self.n @ vi) / (self.n @ (vj - vi))
                    p = vi + (vj - vi) * t
                    f.append(p); b.append(p)
            if len(f) >= 3: front.append(Polygon(np.array(f), poly.plane))
            if len(b) >= 3: back.append(Polygon(np.array(b), poly.plane))


class Polygon:
    __slots__ = ("v", "plane")

    def __init__(self, v, plane=None):
        self.v = np.asarray(v, float)
        self.plane = plane or Plane.from_points(self.v[0], self.v[1], self.v[2])

    def flipped(self):
        return Polygon(self.v[::-1].copy(), self.plane.flipped())


class Node:
    __slots__ = ("plane", "front", "back", "polys")

    def __init__(self, polys=None):
        self.plane = None; self.front = None; self.back = None; self.polys = []
        if polys: self.build(polys)

    def invert(self):
        stack = [self]
        while stack:
            n = stack.pop()
            n.polys = [p.flipped() for p in n.polys]
            n.plane = n.plane.flipped()
            n.front, n.back = n.back, n.front
            if n.front: stack.append(n.front)
            if n.back: stack.append(n.back)

    def clip_polygons(self, polys):
        # iterativo para no reventar la pila
        out = []
        stack = [(self, polys)]
        while stack:
            node, ps = stack.pop()
            if not node.plane:
                out.extend(ps); continue
            f, b = [], []
            for p in ps:
                node.plane.split(p, f, b, f, b)
            if node.front: stack.append((node.front, f))
            else: out.extend(f)
            if node.back: stack.append((node.back, b))
            # sin rama trasera: lo de detrás se descarta
        return out

    def clip_to(self, bsp):
        stack = [self]
        while stack:
            n = stack.pop()
            n.polys = bsp.clip_polygons(n.polys)
            if n.front: stack.append(n.front)
            if n.back: stack.append(n.back)

    def all_polygons(self):
        out = []; stack = [self]
        while stack:
            n = stack.pop(); out.extend(n.polys)
            if n.front: stack.append(n.front)
            if n.back: stack.append(n.back)
        return out

    def build(self, polys):
        stack = [(self, polys)]
        while stack:
            node, ps = stack.pop()
            if not ps: continue
            if not node.plane:
                node.plane = ps[0].plane
            f, b = [], []
            for p in ps:
                node.plane.split(p, node.polys, node.polys, f, b)
            if f:
                if not node.front: node.front = Node()
                stack.append((node.front, f))
            if b:
                if not node.back: node.back = Node()
                stack.append((node.back, b))


def _polys(tris):
    out = []
    for t in tris:
        p = Plane.from_points(t[0], t[1], t[2])
        if p is not None:
            out.append(Polygon(t, p))
    return out


def union(A, B):
    a = Node(A); b = Node(B)
    a.clip_to(b); b.clip_to(a); b.invert(); b.clip_to(a); b.invert()
    a.build(b.all_polygons()); return a.all_polygons()


def subtract(A, B):
    a = Node(A); b = Node(B)
    a.invert(); a.clip_to(b); b.clip_to(a); b.invert(); b.clip_to(a); b.invert()
    a.build(b.all_polygons()); a.invert(); return a.all_polygons()


def intersect(A, B):
    a = Node(A); b = Node(B)
    a.invert(); b.clip_to(a); b.invert(); a.clip_to(b); b.clip_to(a)
    a.build(b.all_polygons()); a.invert(); return a.all_polygons()


def to_tris(polys):
    out = []
    for p in polys:
        v = p.v
        for i in range(1, len(v) - 1):
            out.append((v[0], v[i], v[i + 1]))
    return np.array(out, float)


def from_tris(tris):
    return _polys(np.asarray(tris, float))


def reparar_t(tris, tol=1e-5, pasadas=6):
    """Parte los triángulos donde otro vértice cae en mitad de su arista
    (uniones en T que deja el BSP), para que la malla quede cerrada."""
    import collections
    T = [tuple(map(tuple, np.round(t, 6))) for t in tris]
    for _ in range(pasadas):
        e = collections.Counter()
        for t in T:
            for i in range(3): e[(t[i], t[(i + 1) % 3])] += 1
        abiertas = {k for k in e if e.get((k[1], k[0]), 0) != e[k]}
        if not abiertas: break
        V = np.array(sorted({v for t in T for v in t}))
        nuevo = []
        for t in T:
            hecho = False
            for i in range(3):
                a, b, c = t[i], t[(i + 1) % 3], t[(i + 2) % 3]
                if (a, b) not in abiertas: continue
                A, Bv = np.array(a), np.array(b); d = Bv - A; L2 = d @ d
                if L2 < 1e-14: continue
                s = (V - A) @ d / L2
                cand = (s > 1e-6) & (s < 1 - 1e-6)
                if not cand.any(): continue
                P = V[cand]; sc = s[cand]
                dist = np.linalg.norm(A + sc[:, None] * d - P, axis=1)
                ok = dist < tol
                if not ok.any(): continue
                orden = np.argsort(sc[ok]); pts = [a] + [tuple(p) for p in P[ok][orden]] + [b]
                for p, q in zip(pts[:-1], pts[1:]):
                    nuevo.append((p, q, c))
                hecho = True
                break
            if not hecho: nuevo.append(t)
        T = nuevo
    return np.array(T, float)
