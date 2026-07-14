/* Suite: bad_blocks — factory-bad blocks must never be touched, and
 * blocks that fail during use must be retired without data loss. */
#include "fixture.h"
#include "../framework/test_registry.h"

FTL_TEST(bad_blocks, FactoryBadBlockIsNeverUsed, FtlFixture) {
    /* build a custom device: block 3 is bad from the factory */
    nand::Geometry g;
    g.blocks = 16; g.pagesPerBlock = 8; g.pageSize = 64; g.eraseLimit = 500;
    nand::NandDevice d(g);
    d.markFactoryBad(3);
    ftl::Ftl f(d, 4);

    /* capacity must shrink accordingly: (16-1-4)*8 = 88 pages */
    REQUIRE_EQ(f.logicalPages(), 88u);

    /* hammer the drive; block 3 must remain untouched */
    std::vector<uint8_t> page(g.pageSize);
    for (uint32_t round = 0; round < 30; round++)
        for (uint32_t lpn = 0; lpn < 8; lpn++) {
            for (auto& byte : page) byte = (uint8_t)(round + lpn);
            f.write(lpn, page);
        }

    REQUIRE_EQ(d.eraseCount(3), 0u);
    REQUIRE_EQ(d.programmedPages(3), 0u);
}

FTL_TEST(bad_blocks, GrownBadBlockRetiredWithoutDataLoss, FtlFixture) {
    /* cold data first: it settles into blocks 0..2, which GC never
     * victimises (their pages stay valid) */
    for (uint32_t lpn = 30; lpn < 50; lpn++)
        writeLpn(lpn, lpn);

    /* blocks 3..5 will hold churning hot data — they WILL become GC
     * victims, and each will fail its erase and grow bad mid-run */
    dev->injectEraseFailure(3);
    dev->injectEraseFailure(4);
    dev->injectEraseFailure(5);

    for (int round = 0; round < 40; round++)    /* force GC activity */
        for (uint32_t lpn = 0; lpn < 6; lpn++)
            writeLpn(lpn, (uint32_t)round);

    REQUIRE(ftl->stats().gcRuns > 0);
    REQUIRE(dev->badBlockCount() >= 1);         /* blocks did fail... */

    for (uint32_t lpn = 30; lpn < 50; lpn++)    /* ...but data survived */
        expectLpn(lpn, lpn);
    for (uint32_t lpn = 0; lpn < 6; lpn++)
        expectLpn(lpn, 39);
}

FTL_TEST(bad_blocks, WornOutBlockGoesBadAtEraseLimit, FtlFixture) {
    /* drive the device model directly: a block erased past its limit
     * must fail and be marked bad — the physics the FTL guards against */
    nand::Geometry g;
    g.blocks = 2; g.pagesPerBlock = 2; g.pageSize = 16; g.eraseLimit = 10;
    nand::NandDevice d(g);

    for (uint32_t i = 0; i < g.eraseLimit; i++)
        d.eraseBlock(0);
    REQUIRE_THROWS_AS(d.eraseBlock(0), nand::EraseError);
    REQUIRE(d.isBad(0));
    REQUIRE_EQ(d.badBlockCount(), 1u);
}
