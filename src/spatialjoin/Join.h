#ifndef SPATIALJOIN_JOIN_H_
#define SPATIALJOIN_JOIN_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "util/geo/Geo.h"

namespace spatialjoin {

using Point = util::geo::I32Point;
using MultiPoint = util::geo::I32MultiPoint;
using Line = util::geo::I32Line;
using MultiLine = util::geo::I32MultiLine;
using Ring = util::geo::Ring<int32_t>;
using Polygon = util::geo::I32Polygon;
using MultiPolygon = util::geo::I32MultiPolygon;
using Box = util::geo::I32Box;

struct E7Point {
  int32_t longitude;
  int32_t latitude;
};

using E7Line = std::vector<E7Point>;

struct E7Polygon {
  E7Line outer;
  std::vector<E7Line> inners;
};

using E7MultiPolygon = std::vector<E7Polygon>;

enum class DatasetSide : uint8_t { Left, Right };
enum class JoinMode : uint8_t { Self, TwoSets };
enum class OutputMode : uint8_t { Relations, DE9IM, WithinDistance };
enum class Direction : uint8_t { Symmetric, LeftToRight, RightToLeft };

enum class Relation : uint8_t {
  Intersects,
  Contains,
  Covers,
  Touches,
  Equals,
  Overlaps,
  Crosses,
  WithinDistance,
  DE9IM
};

class RelationSet {
 public:
  RelationSet(std::initializer_list<Relation> relations);
  static RelationSet all();
  bool contains(Relation relation) const;

 private:
  uint16_t _bits = 0;
};

struct Match {
  uint64_t left_id = 0;
  uint64_t right_id = 0;
  Relation relation = Relation::Intersects;
  Direction direction = Direction::Symmetric;
  double distance_m = 0;
  std::array<char, 9> de9im{};
};

struct JoinOptions {
  JoinMode mode = JoinMode::Self;
  OutputMode output = OutputMode::Relations;
  RelationSet relations = RelationSet::all();
  double within_distance_m = -1;
  std::string de9im_filter;
  std::string cache_directory = ".";
  size_t worker_threads = 1;
  size_t cache_threads = 1;
  size_t cache_max_bytes = 64 * 1024 * 1024;
  size_t cache_max_elements = 10000;
  size_t batch_size = 8192;
  bool validate_ids = false;
  bool serialize_callbacks = false;
  bool use_box_ids = true;
  bool use_area = true;
  bool use_oriented_envelopes = true;
  bool use_diagonal_box = true;
  bool use_fast_sweep_skip = true;
  bool euclidean_distance = false;
  bool haversine_approximation = false;
};

struct JoinStatistics {
  uint64_t left_geometries = 0;
  uint64_t right_geometries = 0;
  uint64_t emitted_matches = 0;
};

class JoinSession {
 public:
  explicit JoinSession(JoinOptions options = {});
  ~JoinSession();

  JoinSession(const JoinSession&) = delete;
  JoinSession& operator=(const JoinSession&) = delete;
  JoinSession(JoinSession&&) noexcept;
  JoinSession& operator=(JoinSession&&) noexcept;

  static Point project(E7Point point);
  static Line projectLine(const E7Line& line);
  static Polygon projectPolygon(const E7Polygon& polygon);

  template <typename PointRange, typename ToE7>
  static Line projectLine(const PointRange& points, ToE7 to_e7) {
    Line result;
    result.reserve(points.size());
    for (const auto& point : points) result.push_back(project(to_e7(point)));
    validateProjectedLine(result);
    return result;
  }

  template <typename OuterRange, typename InnerRanges, typename ToE7>
  static Polygon projectPolygon(const OuterRange& outer,
                                const InnerRanges& inners, ToE7 to_e7) {
    Ring projected_outer = projectRing(outer, to_e7);
    std::vector<Ring> projected_inners;
    projected_inners.reserve(inners.size());
    for (const auto& inner : inners) {
      projected_inners.push_back(projectRing(inner, to_e7));
    }
    return Polygon(projected_outer, projected_inners);
  }

  void addPoint(DatasetSide side, uint64_t id, E7Point point);
  void addMultiPoint(DatasetSide side, uint64_t id,
                     const std::vector<E7Point>& points);
  void addLine(DatasetSide side, uint64_t id, const E7Line& line);

  template <typename PointRange, typename ToE7>
  void addLine(DatasetSide side, uint64_t id, const PointRange& points,
               ToE7 to_e7) {
    addNative(side, id, projectLine(points, to_e7));
  }

  void addMultiLine(DatasetSide side, uint64_t id,
                    const std::vector<E7Line>& lines);
  void addPolygon(DatasetSide side, uint64_t id, const E7Polygon& polygon);

  template <typename OuterRange, typename InnerRanges, typename ToE7>
  void addPolygon(DatasetSide side, uint64_t id, const OuterRange& outer,
                  const InnerRanges& inners, ToE7 to_e7) {
    addNative(side, id, projectPolygon(outer, inners, to_e7));
  }

  void addMultiPolygon(DatasetSide side, uint64_t id,
                       const E7MultiPolygon& polygons);

  void addNative(DatasetSide side, uint64_t id, const Point& point);
  void addNative(DatasetSide side, uint64_t id, const MultiPoint& points);
  void addNative(DatasetSide side, uint64_t id, const Line& line);
  void addNative(DatasetSide side, uint64_t id, const MultiLine& lines);
  void addNative(DatasetSide side, uint64_t id, const Polygon& polygon);
  void addNative(DatasetSide side, uint64_t id, const MultiPolygon& polygons);

  JoinStatistics run(const std::function<void(const Match&)>& callback);

 private:
  static void validateProjectedLine(const Line& line);
  static void validateProjectedRing(Ring& ring);

  template <typename PointRange, typename ToE7>
  static Ring projectRing(const PointRange& points, ToE7 to_e7) {
    Ring ring;
    ring.reserve(points.size());
    for (const auto& point : points) ring.push_back(project(to_e7(point)));
    validateProjectedRing(ring);
    return ring;
  }

  class Impl;
  std::unique_ptr<Impl> _impl;
};

}

#endif
