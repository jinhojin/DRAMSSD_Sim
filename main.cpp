#define FMT_HEADER_ONLY

#include <iostream>

#include "Sim.h"
#include "Trace.h"
#include "include/argparse.h"
#include "include/fmt/core.h"

double getMissRatio(const Stat &stat) {
  uint64_t numMisses = stat.numAccesses - stat.numHits;

  return static_cast<double>(numMisses) / stat.numAccesses * 100.0;
}

double getOverwrittenHitRatio(const Stat &stat) {
  uint64_t numFifoMisses = stat.numFifoAccesses - stat.numFifoHits;

  return static_cast<double>(stat.numFifoOverWrittenHits) / numFifoMisses *
         100.0;
}

int main(int argc, char **argv) {
  argparse::ArgumentParser program("issue_rates");

  program.add_argument("-f", "--file")
      .required()
      .nargs(argparse::nargs_pattern::any)
      .default_value("")
      .help("target directory containing trace files");
  program.add_argument("-dsize", "--dramsize")
      .required()
      .scan<'u', uint64_t>();
  program.add_argument("-fsize", "--fifosize")
      .required()
      .nargs(argparse::nargs_pattern::any)
      .scan<'u', uint64_t>();
  program.add_argument("-o", "--output")
      .default_value("./test.log")
      .help("output file");
  program.add_argument("-o", "--overwritten-log")
      .default_value("./overwritten.log")
      .help("output file");
  program.add_argument("-o", "--overwritten-acc-log")
      .default_value("./overwritten-acc.log")
      .help("output file");

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
    std::exit(1);
  }

  Trace trace(program.get<std::vector<std::string>>("--file"));

  Simulator sim(program.get<uint64_t>("--fifosize"),
                program.get<std::string>("--overwritten-log"),
                program.get<std::string>("--overwritten-acc-log"),
                program.get<uint64_t>("--dramsize"));

  std::ofstream log(program.get<std::string>("--output"),
                    std::ios::out | std::ios::trunc);
  log << fmt::format("numAccess,numHit,numDramAccess,numDramHit,"
                     "numFifoAccess,numFifoHit,numFifoOverWrittenHits")
      << std::endl;

  Trace::Entry e;
  const uint64_t statPrintInterval = 500000;
  Stat prevStat;
  while (trace.nextRequest(e)) {
    if (sim.getStat().numAccesses % statPrintInterval == 0) {
      const auto &curStat = sim.getStat();
      Stat mid = curStat - prevStat;
      double missRatio = getMissRatio(mid);
      double overwrittenHitRatio = getOverwrittenHitRatio(mid);

      std::cout << fmt::format(
                       "Miss ratio: {:.2f}, OverwrittenHitRatio: {:.2f}",
                       missRatio, overwrittenHitRatio)
                << std::endl;

      log << fmt::format("{},{},{},{},{},{},{}", curStat.numAccesses,
                         curStat.numHits, curStat.numDramAccesses,
                         curStat.numDramHits, curStat.numFifoAccesses,
                         curStat.numFifoHits, curStat.numFifoOverWrittenHits)
          << std::endl;

      prevStat = sim.getStat();
    }

    if (!e.isGet) {
      sim.remove(e.key);
      continue;
    }

    if (!sim.lookup(e.key)) {
      sim.insert(e.key, e.size);
    }
  }

  return 0;
}
