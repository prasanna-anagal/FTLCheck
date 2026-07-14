/*
 * FTLCheck CLI — entry point.
 *
 *   ftlcheck list                    show every registered test
 *   ftlcheck run [options]           run tests
 *       --suite <name>               only this suite
 *       --test <name>                only this test
 *       --report <file.html>         also write an HTML report
 *   ftlcheck demo                    narrated walkthrough of the FTL
 */
#include "framework/report.h"
#include "framework/test_runner.h"
#include "ftl/ftl.h"
#include "nand/nand_device.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef FTL_WITH_MEMGUARD
#include "../../memguard/src/memguard.h"
#endif

static void printUsage() {
    std::printf(
        "FTLCheck - SSD flash translation layer + validation framework\n"
        "\n"
        "usage:\n"
        "  ftlcheck list                     list all registered tests\n"
        "  ftlcheck run [--suite S] [--test T] [--report out.html]\n"
        "  ftlcheck demo                     narrated FTL walkthrough\n");
}

static int cmdList() {
    const auto& tests = framework::TestRegistry::instance().tests();
    std::string lastSuite;
    for (const auto& t : tests) {
        if (t.suite != lastSuite) {
            std::printf("%s\n", t.suite.c_str());
            lastSuite = t.suite;
        }
        std::printf("    %s\n", t.name.c_str());
    }
    std::printf("\n%zu test(s) registered\n", tests.size());
    return 0;
}

static int cmdRun(int argc, char** argv) {
    std::string suite, test, reportPath, commandLine = "ftlcheck run";

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        commandLine += " " + arg;
        if (arg == "--suite" && i + 1 < argc) {
            suite = argv[++i];
            commandLine += std::string(" ") + argv[i];
        } else if (arg == "--test" && i + 1 < argc) {
            test = argv[++i];
            commandLine += std::string(" ") + argv[i];
        } else if (arg == "--report" && i + 1 < argc) {
            reportPath = argv[++i];
            commandLine += std::string(" ") + argv[i];
        } else {
            std::printf("unknown option: %s\n\n", arg.c_str());
            printUsage();
            return 2;
        }
    }

    std::printf("FTLCheck test run\n\n");
    auto results = framework::TestRunner::instance().run(suite, test);
    if (results.empty()) {
        std::printf("no tests matched the given filters\n");
        return 2;
    }

    int failed = framework::printSummary(results);

    if (!reportPath.empty()) {
        if (framework::writeHtmlReport(results, reportPath, commandLine))
            std::printf("  HTML report written to %s\n", reportPath.c_str());
        else
            std::printf("  could not write report to %s\n", reportPath.c_str());
    }
    return failed == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* demo — a narrated tour of what the FTL does                         */
/* ------------------------------------------------------------------ */

static std::vector<uint8_t> demoPattern(const nand::Geometry& g, uint32_t seed) {
    std::vector<uint8_t> d(g.pageSize);
    for (size_t i = 0; i < d.size(); i++) d[i] = (uint8_t)(seed * 31 + i * 7);
    return d;
}

static int runDemo() {
    nand::Geometry g;
    g.blocks = 16; g.pagesPerBlock = 8; g.pageSize = 64; g.eraseLimit = 500;
    nand::NandDevice dev(g);
    ftl::Ftl drive(dev, /*spareBlocks=*/4);

    std::printf("=============================================================\n");
    std::printf(" FTLCheck demo: a tiny SSD, end to end\n");
    std::printf("=============================================================\n");
    std::printf("device: %u blocks x %u pages x %u bytes  |  host sees %u pages\n",
                g.blocks, g.pagesPerBlock, g.pageSize, drive.logicalPages());
    std::printf("(4 spare blocks are hidden from the host: overprovisioning)\n");

    /* 1 — basic write/read and the mapping */
    std::printf("\n--- 1. write / read, and where data really lands ---------\n");
    drive.write(5, demoPattern(g, 42));
    auto phys = drive.debugPhysical(5);
    std::printf("host wrote logical page 5 -> FTL placed it at physical "
                "block %u, page %u\n", phys.first, phys.second);

    /* 2 — overwrite = write elsewhere + remap */
    std::printf("\n--- 2. 'overwrite' on flash is really a remap ------------\n");
    drive.write(5, demoPattern(g, 43));
    auto phys2 = drive.debugPhysical(5);
    std::printf("host overwrote page 5   -> new copy at block %u, page %u\n",
                phys2.first, phys2.second);
    std::printf("the old copy at block %u page %u is now stale garbage\n",
                phys.first, phys.second);

    /* 3 — write pressure forces garbage collection */
    std::printf("\n--- 3. write pressure -> garbage collection --------------\n");
    for (uint32_t round = 0; round < 40; round++)
        for (uint32_t lpn = 0; lpn < 8; lpn++)
            drive.write(lpn, demoPattern(g, round + lpn));
    auto s = drive.stats();
    std::printf("320 host writes on a 128-page chip forced the FTL to:\n");
    std::printf("  garbage collection runs : %llu\n",
                (unsigned long long)s.gcRuns);
    std::printf("  live pages relocated    : %llu\n",
                (unsigned long long)s.pagesRelocated);
    std::printf("  nand writes / host writes (write amplification): %.2f\n",
                s.waf());

    /* 4 — wear leveling table */
    std::printf("\n--- 4. wear leveling: erases spread across blocks --------\n");
    std::printf("block : state (F ree/A ctive/f U ll/B ad) : erase count\n");
    for (uint32_t b = 0; b < g.blocks; b++) {
        std::printf("  %2u  :   %c   : %2u ", b, drive.blockStateChar(b),
                    dev.eraseCount(b));
        for (uint32_t i = 0; i < dev.eraseCount(b); i++) std::printf("|");
        std::printf("\n");
    }
    std::printf("spread: min %u, max %u erases — no block is being "
                "worn out alone\n", dev.minEraseCount(), dev.maxEraseCount());

    /* 5 — power loss and recovery */
    std::printf("\n--- 5. power loss at the worst moment ---------------------\n");
    drive.write(90, demoPattern(g, 90));
    drive.write(91, demoPattern(g, 91));
    std::printf("host wrote logical pages 90 and 91 (acknowledged)\n");
    dev.injectPowerLossAfter(0);
    std::printf("injecting power loss on the very next program operation...\n");
    try {
        drive.write(92, demoPattern(g, 92));
    } catch (const nand::PowerLoss& e) {
        std::printf("  -> %s (write of page 92 was NOT acknowledged)\n", e.what());
    }
    std::printf("simulating reboot: all RAM state gone, recovering from "
                "the OOB metadata on flash...\n");
    drive.recover();
    bool ok90 = drive.read(90) == demoPattern(g, 90);
    bool ok91 = drive.read(91) == demoPattern(g, 91);
    std::printf("  page 90 after recovery: %s\n", ok90 ? "intact" : "LOST");
    std::printf("  page 91 after recovery: %s\n", ok91 ? "intact" : "LOST");
    try {
        drive.read(92);
        std::printf("  page 92: unexpectedly present?!\n");
    } catch (const ftl::NotWritten&) {
        std::printf("  page 92 after recovery: correctly absent (it was "
                    "never acknowledged)\n");
    }

    /* 6 — bit error detection */
    std::printf("\n--- 6. silent data corruption is refused -------------------\n");
    auto where = drive.debugPhysical(90);
    dev.injectBitFlip(where.first, where.second, 13);
    std::printf("flipped one stored bit of logical page 90 (block %u, page %u)\n",
                where.first, where.second);
    try {
        drive.read(90);
        std::printf("  read returned corrupted data — BAD\n");
    } catch (const ftl::DataCorruption& e) {
        std::printf("  read refused: %s\n", e.what());
    }

    std::printf("\n=============================================================\n");
    std::printf(" demo complete — run 'ftlcheck run' to see the full test "
                "suite prove\n each of these behaviours automatically.\n");
    std::printf("=============================================================\n");
    return 0;
}

int main(int argc, char** argv) {
    int rc = 2;
    if (argc < 2) {
        printUsage();
    } else if (std::strcmp(argv[1], "list") == 0) {
        rc = cmdList();
    } else if (std::strcmp(argv[1], "run") == 0) {
        rc = cmdRun(argc, argv);
    } else if (std::strcmp(argv[1], "demo") == 0) {
        rc = runDemo();
    } else {
        printUsage();
    }

#ifdef FTL_WITH_MEMGUARD
    /* This build routes every C++ new/delete through the MemGuard
     * allocator (project 2). Objects with static lifetime are still
     * alive here, so they appear in the report by design. */
    mg_print_stats();
    mg_report_leaks();
#endif
    return rc;
}
