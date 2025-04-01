#pragma once

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include "include/csv.h"
#include "include/fmt/core.h"

class Trace {
public:
  struct Entry {
    std::string key;
    std::string op;
    uint32_t size;
    uint32_t opCount;
    bool isGet;
  };
  Trace(const std::vector<std::string> &paths)
      : traceFilePaths(paths), recentEntry{"", "", 0, 0}, recentOpCount(0),
        traceFileIndex(0) {
    std::sort(std::begin(traceFilePaths), std::end(traceFilePaths));
    csvFile = std::make_unique<io::CSVReader<4>>(nextTraceFilePath().value());
    csvFile->read_header(io::ignore_extra_column, "key", "size", "op",
                         "op_count");
  }

  bool nextRequest(Entry &e) {
    if (recentOpCount > 0) {
      e = recentEntry;
      recentOpCount--;
      return true;
    }

    bool isValid = false;
    do {
      isValid = csvFile->read_row(e.key, e.size, e.op, e.opCount);
      assert(e.opCount > 0);
      if (isValid) {
        recentEntry = e;
        recentOpCount = e.opCount - 1;
      }
    } while (isValid && !isTargetRequest(e));

    if (!isValid) {
      if (auto nextFile = nextTraceFilePath()) {
        std::cout << fmt::format("Processing next file: {}",
                                 nextFile.value().string())
                  << std::endl;
        csvFile = std::make_unique<io::CSVReader<4>>(nextFile.value());
        csvFile->read_header(io::ignore_extra_column, "key", "size", "op",
                             "op_count");
        do {
          isValid = csvFile->read_row(e.key, e.size, e.op, e.opCount);
          assert(e.opCount > 0);
          if (isValid) {
            recentEntry = e;
            recentOpCount = e.opCount - 1;
          }
        } while (isValid && !isTargetRequest(e));
      }
    }
    return isValid;
  }

private:
  std::vector<std::string> traceFilePaths;
  std::unique_ptr<io::CSVReader<4>> csvFile;

  Entry recentEntry;
  uint32_t recentOpCount;

  uint32_t traceFileIndex;

  bool isTargetRequest(const Entry &e) const {
    return (e.op.front() == 'G' && e.size <= 2048) || e.op.front() == 'D';
  }

  std::optional<std::filesystem::path> nextTraceFilePath() {
    if (traceFileIndex < traceFilePaths.size()) {
      return std::make_optional(traceFilePaths[traceFileIndex++]);
    }
    return std::nullopt;
  }
};
