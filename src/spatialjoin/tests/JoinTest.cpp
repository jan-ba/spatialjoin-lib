#include <limits>
#include <stdexcept>
#include <vector>

#include "spatialjoin/Join.h"

using spatialjoin::DatasetSide;
using spatialjoin::Direction;
using spatialjoin::E7Point;
using spatialjoin::E7Polygon;
using spatialjoin::JoinMode;
using spatialjoin::JoinOptions;
using spatialjoin::JoinSession;
using spatialjoin::Match;
using spatialjoin::OutputMode;
using spatialjoin::Relation;
using spatialjoin::RelationSet;

void require(bool condition) {
  if (!condition) throw std::runtime_error("Join wrapper test failed");
}

E7Polygon square(int32_t min, int32_t max) {
  return {{{min, min}, {max, min}, {max, max}, {min, max}, {min, min}}, {}};
}

struct OwnPoint {
  int32_t lon_e7;
  int32_t lat_e7;
};

JoinOptions options() {
  JoinOptions result;
  result.cache_directory = "/tmp";
  result.worker_threads = 1;
  result.cache_threads = 1;
  result.validate_ids = true;
  result.serialize_callbacks = true;
  return result;
}

void testSelfJoin() {
  JoinOptions config = options();
  config.relations = RelationSet{Relation::Contains};
  JoinSession join(config);
  join.addPolygon(DatasetSide::Left, 1, square(0, 100000000));
  join.addPoint(DatasetSide::Left, 2, E7Point{20000000, 20000000});

  std::vector<Match> matches;
  auto stats = join.run([&matches](const Match& match) {
    matches.push_back(match);
  });
  require(stats.left_geometries == 2);
  require(stats.right_geometries == 0);
  require(matches.size() == 1);
  require(stats.emitted_matches == matches.size());
  require(matches[0].left_id == 1 && matches[0].right_id == 2);
  require(matches[0].relation == Relation::Contains);
  require(matches[0].direction == Direction::LeftToRight);

  bool rejected = false;
  try {
    join.addPoint(DatasetSide::Left, 3, E7Point{0, 0});
  } catch (const std::logic_error&) {
    rejected = true;
  }
  require(rejected);
}

void testTwoSetsAndReverseRelation() {
  JoinOptions config = options();
  config.mode = JoinMode::TwoSets;
  config.relations = RelationSet{Relation::Contains};
  JoinSession join(config);
  join.addPoint(DatasetSide::Left, 42, E7Point{20000000, 20000000});
  join.addPolygon(DatasetSide::Right, 42, square(0, 100000000));

  std::vector<Match> matches;
  join.run([&matches](const Match& match) { matches.push_back(match); });
  require(matches.size() == 1);
  require(matches[0].left_id == 42 && matches[0].right_id == 42);
  require(matches[0].relation == Relation::Contains);
  require(matches[0].direction == Direction::RightToLeft);
}

void testSymmetricAndNativeInput() {
  JoinOptions config = options();
  config.mode = JoinMode::TwoSets;
  config.relations = RelationSet{Relation::Intersects};
  JoinSession join(config);
  const uint64_t max_id = std::numeric_limits<uint64_t>::max();
  join.addNative(DatasetSide::Left, max_id,
                 JoinSession::project(E7Point{20000000, 20000000}));
  join.addPolygon(DatasetSide::Right, 2, square(0, 100000000));

  std::vector<Match> matches;
  join.run([&matches](const Match& match) { matches.push_back(match); });
  require(matches.size() == 1);
  require(matches[0].left_id == max_id && matches[0].right_id == 2);
  require(matches[0].relation == Relation::Intersects);
  require(matches[0].direction == Direction::Symmetric);
}

void testCustomPointAdapters() {
  JoinOptions config = options();
  config.mode = JoinMode::TwoSets;
  config.relations = RelationSet{Relation::Intersects};
  JoinSession join(config);
  const std::vector<OwnPoint> line{{20000000, 20000000},
                                   {80000000, 80000000}};
  const std::vector<OwnPoint> outer{{0, 0}, {100000000, 0},
                                    {100000000, 100000000}, {0, 100000000}};
  const std::vector<std::vector<OwnPoint>> inners;
  auto to_e7 = [](const OwnPoint& point) {
    return E7Point{point.lon_e7, point.lat_e7};
  };
  join.addLine(DatasetSide::Left, 1, line, to_e7);
  join.addPolygon(DatasetSide::Right, 2, outer, inners, to_e7);

  std::vector<Match> matches;
  join.run([&matches](const Match& match) { matches.push_back(match); });
  require(matches.size() == 1);
  require(matches[0].relation == Relation::Intersects);
}

void testPolygonHole() {
  JoinOptions config = options();
  config.mode = JoinMode::TwoSets;
  config.relations = RelationSet{Relation::Contains};
  E7Polygon polygon = square(0, 100000000);
  polygon.inners.push_back(square(40000000, 60000000).outer);

  JoinSession join(config);
  join.addPolygon(DatasetSide::Left, 1, polygon);
  join.addPoint(DatasetSide::Right, 2, E7Point{50000000, 50000000});
  join.addPoint(DatasetSide::Right, 3, E7Point{20000000, 20000000});

  std::vector<Match> matches;
  join.run([&matches](const Match& match) { matches.push_back(match); });
  require(matches.size() == 1);
  require(matches[0].left_id == 1 && matches[0].right_id == 3);
}

void testMultiPolygon() {
  JoinOptions config = options();
  config.mode = JoinMode::TwoSets;
  config.relations = RelationSet{Relation::Contains};
  JoinSession join(config);
  join.addMultiPolygon(DatasetSide::Left, 7,
                       {square(0, 10000000), square(40000000, 60000000)});
  join.addPoint(DatasetSide::Right, 8, E7Point{50000000, 50000000});

  std::vector<Match> matches;
  join.run([&matches](const Match& match) { matches.push_back(match); });
  require(matches.size() == 1);
  require(matches[0].left_id == 7 && matches[0].right_id == 8);
}

void testDE9IMAndCallbackError() {
  JoinOptions config = options();
  config.output = OutputMode::DE9IM;
  JoinSession join(config);
  join.addPolygon(DatasetSide::Left, 1, square(0, 100000000));
  join.addPoint(DatasetSide::Left, 2, E7Point{20000000, 20000000});

  bool saw_matrix = false;
  join.run([&saw_matrix](const Match& match) {
    saw_matrix = true;
    require(match.relation == Relation::DE9IM);
    require(match.de9im[0] >= '0' && match.de9im[0] <= '2');
  });
  require(saw_matrix);

  JoinSession throwing_join(options());
  throwing_join.addPolygon(DatasetSide::Left, 1, square(0, 100000000));
  throwing_join.addPoint(DatasetSide::Left, 2, E7Point{20000000, 20000000});
  bool propagated = false;
  try {
    throwing_join.run([](const Match&) {
      throw std::runtime_error("callback failed");
    });
  } catch (const std::runtime_error&) {
    propagated = true;
  }
  require(propagated);
}

void testDE9IMDirectionalFilter() {
  JoinOptions config = options();
  config.mode = JoinMode::TwoSets;
  config.output = OutputMode::DE9IM;
  config.de9im_filter = "T*****FF*";
  JoinSession join(config);
  join.addPoint(DatasetSide::Left, 1, E7Point{20000000, 20000000});
  join.addPolygon(DatasetSide::Right, 2, square(0, 100000000));

  std::vector<Match> matches;
  join.run([&matches](const Match& match) { matches.push_back(match); });
  require(matches.size() == 1);
  require(matches[0].left_id == 1 && matches[0].right_id == 2);
  require(matches[0].relation == Relation::DE9IM);
  require(matches[0].direction == Direction::RightToLeft);
  require(matches[0].de9im[0] == '0');
}

void testWithinDistance() {
  JoinOptions config = options();
  config.mode = JoinMode::TwoSets;
  config.output = OutputMode::WithinDistance;
  config.within_distance_m = 0;
  JoinSession join(config);
  join.addPoint(DatasetSide::Left, 1, E7Point{0, 0});
  join.addPoint(DatasetSide::Right, 2, E7Point{0, 0});

  std::vector<Match> matches;
  join.run([&matches](const Match& match) { matches.push_back(match); });
  require(matches.size() == 1);
  require(matches[0].relation == Relation::WithinDistance);
  require(matches[0].distance_m == 0);
}

void testValidation() {
  JoinSession join(options());
  join.addPoint(DatasetSide::Left, 1, E7Point{0, 0});
  bool duplicate_rejected = false;
  try {
    join.addPoint(DatasetSide::Left, 1, E7Point{0, 0});
  } catch (const std::invalid_argument&) {
    duplicate_rejected = true;
  }
  require(duplicate_rejected);

  bool side_rejected = false;
  try {
    join.addPoint(DatasetSide::Right, 2, E7Point{0, 0});
  } catch (const std::invalid_argument&) {
    side_rejected = true;
  }
  require(side_rejected);

  bool coordinate_rejected = false;
  try {
    JoinSession::project(E7Point{0, 900000001});
  } catch (const std::invalid_argument&) {
    coordinate_rejected = true;
  }
  require(coordinate_rejected);
}

int main() {
  testSelfJoin();
  testTwoSetsAndReverseRelation();
  testSymmetricAndNativeInput();
  testCustomPointAdapters();
  testPolygonHole();
  testMultiPolygon();
  testDE9IMAndCallbackError();
  testDE9IMDirectionalFilter();
  testWithinDistance();
  testValidation();
}
