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
