# First Terrain scan: road-network benchmark

The optional `MWAccessibilityRoadNetwork.RealContentConstruction` test loads
actual installed content into an ESM store, then invokes the same network builder
as the scanner. It does not launch the game, load a save, or overwrite its log.

## Windows example

Use a flat configuration with absolute data directories; the audit rejects
configuration includes rather than silently benchmarking incomplete input.

```powershell
python scripts/audit_road_routes.py --config "$env:USERPROFILE\Documents\My Games\OpenMW\openmw.cfg" --content-list "$env:TEMP\road-content.txt" --output "$env:TEMP\road-audit.json"
if ($LASTEXITCODE -ne 0) { throw 'Could not resolve the content files' }
cmake --build MSVC2022_64 --config RelWithDebInfo --target openmw-tests -- /m:2 /verbosity:minimal
if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
$env:OPENMW_ROAD_CONTENT_FILES = "$env:TEMP\road-content.txt"
$env:OPENMW_ROAD_NETWORK_REPORT = "$env:TEMP\road-routes.txt"
& .\MSVC2022_64\RelWithDebInfo\openmw-tests.exe '--gtest_filter=MWAccessibilityRoadNetwork.RealContentConstruction'
if ($LASTEXITCODE -ne 0) { throw 'Road-network regression failed' }
```

The test constructs three fresh networks. `build_ms` includes temporary-cache
cleanup but excludes initial ESM-store loading, just as the first Terrain scan
runs after game content has loaded. `queries_ms` covers 110 sampled positions,
not a single keypress. The report records destination names, foyada flags,
distances and every tile of each sampled route. Compare before/after reports
byte-for-byte; timings alone do not establish equivalent behaviour.

The environment variables apply only to this shell. Remove them when finished:

```powershell
Remove-Item Env:OPENMW_ROAD_CONTENT_FILES,Env:OPENMW_ROAD_NETWORK_REPORT -ErrorAction SilentlyContinue
```

## Limits

- This measures the network builder and route queries, not total in-game frame
  latency, renderer contention, or speech startup. It does not execute Lua LOAD
  scripts that might alter records.
- OS file-cache warmth affects results. Compare repeated warm runs; do not claim
  a speedup by comparing a cold baseline against a warm optimized run.
- The fixed sample grid covers Vvardenfell and nearby cells; it
  is not exhaustive coverage of all modded locations or all possible routes.
- Without the content-list environment variable, the real-content test skips.
  The full suite also runs synthetic tests without game files.
- Generated files contain local paths or game-derived data. Keep them local,
  outside the repository; do not distribute them with the source or a release.
