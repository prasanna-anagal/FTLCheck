/* Suite: garbage_collection — sustained overwrites must trigger GC,
 * and GC must never lose or corrupt data while reclaiming space. */
#include "fixture.h"
#include "../framework/test_registry.h"

FTL_TEST(garbage_collection, GcTriggersUnderWritePressure, FtlFixture) {
    /* 8 hot pages overwritten 40 times each = 320 writes into a
     * 128-page device — impossible without garbage collection */
    for (int round = 0; round < 40; round++)
        for (uint32_t lpn = 0; lpn < 8; lpn++)
            writeLpn(lpn, (uint32_t)round);

    REQUIRE(ftl->stats().gcRuns > 0);
    for (uint32_t lpn = 0; lpn < 8; lpn++)
        expectLpn(lpn, 39);            /* latest round must win */
}

FTL_TEST(garbage_collection, ColdDataSurvivesGc, FtlFixture) {
    /* cold data: written once, then never touched again */
    for (uint32_t lpn = 20; lpn < 60; lpn++)
        writeLpn(lpn, lpn + 500);

    /* hot data: hammer 4 pages until GC has run several times —
     * GC must RELOCATE the cold pages without corrupting them */
    for (int round = 0; round < 60; round++)
        for (uint32_t lpn = 0; lpn < 4; lpn++)
            writeLpn(lpn, (uint32_t)round);
    REQUIRE(ftl->stats().gcRuns >= 3);

    for (uint32_t lpn = 20; lpn < 60; lpn++)
        expectLpn(lpn, lpn + 500);     /* cold data intact */
}

FTL_TEST(garbage_collection, GcActuallyReclaimsSpace, FtlFixture) {
    for (int round = 0; round < 50; round++)
        for (uint32_t lpn = 0; lpn < 8; lpn++)
            writeLpn(lpn, (uint32_t)round);

    /* after all that pressure the drive must still have room to
     * breathe — GC kept the free pool alive */
    REQUIRE(ftl->freeBlockCount() >= 1);
}

FTL_TEST(garbage_collection, GcRelocatesLiveDataOutOfVictims, FtlFixture) {
    /* Fill the whole logical space: 12 blocks, every page valid. */
    for (uint32_t lpn = 0; lpn < 96; lpn++)
        writeLpn(lpn, lpn);

    /* Punch two holes per original block by rewriting lpns 0,8,16...
     * and then 1,9,17... — each original block drops to 6/8 valid,
     * while the rewrite copies themselves all stay fully valid. */
    for (uint32_t lpn = 0; lpn < 96; lpn += 8) writeLpn(lpn, lpn + 200);
    for (uint32_t lpn = 1; lpn < 96; lpn += 8) writeLpn(lpn, lpn + 400);

    /* The free pool is now down to its reserve — the next write must
     * garbage-collect a 6-valid victim, i.e. RELOCATE 6 live pages. */
    writeLpn(2, 999);

    REQUIRE(ftl->stats().gcRuns >= 1);
    REQUIRE(ftl->stats().pagesRelocated >= 6);

    /* nothing was lost in the move */
    for (uint32_t lpn = 0; lpn < 96; lpn++) {
        if (lpn == 2)             expectLpn(lpn, 999);
        else if (lpn % 8 == 0)    expectLpn(lpn, lpn + 200);
        else if (lpn % 8 == 1)    expectLpn(lpn, lpn + 400);
        else                      expectLpn(lpn, lpn);
    }
}

FTL_TEST(garbage_collection, WriteAmplificationIsMeasured, FtlFixture) {
    for (int round = 0; round < 40; round++)
        for (uint32_t lpn = 0; lpn < 8; lpn++)
            writeLpn(lpn, (uint32_t)round);

    /* WAF = nand writes / host writes: always >= 1, and > 1 once GC
     * has had to relocate anything */
    REQUIRE(ftl->stats().waf() >= 1.0);
    REQUIRE(ftl->stats().hostWrites == 320u);
}
