# SpatialJoin C++ library

Use [SpatialJoin](https://github.com/ad-freiburg/spatialjoin) as a compiled
static library instead of piping WKT through its CLI. This fork adds a small
`JoinSession` API for OSM-style E7 coordinates, numeric IDs, and typed results.
It supports **self joins** and **joins between two sets**; the original CLI is
still available. The wrapper is an initial API, not yet a stable ABI.

## Quick start

On Linux, you need CMake 3.21+, a C++14 compiler, Git, and a writable directory
for SpatialJoin's temporary cache files. `src/util` is a Git submodule, so clone
recursively. Zlib and BZip2 support is optional; libosmium is **not**
required.

```sh
git clone --recurse-submodules https://github.com/jan-ba/spatialjoin-lib.git
cmake -S spatialjoin-lib -B spatialjoin-lib/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
  -DSPATIALJOIN_BUILD_CLI=OFF \
  -DSPATIALJOIN_BUILD_TESTS=OFF
cmake --build spatialjoin-lib/build --parallel
cmake --install spatialjoin-lib/build
```

The install includes `libspatialjoin.a`, its utility libraries, headers, and a
CMake package. In your own project, include only `<spatialjoin/Join.h>` and link
the exported target; CMake propagates the required include paths and libraries:

```cmake
find_package(spatialjoin CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE spatialjoin::spatialjoin)
```

If CMake cannot find the installation, configure your app with
`-DCMAKE_PREFIX_PATH="$HOME/.local"`. The runnable
[`examples/library`](examples/library) project uses exactly this setup:

```sh
cmake -S spatialjoin-lib/examples/library -B example-build \
  -DCMAKE_PREFIX_PATH="$HOME/.local"
cmake --build example-build
./example-build/spatialjoin-library-example
# 10 contains 20
```

No installation is needed when embedding the cloned source instead:

```cmake
add_subdirectory(path/to/spatialjoin-lib)
target_link_libraries(your_app PRIVATE spatialjoin::spatialjoin)
```

Initialize the submodule first if you did not clone recursively:
`git submodule update --init --recursive`. The
[`examples/embedded`](examples/embedded) project demonstrates this path:

```sh
cmake -S spatialjoin-lib/examples/embedded -B embedded-build
cmake --build embedded-build --target spatialjoin-embedded-example
./embedded-build/spatialjoin-embedded-example
# 10 contains 20
```

## Use the library

The first coordinate in `E7Point` is **longitude**, the second is **latitude**;
both are signed degrees multiplied by 10,000,000. This example joins one
polygon on the left to one point on the right and prints `10 contains 20`:

```cpp
#include <iostream>
#include <spatialjoin/Join.h>

int main() {
  spatialjoin::JoinOptions options;
  options.mode = spatialjoin::JoinMode::TwoSets;
  options.relations = {spatialjoin::Relation::Contains};
  options.serialize_callbacks = true;

  spatialjoin::JoinSession join(options);
  spatialjoin::E7Polygon polygon{
      {{0, 0}, {100000000, 0}, {100000000, 100000000},
       {0, 100000000}},
      {}};
  join.addPolygon(spatialjoin::DatasetSide::Left, 10, polygon);
  join.addPoint(spatialjoin::DatasetSide::Right, 20,
                {50000000, 50000000});

  join.run([](const spatialjoin::Match& match) {
    if (match.direction == spatialjoin::Direction::LeftToRight) {
      std::cout << match.left_id << " contains " << match.right_id << '\n';
    }
  });
}
```

Add as many geometries as needed, in either side order, before calling
`run(callback)`. This call performs the join **once**, streams matches to the
callback, and returns `JoinStatistics` with input and emitted-match counts.
It does not collect all results in memory. To run again with changed inputs,
construct a new session.

For a **self join**, leave `options.mode` at its default `JoinMode::Self`, add
all inputs with `DatasetSide::Left`, and call `run()` once. The CLI and wrapper
retain the engine's directed self-join output, including both orders of a
symmetric match.

### Geometry inputs

| Geometry | E7 input method | Representation |
| --- | --- | --- |
| Point | `addPoint` | `E7Point {longitude, latitude}` |
| MultiPoint | `addMultiPoint` | `std::vector<E7Point>` |
| LineString | `addLine` | `E7Line`, a vector of points |
| MultiLineString | `addMultiLine` | Vector of `E7Line` |
| Polygon | `addPolygon` | `E7Polygon {outer, inners}` |
| MultiPolygon | `addMultiPolygon` | `E7MultiPolygon`, a vector of polygons |

Rings may be closed or unclosed; the polygon constructor normalizes closure
and orientation. A closed point sequence is **not** automatically treated as a
polygon: choose `addLine` or `addPolygon` explicitly. Each polygon keeps its
own inner rings, so multipolygons can represent OSM areas with holes.

You can keep your application's point types. `addLine` accepts a point range
and a getter returning `E7Point`; `addPolygon` accepts outer and inner ranges
with the same getter. With an existing `JoinSession join`, for example:

```cpp
struct OsmPoint { int32_t lon_e7, lat_e7; };
std::vector<OsmPoint> way{{0, 0}, {10000000, 10000000}};
join.addLine(spatialjoin::DatasetSide::Left, 7, way,
             [](const OsmPoint& point) {
               return spatialjoin::E7Point{point.lon_e7, point.lat_e7};
             });
```

For already projected input, `JoinSession::project`, `projectLine`,
`projectPolygon`, and `addNative` expose the engine's native geometry types.
The native coordinates are **Web Mercator metres times 10**, not E7 values.
No libosmium-specific adapter is required or provided yet; convert assembled
OSM areas to `E7Polygon`/`E7MultiPolygon` or use the range overloads.

### Matches and options

`Match` contains `uint64_t left_id`, `right_id`, a `Relation` enum, and a
`Direction` enum. In a two-set join the IDs always identify their dataset
sides, even if both sides use the same numeric ID. `LeftToRight` evaluates a
directional relation from left to right; for `Contains` or `Covers`, it means
the left geometry contains/covers the right. `RightToLeft` reverses that.
Symmetric relations use `Direction::Symmetric` and are emitted once per pair
in a two-set join. In a self join, directed IDs follow the predicate order.

Set `options.relations` to the topological relations you want returned:
`Intersects`, `Contains`, `Covers`, `Touches`, `Equals`, `Overlaps`, or
`Crosses`. This filters **output**, not the engine's geometric computation.

Two other output modes are available instead of topological relations:

- Set `options.output = OutputMode::DE9IM` to get a nine-character matrix in
  `Match::de9im` (**not** null-terminated). An optional nine-character
  `options.de9im_filter` applies to each directed matrix; `Match::direction`
  tells you its orientation.
- Set `options.output = OutputMode::WithinDistance` and a nonnegative
  `options.within_distance_m` to get the measured distance in
  `Match::distance_m`.

`JoinOptions` defaults to one worker, one cache thread, a batch size of 8192,
and `cache_directory = "."`. Set the cache directory to a **writable location
with enough free space** for your workload. Callbacks may run concurrently on
worker threads; set `serialize_callbacks = true` if your callback is not
thread-safe. Callback exceptions are rethrown after the sweep finishes.
Numeric IDs must be unique *within* each side; `validate_ids = true` checks
duplicates at the cost of an in-memory ID set. The same ID can occur on both
sides.

### Limits and testing

E7 coordinates are projected and quantized to a **0.1-metre Web Mercator
grid** on ingestion. This is less precise than E7; resolution is not yet
configurable. Some close points or narrow features can collapse after
quantization. There is no incremental re-run or native libosmium adapter yet.
Windows support is not provided.

To build and run the tests:

```sh
cmake -S spatialjoin-lib -B spatialjoin-lib/build-tests \
  -DCMAKE_BUILD_TYPE=Release -DSPATIALJOIN_BUILD_TESTS=ON
cmake --build spatialjoin-lib/build-tests --parallel
ctest --test-dir spatialjoin-lib/build-tests --output-on-failure
```

## Original CLI

The upstream CLI still reads line-separated WKT from stdin or a file and
writes relation lines to stdout (or a BZ2/GZ/plain file with `-o`). Build it
with `-DSPATIALJOIN_BUILD_CLI=ON` (the default for a standalone build). Its
topological predicates, DE-9IM, and within-distance modes remain available.

### Usage

```
$ cat example.txt
POLYGON((0 0, 10  0 ,10 10, 0 10, 0 0))
POLYGON((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 9 1, 9 9, 1 9, 1 1))
MULTIPOLYGON(((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 9 1, 9 9, 1 9, 1 1)))
POLYGON((4 4, 5 4, 5 5, 4 5, 4 4))
POLYGON((4 4, 5 4, 5 11, 4 11, 4 4))
LINESTRING(1 1, 1 2)
LINESTRING(0.5 1.5, 1.5 1.5)
LINESTRING(-10 1, 100 1)
POINT(0.5 0.5)
```

```
$ spatialjoin < example.txt
1 contains 9
9 intersects 1
[...]
```

You can also give the input file as an argument.

### Custom IDs

You may specify a custom geometry string ID, outputted instead of the line number, before the WKT, separated by a tab:

```
$ cat example_id.txt
polygon1	POLYGON((0 0, 10  0 ,10 10, 0 10, 0 0))
polygon2	POLYGON((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 9 1, 9 9, 1 9, 1 1))
multipolygon3	MULTIPOLYGON(((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 9 1, 9 9, 1 9, 1 1)))
polygon4	POLYGON((4 4, 5 4, 5 5, 4 5, 4 4))
polygon5	POLYGON((4 4, 5 4, 5 11, 4 11, 4 4))
linestring6	LINESTRING(1 1, 1 2)
linestring7	LINESTRING(0.5 1.5, 1.5 1.5)
linestring8	LINESTRING(-10 1, 100 1)
point9	POINT(0.5 0.5)
```

```
$ spatialjoin < example_id.txt
polygon1 contains point9
point9 intersects polygon1
[...]
```

### DE-9IM joins

To calculate the DE-9IM matrix between intersecting geometries, use the `--de9im` option:

```
$ spatialjoin --de9im < example.txt
9	0FFFFF212	1
1	0F2FF1FF2	9
9	0FFFFF212	2
2	0F2FF1FF2	9
9	0FFFFF212	3
3	0F2FF1FF2	9
[...]
```

### Non-self joins

For non-self joins, you can give the left and right side as two file arguments:

```
$ spatialjoin example_left.txt example_right.txt
```

Alternatively, you may specify a "side" (either 0 or 1) per geometry, as an additional tab-separated field after the custom geometry ID. If sides are defined, only geometries from different sides are compared. Note that a custom geometry ID *must* be given, otherwise the side will be interpreted as the custom geometry ID. The default side is 0.

```
$ cat example_nonself.txt
polygon1	0	POLYGON((0 0, 10  0 ,10 10, 0 10, 0 0))
polygon2	0	POLYGON((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 9 1, 9 9, 1 9, 1 1))
multipolygon3	0	MULTIPOLYGON(((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 9 1, 9 9, 1 9, 1 1)))
polygon4	1	POLYGON((4 4, 5 4, 5 5, 4 5, 4 4))
polygon5	1	POLYGON((4 4, 5 4, 5 11, 4 11, 4 4))
linestring6	1	LINESTRING(1 1, 1 2)
linestring7	1	LINESTRING(0.5 1.5, 1.5 1.5)
linestring8	0	LINESTRING(-10 1, 100 1)
point9	1	POINT(0.5 0.5)
```

### `within-distance` queries

To compute a spatial join on a `within-distance` predicate, use the `--within-distance <METER>` option. `spatialjoin` will output all pairs of geometries which are within `<METER>` meters of each other. Note that your input geometries **must be** WGS 84 coordinates (longitude/latitude pairs) for correct meter distance computation.

## Upstream and research

This repository is a fork of [ad-freiburg/spatialjoin](https://github.com/ad-freiburg/spatialjoin).
The SpatialJoin engine and original CLI are by the University of Freiburg;
the methods are described in the [SIGSPATIAL 2025 paper](https://ad-publications.cs.uni-freiburg.de/SIGSPATIAL_spatialjoin_BBK_2025.pdf).
The upstream project reports a full self join of roughly 1.5 billion OSM
geometries in about 90 minutes on a 32-thread Ryzen 9 7950X system with
128 GB RAM and 7.7 TB NVMe storage, excluding parsing and output time.
That is an upstream benchmark, not a benchmark of this wrapper.

Upstream also provides [2026 TSAS reproducibility materials](https://github.com/ad-freiburg/spatialjoin/tree/geos-evaluation-v2/tsas-reproducibility)
and [SIGSPATIAL 2025 submission materials](https://github.com/ad-freiburg/spatialjoin/tree/use-libgeos-option/sigspatial-reproducibility-143).
