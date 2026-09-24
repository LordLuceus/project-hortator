"""Read-only audit of TES3 painted-road connectivity and the legacy greedy walk.

Use --config for one flat openmw.cfg, or pass plugins in load order. No game
files are modified. This measures texture topology, NOT physical walkability.
Named exterior cells are places, not a verified settlement classification.
"""
import argparse
from collections import Counter, defaultdict, deque
import json
import math
from pathlib import Path
import random
import struct
import time


def records(path):
    with path.open('rb') as stream:
        while header := stream.read(16):
            if len(header) != 16:
                raise ValueError(f'Truncated record header: {path}')
            tag, size, _, flags = struct.unpack('<4sIII', header)
            if tag not in (b'LAND', b'LTEX', b'CELL'):
                stream.seek(size, 1)
                continue
            body = stream.read(size)
            if len(body) != size:
                raise ValueError(f'Truncated {tag}: {path}')
            yield tag, body


def fields(body):
    pos = 0
    while pos < len(body):
        tag, size = struct.unpack_from('<4sI', body, pos)
        pos += 8
        if pos + size > len(body):
            raise ValueError('Truncated subrecord')
        yield tag, body[pos:pos + size]
        pos += size


def text(data):
    return data.rstrip(b'\0').decode('cp1252')


def road(name):
    name = name.lower()
    return 'daed' not in name and any(word in name for word in ('road', 'cobble', 'path'))


def decode_grid(data):
    raw = struct.unpack('<256H', data)
    grid = [0] * 256
    index = 0
    for by in range(4):
        for bx in range(4):
            for y in range(4):
                for x in range(4):
                    grid[(by * 4 + y) * 16 + bx * 4 + x] = raw[index]
                    index += 1
    return grid


def load(paths):
    textures, mappings, lands, names = {}, {}, {}, {}
    for plugin, path in enumerate(paths):
        for tag, body in records(path):
            data = {}
            for key, value in fields(body):
                if tag == b'CELL' and key in (b'FRMR', b'MVRF'):
                    break  # Reference NAME/DATA must never replace the cell header.
                data.setdefault(key, value)
            if tag == b'LTEX':
                name = text(data[b'NAME']).lower()
                if b'DELE' in data:
                    textures.pop(name, None)
                    continue
                index, = struct.unpack('<I', data[b'INTV'])
                textures[name] = text(data[b'DATA'])  # Runtime matches filename, not id.
                mappings.setdefault((plugin, index), name)
            elif tag == b'LAND':
                xy = struct.unpack('<ii', data[b'INTV'])
                if b'DELE' in data or b'VTEX' not in data:
                    lands.pop(xy, None)
                else:
                    lands[xy] = plugin, decode_grid(data[b'VTEX'])
            elif b'DATA' in data:
                flags, x, y = struct.unpack('<Iii', data[b'DATA'])
                if not flags & 1:
                    if b'DELE' in data:
                        names.pop((x, y), None)
                    else:
                        names[x, y] = text(data.get(b'NAME', b''))
    tiles, unresolved = set(), Counter()
    for (cx, cy), (plugin, grid) in lands.items():
        for pos, raw in enumerate(grid):
            if not raw:
                continue
            texture = textures.get(mappings.get((plugin, raw - 1)))
            if texture is None:
                unresolved[paths[plugin].name] += 1
            elif road(texture):
                tiles.add((cx * 16 + pos % 16, cy * 16 + pos // 16))
    return tiles, names, unresolved


DELTAS = [(x, y) for x in (-1, 0, 1) for y in (-1, 0, 1) if x or y]


def neighbors(tile, tiles):
    x, y = tile
    return [(x + dx, y + dy) for dx, dy in DELTAS if (x + dx, y + dy) in tiles]


def endpoint_label(tile, names):
    x, y = tile[0] // 16, tile[1] // 16
    for ring in (0, 1):
        for dx in range(-ring, ring + 1):
            for dy in range(-ring, ring + 1):
                if max(abs(dx), abs(dy)) == ring and names.get((x + dx, y + dy)):
                    return names[x + dx, y + dy]
    return ''


def local_axis(tile, adjacency):
    # Match a scan centered on this tile: its component inside the 3x3-cell
    # discovery window, then the scanner's radius-three covariance fit.
    cx, cy = tile[0] // 16, tile[1] // 16
    seen, queue = {tile}, deque([tile])
    while queue:
        for near in adjacency[queue.popleft()]:
            if near not in seen and abs(near[0] // 16 - cx) <= 1 and abs(near[1] // 16 - cy) <= 1:
                seen.add(near)
                queue.append(near)
    local = [(x, y) for x, y in seen if (x - tile[0]) ** 2 + (y - tile[1]) ** 2 <= 9]
    if len(local) < 3:
        return None
    mx = sum(x for x, y in local) / len(local)
    my = sum(y for x, y in local) / len(local)
    xx = sum((x - mx) ** 2 for x, y in local)
    yy = sum((y - my) ** 2 for x, y in local)
    xy = sum((x - mx) * (y - my) for x, y in local)
    eigen = (xx + yy) / 2 + math.sqrt(max(0, (xx + yy) ** 2 / 4 - (xx * yy - xy * xy)))
    if abs(xy) > 1e-9:
        vx, vy = eigen - yy, xy
    else:
        vx, vy = (1, 0) if xx >= yy else (0, 1)
    length = math.hypot(vx, vy)
    return (vx / length, vy / length) if length else None


def greedy(start, heading, adjacency):
    current, visited, distance = start, {start}, 0.0
    while len(visited) < 600:
        raw = adjacency[current]
        candidates = [point for point in raw if point not in visited]
        best, score = None, 0.0
        for point in candidates:
            dx, dy = point[0] - current[0], point[1] - current[1]
            dot = (dx * heading[0] + dy * heading[1]) / math.hypot(dx, dy)
            if dot > score:
                best, score = point, dot
        if best is None:
            reason = 'heading_rejection' if candidates else 'visited_only' if raw else 'no_neighbors'
            return current, len(visited), distance, reason
        heading = best[0] - current[0], best[1] - current[1]
        distance += math.hypot(*heading) * 512
        current = best
        visited.add(current)
    return current, len(visited), distance, 'preview_limit'


def audit(paths):
    started = time.perf_counter()
    tiles, names, unresolved = load(paths)
    adjacency = {tile: neighbors(tile, tiles) for tile in tiles}
    pending, components, ids = set(tiles), [], {}
    while pending:
        seed = min(pending)
        pending.remove(seed)
        queue, component = deque([seed]), []
        while queue:
            tile = queue.popleft()
            component.append(tile)
            ids[tile] = len(components)
            for near in adjacency[tile]:
                if near in pending:
                    pending.remove(near)
                    queue.append(near)
        components.append(component)
    direct_names = lambda component: sorted({names.get((x // 16, y // 16), '') for x, y in component} - {''})
    places = [direct_names(component) for component in components]
    place_links = defaultdict(set)
    for group in places:
        for name in group:
            place_links[name].update(set(group) - {name})
    # Deterministic stratification by named place, not by wide patches of paving.
    # Sample two fitted directions at on-road starts, not off-road player scans.
    starts = defaultdict(list)
    for tile in sorted(tiles):
        name = names.get((tile[0] // 16, tile[1] // 16), '')
        if name:
            starts[name].append(tile)
    rng, outcomes, samples = random.Random(23), Counter(), 0
    for origin, points in sorted(starts.items()):
        for tile in rng.sample(points, min(24, len(points))):
            axis = local_axis(tile, adjacency)
            if axis is None:
                continue
            for heading in (axis, (-axis[0], -axis[1])):
                end, steps, distance, reason = greedy(tile, heading, adjacency)
                samples += 1
                outcomes[reason] += 1
                outcomes['one_tile'] += steps == 1
                outcomes['heading_rejection_after_moving'] += reason == 'heading_rejection' and steps > 1
                label = endpoint_label(end, names)
                outcomes['same_origin_label'] += label == origin
                outcomes['different_place_label'] += bool(label) and label != origin
                outcomes['unnamed_endpoint'] += not label
                outcomes['same_origin_label_but_outside_named_cell'] += label == origin and names.get((end[0] // 16, end[1] // 16)) != origin
                outcomes['same_origin_label_with_other_place_connected'] += label == origin and any(name != origin for name in places[ids[tile]])
    return {
        'plugins': [str(path) for path in paths], 'road_tiles': len(tiles),
        'components': len(components), 'unresolved_texture_tiles': dict(unresolved),
        'named_places_with_road_tiles': len(starts),
        'places_connected_to_other_named_cells': sum(bool(v) for v in place_links.values()),
        'samples': samples, 'sampling': 'up to 24 road tiles per named place; two scanner-fitted axes per usable tile',
        'outcomes': dict(outcomes),
        'largest_components': [{'tiles': len(components[i]), 'places': places[i]} for i in sorted(range(len(components)), key=lambda i: -len(components[i]))[:8]],
        'place_connections': {k: sorted(v) for k, v in sorted(place_links.items())},
        'seconds': round(time.perf_counter() - started, 3),
        'limitations': 'Topology only; no physical walkability or settlement classification. On-road samples, not actual off-road player scans; Python floating point may differ from engine float32 at ties. Flat configs only; includes deliberately rejected.',
    }


def config_paths(path):
    dirs, content = [], []
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        key, separator, value = line.partition('=')
        if not separator:
            continue
        key, value = key.strip(), value.strip().strip('"')
        if key == 'config':
            raise ValueError('Resolve config includes before auditing')
        if key in ('data', 'data-local'):
            candidate = Path(value)
            if not candidate.is_absolute():
                raise ValueError('Resolve relative data directories before auditing')
            dirs.append(candidate)
        if key == 'content' and Path(value).suffix.lower() in ('.esm', '.esp', '.omwaddon', '.omwgame'):
            content.append(value)
    result = []
    for name in content:
        matches = [directory / name for directory in dirs if (directory / name).is_file()]
        if not matches:
            raise FileNotFoundError(name)
        result.append(matches[-1])
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plugins', nargs='*', type=Path)
    parser.add_argument('--config', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--test-data', type=Path, help='Export local tile inputs for the opt-in C++ regression; do not distribute game data')
    parser.add_argument('--content-list', type=Path, help='Export ordered plugin paths for OPENMW_ROAD_CONTENT_FILES; contains local paths')
    args = parser.parse_args()
    if bool(args.config) == bool(args.plugins):
        parser.error('Supply either --config or ordered plugin paths')
    paths = config_paths(args.config) if args.config else args.plugins
    if args.content_list:
        args.content_list.write_text('\n'.join(str(path.resolve()) for path in paths) + '\n', encoding='utf-8')
    report = json.dumps(audit(paths), indent=2)
    if args.test_data:
        tiles, names, unresolved = load(paths)
        if unresolved:
            raise ValueError('Cannot export incomplete texture resolution')
        with args.test_data.open('w', encoding='utf-8') as output:
            for x, y in sorted(tiles):
                output.write(f'{x} {y} {json.dumps(names.get((x // 16, y // 16), ""), ensure_ascii=False)}\n')
    if args.output:
        args.output.write_text(report + '\n', encoding='utf-8')
    else:
        print(report)
