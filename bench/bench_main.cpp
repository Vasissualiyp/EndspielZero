// SPDX-License-Identifier: GPL-3.0-or-later
// Benchmarks with committed baselines. The numbers matter less than the deltas:
// CI compares against bench/baselines/ and fails an unexplained regression.
//
// Deliberately plain: no framework, no statistics beyond a best-of-N, because a
// benchmark that is hard to run does not get run.
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "io/fen.hpp"
#include "movegen/movegen.hpp"
#include "variant/standard.hpp"

using namespace cb;

namespace {

using Clock = std::chrono::steady_clock;

struct Result {
  std::string name;
  std::uint64_t units{0};
  double microseconds{0};
};

std::vector<Result> g_results;

template <class F>
void measure(const std::string& name, int repeats, F&& f) {
  double best = 0;
  std::uint64_t units = 0;
  for (int i = 0; i < repeats; ++i) {
    const auto t0 = Clock::now();
    units = f();
    const auto us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
    if (best == 0 || us < best) best = us;
  }
  g_results.push_back(Result{name, units, best});
  const double rate = best > 0 ? static_cast<double>(units) / (best / 1e6) : 0.0;
  std::printf("%-34s %12llu units %10.1f ms  %12.0f units/s\n", name.c_str(),
              static_cast<unsigned long long>(units), best / 1000.0, rate);
}

}  // namespace

int main() {
  const auto standard = makeStandardChess();
  if (!standard.has_value()) {
    std::fprintf(stderr, "cannot build standard chess: %s\n",
                 standard.error().format().c_str());
    return 1;
  }
  const MoveGen gen(*standard);

  std::printf("EndspielZero benchmarks\n");
  std::printf("%-34s %12s       %10s  %12s\n", "benchmark", "work", "best", "rate");

  {
    Position p = Position::startPosition(*standard);
    measure("perft(4) initial", 3, [&] { return gen.perft(p, 4); });
  }
  {
    auto p =
        fromFen(*standard,
                "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    measure("perft(4) kiwipete", 3, [&] { return gen.perft(*p, 4); });
  }
  {
    // Pure generation throughput, with legality and make/unmake excluded.
    Position p = Position::startPosition(*standard);
    MoveList out(standard->moveUpperBound());
    measure("movegen calls, initial", 5, [&] {
      std::uint64_t n = 0;
      for (int i = 0; i < 200000; ++i) {
        out.clear();
        gen.generatePseudoLegal(p, out);
        n += out.size();
      }
      return n;
    });
  }
  {
    Position p = Position::startPosition(*standard);
    measure("isAttacked calls", 5, [&] {
      std::uint64_t n = 0;
      for (int i = 0; i < 200000; ++i) {
        for (CellId c = 0; c < 8; ++c) n += gen.isAttacked(p, c, Color::Black) ? 1u : 0u;
      }
      (void)n;
      return std::uint64_t{200000} * 8;
    });
  }

  std::printf(
      "\nRecord these with the cb-bench-baseline skill, including CPU model and\n"
      "governor; a number without its machine is not comparable.\n");
  return 0;
}
