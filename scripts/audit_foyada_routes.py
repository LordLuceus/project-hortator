"""Measure candidate dry-channel routes against actual TES3 terrain heights.

Developer investigation only: materials do not prove a named foyada or physical
walkability. No game files are changed. Supply vanilla Morrowind.esm.
"""
import argparse
from collections import Counter
import heapq
import math
import json
from pathlib import Path
import struct

from audit_road_routes import records, fields, text, decode_grid, road, neighbors


def heights(data):
    base, = struct.unpack_from('<f', data)
    offsets = struct.unpack_from('<4225b', data, 4)
    result = []
    for y in range(65):
        base += offsets[y * 65]
        z = base
        result.append(z * 8)
        for x in range(1, 65):
            z += offsets[y * 65 + x]
            result.append(z * 8)
    return result


def load(path):
    textures, tiles, elevation, names = {}, {}, {}, {}
    for tag, body in records(path):
        data = {}
        for key, value in fields(body):
            if tag == b'CELL' and key in (b'FRMR', b'MVRF'):
                break
            data.setdefault(key, value)
        if tag == b'LTEX':
            textures[struct.unpack('<I', data[b'INTV'])[0] + 1] = text(data[b'DATA']).lower()
        elif tag == b'LAND':
            cx, cy = struct.unpack('<ii', data[b'INTV'])
            if b'VTEX' in data:
                for i, value in enumerate(decode_grid(data[b'VTEX'])):
                    tiles[cx * 16 + i % 16, cy * 16 + i // 16] = value
            if b'VHGT' in data:
                elevation[cx, cy] = heights(data[b'VHGT'])
        elif tag == b'CELL':
            flags, x, y = struct.unpack('<Iii', data[b'DATA'])
            if not flags & 1:
                names[x, y] = text(data.get(b'NAME', b''))
    return textures, tiles, elevation, names


def height(vertex, elevation):
    x, y = vertex
    grid = elevation.get((x // 64, y // 64))
    return grid[y % 64 * 65 + x % 64] if grid is not None else None


def grade(a, b, elevation):
    dx, dy = b[0] - a[0], b[1] - a[1]
    samples = [height((a[0] * 4 + 2 + i * dx, a[1] * 4 + 2 + i * dy), elevation) for i in range(5)]
    if any(z is None for z in samples):
        return math.inf
    along = max(abs(z1 - z0) / (128 * math.hypot(dx, dy)) for z0, z1 in zip(samples, samples[1:]))
    return max(along, *(surface_grade(a[0] * 4 + 2 + i * dx,
                                    a[1] * 4 + 2 + i * dy, elevation)
                        for i in (.5, 1.5, 2.5, 3.5)))


def surface_grade(x, y, elevation):
    # Same fixed v0-v1-v3 / v1-v2-v3 split as Storage::getHeightAt.
    # On a triangle/grid edge either incident walkable face can support a path;
    # loaded navmesh remains the authority for actor clearance and obstacles.
    grades = []
    for qx in (math.floor(x - 1e-6), math.floor(x + 1e-6)):
        for qy in (math.floor(y - 1e-6), math.floor(y + 1e-6)):
            z0, z1, z2, z3 = (height(p, elevation) for p in
                             ((qx,qy),(qx+1,qy),(qx+1,qy+1),(qx,qy+1)))
            if None in (z0, z1, z2, z3):
                continue
            side = x - qx + y - qy
            if side <= 1 + 1e-6:
                grades.append(math.hypot(z1-z0, z3-z0)/128)
            if side >= 1 - 1e-6:
                grades.append(math.hypot(z2-z3, z2-z1)/128)
    return min(grades, default=math.inf)


def route(start, goals, tiles, roads, elevation, max_grade):
    distance, previous = {start: 0}, {}
    queue = [(0, start)]
    while queue:
        cost, at = heapq.heappop(queue)
        if cost != distance[at]:
            continue
        if at in goals:
            path = [at]
            while at in previous:
                at = previous[at]
                path.append(at)
            return list(reversed(path)), cost
        for near in neighbors(at, tiles):
            if not (at in roads and near in roads) and grade(at, near, elevation) > max_grade:
                continue
            next_cost = cost + 512 * math.hypot(near[0] - at[0], near[1] - at[1])
            if next_cost < distance.get(near, math.inf):
                distance[near] = next_cost
                previous[near] = at
                heapq.heappush(queue, (next_cost, near))
    return [], 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plugin', type=Path)
    parser.add_argument('--test-data', type=Path, help='Export local game inputs for the optional C++ regression')
    args = parser.parse_args()
    textures, raw, elevation, names = load(args.plugin)
    roads = {p for p, v in raw.items() if road(textures.get(v, ''))}
    channels = {p for p, v in raw.items() if any(s in textures.get(v, '') for s in ('ma_lavaflow', 'ma_rock04'))}
    tiles = roads | channels
    goals = {p for p in tiles if names.get((p[0] // 16, p[1] // 16)) == 'Ghostgate'}
    if args.test_data:
        needed = {(x // 16 + dx, y // 16 + dy) for x, y in channels
                  for dx in (-1, 0, 1) for dy in (-1, 0, 1)}
        with args.test_data.open('w', encoding='utf-8') as output:
            for x, y in sorted(tiles):
                output.write(f'T {x} {y} {json.dumps(textures[raw[x,y]])} '
                             f'{json.dumps(names.get((x//16,y//16), ""))}\n')
            for cx, cy in sorted(needed):
                if (cx, cy) in elevation:
                    output.write(f'H {cx} {cy} ' + ' '.join(str(int(z)) for z in elevation[cx,cy]) + '\n')
    for limit in (math.inf, math.tan(math.radians(46)), 0.7, 0.5):
        path, length = route((-35, -16), goals, tiles, roads, elevation, limit)
        print('grade_limit=', limit, 'steps=', len(path), 'metres=', round(length / 70, 1))
        if not path:
            continue
        print('materials=', dict(Counter(textures[raw[p]] for p in path)))
        grades = [grade(a, b, elevation) for a, b in zip(path, path[1:]) if not (a in roads and b in roads)]
        print('max_new_edge_grade=', max(grades, default=0), 'edges_over_0.7=', sum(g > 0.7 for g in grades))
        cells = []
        for x, y in path:
            cell = x // 16, y // 16
            if not cells or cell != cells[-1]:
                cells.append(cell)
        print('cells=', cells)
