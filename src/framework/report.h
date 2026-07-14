/*
 * Reporting — a console summary and a self-contained HTML report
 * (open it in any browser; no external files needed). This mirrors
 * what Jenkins-style CI produces after a firmware test run.
 */
#pragma once

#include "test_runner.h"

#include <string>
#include <vector>

namespace framework {

/* Prints the per-suite totals and the final verdict line.
 * Returns the number of failed tests (handy as an exit code). */
int printSummary(const std::vector<TestResult>& results);

/* Writes the HTML report. Returns true on success. */
bool writeHtmlReport(const std::vector<TestResult>& results,
                     const std::string& path,
                     const std::string& commandLine);

} // namespace framework
