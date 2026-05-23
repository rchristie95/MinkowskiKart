import math

RADIUS = 82.0
ROAD_HALF_WIDTH = 8.0

START_U = 0.55
START_GRID_ROWS = 7
START_GRID_COLS = 3
START_GRID_LATERALS = (-4.4, 0.0, 4.4)
START_GRID_U_OFFSET = 0.11
START_GRID_U_SPACING = 0.070
START_GRID_LIFT = 0.55

def vmul(v, s):
    return (v[0] * s, v[1] * s, v[2] * s)

def vadd(v1, v2):
    return (v1[0] + v2[0], v1[1] + v2[1], v1[2] + v2[2])

def vcross(v1, v2):
    return (v1[1] * v2[2] - v1[2] * v2[1],
            v1[2] * v2[0] - v1[0] * v2[2],
            v1[0] * v2[1] - v1[1] * v2[0])

def vnorm(v):
    mag = math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2])
    return (v[0]/mag, v[1]/mag, v[2]/mag)

def mobius_point(u, lateral_v):
    cu = math.cos(u)
    su = math.sin(u)
    ch = math.cos(u / 2.0)
    sh = math.sin(u / 2.0)
    x = (RADIUS + lateral_v * ch) * cu
    y = lateral_v * sh
    z = (RADIUS + lateral_v * ch) * su
    return (x, y, z)

def tangent_at(u):
    cu = math.cos(u)
    su = math.sin(u)
    return (-su, 0.0, cu)

def mobius_normal(u, lateral_v):
    cu = math.cos(u)
    su = math.sin(u)
    ch = math.cos(u / 2.0)
    sh = math.sin(u / 2.0)
    dr_du = -0.5 * lateral_v * sh
    dy_du = 0.5 * lateral_v * ch
    r = RADIUS + lateral_v * ch
    du = (dr_du * cu - r * su, dy_du, dr_du * su + r * cu)
    dv = (ch * cu, sh, ch * su)
    normal = vnorm(vcross(dv, du))
    if normal[1] < 0:
        normal = vmul(normal, -1.0)
    return normal

def item_position(u, lateral, lift=1.2):
    p = mobius_point(u, lateral)
    n = mobius_normal(u, lateral)
    return vadd(p, vmul(n, lift))

for row in range(START_GRID_ROWS):
    for col in range(START_GRID_COLS):
        idx = row * START_GRID_COLS + col
        u = START_U - START_GRID_U_OFFSET - row * START_GRID_U_SPACING
        lateral = START_GRID_LATERALS[col]
        p = item_position(u, lateral, START_GRID_LIFT)
        t = tangent_at(u)
        heading = math.degrees(math.atan2(t[0], t[2]))
        print(f'  <start position="{idx}" x="{p[0]:.3f}" y="{p[1]:.3f}" z="{p[2]:.3f}" h="{heading:.2f}"/>')
