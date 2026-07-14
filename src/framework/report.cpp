#include "report.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>

namespace framework {

int printSummary(const std::vector<TestResult>& results) {
    /* per-suite tallies */
    std::map<std::string, std::pair<int, int>> suites;   // suite -> {pass, fail}
    double totalMs = 0.0;
    for (const auto& r : results) {
        auto& t = suites[r.suite];
        (r.passed ? t.first : t.second)++;
        totalMs += r.millis;
    }

    std::printf("\n  %-24s %6s %6s\n", "suite", "pass", "fail");
    std::printf("  %-24s %6s %6s\n", "------------------------", "----", "----");
    int failed = 0;
    for (const auto& [suite, t] : suites) {
        std::printf("  %-24s %6d %6d\n", suite.c_str(), t.first, t.second);
        failed += t.second;
    }

    std::printf("\n  %zu test(s), %d failed, %.0f ms total\n",
                results.size(), failed, totalMs);
    std::printf("  RESULT: %s\n", failed == 0 ? "ALL TESTS PASSED"
                                              : "*** FAILURES ***");
    return failed;
}

/* minimal HTML escaping for failure messages */
static std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default:  out += c;
        }
    }
    return out;
}

bool writeHtmlReport(const std::vector<TestResult>& results,
                     const std::string& path,
                     const std::string& commandLine) {
    int pass = 0, fail = 0;
    double totalMs = 0.0;
    for (const auto& r : results) {
        (r.passed ? pass : fail)++;
        totalMs += r.millis;
    }

    char timestamp[64];
    std::time_t now = std::time(nullptr);
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",
                  std::localtime(&now));

    std::ostringstream h;
    h << "<!DOCTYPE html><html><head><meta charset='utf-8'>"
         "<title>FTLCheck report</title><style>"
         "body{font-family:Segoe UI,Arial,sans-serif;margin:0;background:#f4f6f9;color:#1c2733}"
         ".top{background:#102a43;color:#fff;padding:24px 32px}"
         ".top h1{margin:0 0 4px;font-size:22px}"
         ".top .sub{color:#9fb3c8;font-size:13px}"
         ".cards{display:flex;gap:16px;padding:24px 32px 8px;flex-wrap:wrap}"
         ".card{background:#fff;border-radius:10px;padding:14px 22px;min-width:120px;"
         "box-shadow:0 1px 3px rgba(16,42,67,.12)}"
         ".card .num{font-size:26px;font-weight:700}"
         ".card .lbl{font-size:12px;color:#627d98;text-transform:uppercase;letter-spacing:.5px}"
         ".ok .num{color:#0f9d58}.bad .num{color:#d93025}"
         "table{border-collapse:collapse;margin:16px 32px 40px;background:#fff;"
         "border-radius:10px;overflow:hidden;box-shadow:0 1px 3px rgba(16,42,67,.12);min-width:640px}"
         "th{background:#243b53;color:#fff;text-align:left;padding:10px 16px;font-size:13px}"
         "td{padding:9px 16px;border-top:1px solid #e5eaf0;font-size:14px}"
         ".suite{color:#627d98;font-size:12px}"
         ".badge{display:inline-block;padding:2px 10px;border-radius:12px;"
         "font-size:12px;font-weight:600;color:#fff}"
         ".badge.pass{background:#0f9d58}.badge.fail{background:#d93025}"
         ".msg{color:#b23c17;font-family:Consolas,monospace;font-size:12px}"
         "</style></head><body>"
      << "<div class='top'><h1>FTLCheck &mdash; SSD firmware validation report</h1>"
      << "<div class='sub'>generated " << timestamp
      << " &nbsp;|&nbsp; command: <code>" << escape(commandLine) << "</code></div></div>"
      << "<div class='cards'>"
      << "<div class='card'><div class='num'>" << results.size()
      << "</div><div class='lbl'>tests run</div></div>"
      << "<div class='card ok'><div class='num'>" << pass
      << "</div><div class='lbl'>passed</div></div>"
      << "<div class='card bad'><div class='num'>" << fail
      << "</div><div class='lbl'>failed</div></div>"
      << "<div class='card'><div class='num'>" << (int)totalMs
      << "<span style='font-size:14px'> ms</span></div><div class='lbl'>total time</div></div>"
      << "</div>"
      << "<table><tr><th>Test</th><th>Result</th><th>Time</th><th>Details</th></tr>";

    for (const auto& r : results) {
        h << "<tr><td><div>" << escape(r.name) << "</div><div class='suite'>"
          << escape(r.suite) << "</div></td>"
          << "<td><span class='badge " << (r.passed ? "pass'>PASS" : "fail'>FAIL")
          << "</span></td>"
          << "<td>" << (int)(r.millis + 0.5) << " ms</td>"
          << "<td class='msg'>" << escape(r.message) << "</td></tr>";
    }
    h << "</table></body></html>";

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << h.str();
    return (bool)out;
}

} // namespace framework
