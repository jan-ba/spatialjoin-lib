#include "Join.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "Sweeper.h"

namespace spatialjoin {

namespace {

bool isSymmetric(Relation relation) {
  return relation == Relation::Intersects || relation == Relation::Touches ||
         relation == Relation::Equals || relation == Relation::Overlaps ||
         relation == Relation::Crosses || relation == Relation::WithinDistance;
}

Relation decodeRelation(const char* predicate, size_t length) {
  if (length != 1) throw std::runtime_error("Invalid relation from sweeper");
  switch (predicate[0]) {
    case 1: return Relation::Intersects;
    case 2: return Relation::Contains;
    case 3: return Relation::Covers;
    case 4: return Relation::Touches;
    case 5: return Relation::Equals;
    case 6: return Relation::Overlaps;
    case 7: return Relation::Crosses;
    default: throw std::runtime_error("Unknown relation from sweeper");
  }
}

uint64_t decodeId(const char* data, size_t length) {
  if (length < 2 || (data[0] != 'L' && data[0] != 'R')) {
    throw std::runtime_error("Invalid geometry ID from sweeper");
  }
  uint64_t value = 0;
  for (size_t index = 1; index < length; ++index) {
    if (data[index] < '0' || data[index] > '9') {
      throw std::runtime_error("Invalid geometry ID from sweeper");
    }
    unsigned digit = static_cast<unsigned>(data[index] - '0');
    if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
      throw std::runtime_error("Geometry ID overflow");
    }
    value = value * 10 + digit;
  }
  return value;
}

void validateOptions(const JoinOptions& options) {
  if (options.worker_threads == 0 || options.cache_threads == 0 ||
      options.batch_size == 0 || options.cache_directory.empty()) {
    throw std::invalid_argument("Invalid join thread, batch, or cache setting");
  }
  if (options.output == OutputMode::WithinDistance &&
      (!std::isfinite(options.within_distance_m) ||
       options.within_distance_m < 0)) {
    throw std::invalid_argument("Within-distance mode needs a nonnegative distance");
  }
  if (options.output != OutputMode::DE9IM && !options.de9im_filter.empty()) {
    throw std::invalid_argument("DE-9IM filter requires DE-9IM output mode");
  }
  if (!options.de9im_filter.empty()) {
    if (options.de9im_filter.size() != 9) {
      throw std::invalid_argument("DE-9IM filter must have nine characters");
    }
    for (char value : options.de9im_filter) {
      if (value != '*' && value != 'T' && value != 'F' &&
          value != 't' && value != 'f' &&
          value != '0' && value != '1' && value != '2') {
        throw std::invalid_argument("Invalid DE-9IM filter character");
      }
    }
  }
}

}

RelationSet::RelationSet(std::initializer_list<Relation> relations) {
  for (Relation relation : relations) {
    _bits |= static_cast<uint16_t>(1u << static_cast<unsigned>(relation));
  }
}

RelationSet RelationSet::all() {
  return {Relation::Intersects, Relation::Contains, Relation::Covers,
          Relation::Touches, Relation::Equals, Relation::Overlaps,
          Relation::Crosses};
}

bool RelationSet::contains(Relation relation) const {
  return (_bits & static_cast<uint16_t>(1u << static_cast<unsigned>(relation))) != 0;
}

class JoinSession::Impl {
 public:
  explicit Impl(JoinOptions value) : options(std::move(value)) {
    validateOptions(options);
    if (!options.de9im_filter.empty()) {
      de9im_filter = util::geo::DE9IMFilter(options.de9im_filter.c_str());
    }
    sj::SweeperCfg config{options.worker_threads,
                          options.cache_threads,
                          options.cache_max_bytes,
                          options.cache_max_elements,
                          std::string(1, '\x01'),
                          std::string(1, '\x02'),
                          std::string(1, '\x03'),
                          std::string(1, '\x04'),
                          std::string(1, '\x05'),
                          std::string(1, '\x06'),
                          std::string(1, '\x07'),
                          options.use_box_ids,
                          options.use_area,
                          options.use_oriented_envelopes,
                          options.use_diagonal_box,
                          options.use_fast_sweep_skip,
                          false,
                          options.output == OutputMode::WithinDistance
                              ? options.within_distance_m : -1,
                          options.euclidean_distance,
                          options.haversine_approximation,
                          options.output == OutputMode::DE9IM,
                          util::geo::DE9IMFilter{},
                          options.mode == JoinMode::TwoSets,
                          [this](size_t, const char* first, size_t first_length,
                                 const char* second, size_t second_length,
                                 const char* predicate, size_t predicate_length) {
                            dispatch(first, first_length, second, second_length,
                                     predicate, predicate_length);
                          },
                          {}, {}, {}, {}};
    config.emitReverseRelationsInTwoSidedJoin = true;
    sweeper.reset(new sj::Sweeper(std::move(config), options.cache_directory));
  }

  template <typename Geometry>
  void add(DatasetSide side, uint64_t id, const Geometry& geometry) {
    if (state != State::Ingesting) {
      throw std::logic_error("Cannot add geometry after run()");
    }
    if (options.mode == JoinMode::Self && side != DatasetSide::Left) {
      throw std::invalid_argument("Self join accepts only the left dataset");
    }
    const size_t side_index = side == DatasetSide::Right ? 1 : 0;
    if (options.validate_ids && ids[side_index].count(id)) {
      throw std::invalid_argument("Duplicate geometry ID on one dataset side");
    }
    const std::string internal_id =
        std::string(1, side == DatasetSide::Right ? 'R' : 'L') +
        std::to_string(id);
    Box box = sweeper->add(geometry, internal_id,
                           side == DatasetSide::Right, batch);
    if (box.isNull()) {
      throw std::invalid_argument("Geometry produced no sweep events");
    }
    if (options.validate_ids) ids[side_index].insert(id);
    if (side == DatasetSide::Right) ++statistics.right_geometries;
    else ++statistics.left_geometries;
    if (++pending >= options.batch_size) flushBatch();
  }

  void flushBatch() {
    if (pending == 0) return;
    sweeper->addBatch(batch);
    batch = sj::WriteBatch{};
    pending = 0;
  }

  void dispatch(const char* first, size_t first_length, const char* second,
                size_t second_length, const char* predicate,
                size_t predicate_length) {
    if (callback_failed.load()) return;
    try {
      Match match;
      bool reverse = options.mode == JoinMode::TwoSets && first[0] == 'R';
      if (options.mode == JoinMode::TwoSets) {
        if (first[0] == second[0]) return;
        if (reverse) {
          match.left_id = decodeId(second, second_length);
          match.right_id = decodeId(first, first_length);
        } else {
          match.left_id = decodeId(first, first_length);
          match.right_id = decodeId(second, second_length);
        }
      } else {
        match.left_id = decodeId(first, first_length);
        match.right_id = decodeId(second, second_length);
      }

      if (options.output == OutputMode::Relations) {
        match.relation = decodeRelation(predicate, predicate_length);
        if (!options.relations.contains(match.relation)) return;
        if (isSymmetric(match.relation) && reverse) return;
        match.direction = isSymmetric(match.relation)
                              ? Direction::Symmetric
                              : reverse ? Direction::RightToLeft
                                        : Direction::LeftToRight;
      } else if (options.output == OutputMode::DE9IM) {
        if (predicate_length != 11 || predicate[0] != '\t' ||
            predicate[10] != '\t') {
          throw std::runtime_error("Invalid DE-9IM result from sweeper");
        }
        match.relation = Relation::DE9IM;
        match.direction = reverse ? Direction::RightToLeft
                                  : Direction::LeftToRight;
        for (size_t index = 0; index < 9; ++index) {
          match.de9im[index] = predicate[index + 1];
        }
        if (!options.de9im_filter.empty() &&
            !de9im_filter.matches(util::geo::DE9IMatrix(
                std::string(predicate + 1, 9)))) return;
      } else {
        if (reverse) return;
        if (predicate_length < 3 || predicate[0] != '\t' ||
            predicate[predicate_length - 1] != '\t') {
          throw std::runtime_error("Invalid distance result from sweeper");
        }
        std::string distance(predicate + 1, predicate_length - 2);
        char* end = nullptr;
        match.distance_m = std::strtod(distance.c_str(), &end);
        if (end != distance.c_str() + distance.size()) {
          throw std::runtime_error("Invalid distance value from sweeper");
        }
        match.relation = Relation::WithinDistance;
        match.direction = Direction::Symmetric;
      }

      if (options.serialize_callbacks) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        if (callback_failed.load()) return;
        if (callback) callback(match);
      } else if (callback) {
        callback(match);
      }
      emitted.fetch_add(1);
    } catch (...) {
      std::lock_guard<std::mutex> lock(error_mutex);
      if (!callback_failed.load()) {
        callback_error = std::current_exception();
        callback_failed.store(true);
      }
    }
  }

  JoinStatistics run(const std::function<void(const Match&)>& handler) {
    if (state != State::Ingesting) {
      throw std::logic_error("Join session can run only once");
    }
    state = State::Running;
    callback = handler;
    flushBatch();
    sweeper->flush();
    sweeper->sweep();
    state = State::Finished;
    if (callback_error) std::rethrow_exception(callback_error);
    statistics.emitted_matches = emitted.load();
    return statistics;
  }

  enum class State { Ingesting, Running, Finished };
  JoinOptions options;
  util::geo::DE9IMFilter de9im_filter;
  std::unique_ptr<sj::Sweeper> sweeper;
  sj::WriteBatch batch;
  size_t pending = 0;
  State state = State::Ingesting;
  std::unordered_set<uint64_t> ids[2];
  std::function<void(const Match&)> callback;
  std::mutex callback_mutex;
  std::mutex error_mutex;
  std::atomic<bool> callback_failed{false};
  std::exception_ptr callback_error;
  std::atomic<uint64_t> emitted{0};
  JoinStatistics statistics;
};

JoinSession::JoinSession(JoinOptions options)
    : _impl(new Impl(std::move(options))) {}

JoinSession::~JoinSession() = default;
JoinSession::JoinSession(JoinSession&&) noexcept = default;
JoinSession& JoinSession::operator=(JoinSession&&) noexcept = default;

Point JoinSession::project(E7Point point) {
  if (point.longitude < -1800000000 || point.longitude > 1800000000 ||
      point.latitude < -900000000 || point.latitude > 900000000) {
    throw std::invalid_argument("E7 point is outside WGS84 coordinate bounds");
  }
  const auto projected = util::geo::latLngToWebMerc<double>(
      point.latitude / 10000000.0, point.longitude / 10000000.0);
  const double east = projected.getX() * PREC;
  const double north = projected.getY() * PREC;
  if (!std::isfinite(east) || !std::isfinite(north) ||
      east < std::numeric_limits<int32_t>::min() ||
      east > std::numeric_limits<int32_t>::max() ||
      north < std::numeric_limits<int32_t>::min() ||
      north > std::numeric_limits<int32_t>::max()) {
    throw std::out_of_range("Projected point exceeds SpatialJoin coordinate range");
  }
  return {static_cast<int32_t>(east), static_cast<int32_t>(north)};
}

Line JoinSession::projectLine(const E7Line& points) {
  return projectLine(points, [](E7Point point) { return point; });
}

void JoinSession::validateProjectedLine(const Line& line) {
  if (line.size() < 2) throw std::invalid_argument("Line needs two points");
  bool distinct = false;
  for (size_t index = 1; index < line.size(); ++index) {
    if (line[index] != line[0]) distinct = true;
  }
  if (!distinct) {
    throw std::invalid_argument("Line collapses to one point after projection");
  }
}

void JoinSession::validateProjectedRing(Ring& ring) {
  if (ring.size() > 1 && ring.front() == ring.back()) ring.pop_back();
  if (ring.size() < 3 || util::geo::signedRingArea(ring) == 0) {
    throw std::invalid_argument("Polygon ring has no area after projection");
  }
}

Polygon JoinSession::projectPolygon(const E7Polygon& polygon) {
  return projectPolygon(polygon.outer, polygon.inners,
                        [](E7Point point) { return point; });
}

void JoinSession::addPoint(DatasetSide side, uint64_t id, E7Point point) {
  addNative(side, id, project(point));
}

void JoinSession::addMultiPoint(DatasetSide side, uint64_t id,
                                const std::vector<E7Point>& points) {
  if (points.empty()) throw std::invalid_argument("MultiPoint is empty");
  MultiPoint native;
  native.reserve(points.size());
  for (const auto& point : points) native.push_back(project(point));
  addNative(side, id, native);
}

void JoinSession::addLine(DatasetSide side, uint64_t id, const E7Line& line) {
  addNative(side, id, projectLine(line));
}

void JoinSession::addMultiLine(DatasetSide side, uint64_t id,
                               const std::vector<E7Line>& lines) {
  if (lines.empty()) throw std::invalid_argument("MultiLine is empty");
  MultiLine native;
  native.reserve(lines.size());
  for (const auto& line : lines) native.push_back(projectLine(line));
  addNative(side, id, native);
}

void JoinSession::addPolygon(DatasetSide side, uint64_t id,
                             const E7Polygon& polygon) {
  addNative(side, id, projectPolygon(polygon));
}

void JoinSession::addMultiPolygon(DatasetSide side, uint64_t id,
                                  const E7MultiPolygon& polygons) {
  if (polygons.empty()) throw std::invalid_argument("MultiPolygon is empty");
  MultiPolygon native;
  native.reserve(polygons.size());
  for (const auto& polygon : polygons) {
    native.push_back(projectPolygon(polygon));
  }
  addNative(side, id, native);
}

void JoinSession::addNative(DatasetSide side, uint64_t id, const Point& value) {
  _impl->add(side, id, value);
}

void JoinSession::addNative(DatasetSide side, uint64_t id, const MultiPoint& value) {
  if (value.empty()) throw std::invalid_argument("MultiPoint is empty");
  _impl->add(side, id, value);
}

void JoinSession::addNative(DatasetSide side, uint64_t id, const Line& value) {
  if (value.size() < 2) throw std::invalid_argument("Line needs two points");
  _impl->add(side, id, value);
}

void JoinSession::addNative(DatasetSide side, uint64_t id, const MultiLine& value) {
  if (value.empty()) throw std::invalid_argument("MultiLine is empty");
  _impl->add(side, id, value);
}

void JoinSession::addNative(DatasetSide side, uint64_t id, const Polygon& value) {
  if (value.empty()) throw std::invalid_argument("Polygon is empty");
  _impl->add(side, id, value);
}

void JoinSession::addNative(DatasetSide side, uint64_t id, const MultiPolygon& value) {
  if (value.empty()) throw std::invalid_argument("MultiPolygon is empty");
  _impl->add(side, id, value);
}

JoinStatistics JoinSession::run(
    const std::function<void(const Match&)>& callback) {
  return _impl->run(callback);
}

}
