/*
 * FtlFixture — shared setup for FTL tests (a "test fixture").
 *
 * Inherits from TestCase and is itself inherited by every concrete
 * test (see FTL_TEST in the test files): a three-level inheritance
 * chain that gives each test a fresh little device + FTL and a set
 * of helper methods, with zero copied code.
 *
 * The geometry is deliberately tiny (16 blocks x 8 pages x 64 bytes)
 * so garbage collection and wear effects appear within a few hundred
 * writes instead of millions.
 */
#pragma once

#include "../framework/check.h"
#include "../framework/test_case.h"
#include "../ftl/ftl.h"
#include "../nand/nand_device.h"

#include <memory>
#include <vector>

class FtlFixture : public framework::TestCase {
public:
    void setUp() override {
        nand::Geometry g;
        g.blocks        = 16;
        g.pagesPerBlock = 8;
        g.pageSize      = 64;
        g.eraseLimit    = 500;
        dev = std::make_unique<nand::NandDevice>(g);
        ftl = std::make_unique<ftl::Ftl>(*dev, /*spareBlocks=*/4);
        /* logical capacity: (16 - 4 spare) * 8 = 96 pages */
    }

    void tearDown() override {
        ftl.reset();
        dev.reset();
    }

protected:
    /* Deterministic page contents derived from a seed, so any test
     * can later verify a page without storing what it wrote. */
    std::vector<uint8_t> pattern(uint32_t seed) const {
        std::vector<uint8_t> d(dev->geometry().pageSize);
        for (size_t i = 0; i < d.size(); i++)
            d[i] = (uint8_t)(seed * 31 + i * 7);
        return d;
    }

    void writeLpn(uint32_t lpn, uint32_t seed) { ftl->write(lpn, pattern(seed)); }

    void expectLpn(uint32_t lpn, uint32_t seed) {
        REQUIRE(ftl->read(lpn) == pattern(seed));
    }

    std::unique_ptr<nand::NandDevice> dev;
    std::unique_ptr<ftl::Ftl>         ftl;
};
