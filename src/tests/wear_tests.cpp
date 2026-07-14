/* Suite: wear_leveling — erases must be spread across the whole
 * device instead of hammering a few blocks to death. */
#include "fixture.h"
#include "../framework/test_registry.h"

FTL_TEST(wear_leveling, WearSpreadsAcrossAllBlocks, FtlFixture) {
    /* Overwrite a small hot set for a long time. With NO wear
     * leveling the same few blocks would cycle forever while the
     * rest sat at zero erases. */
    for (int round = 0; round < 100; round++)
        for (uint32_t lpn = 0; lpn < 6; lpn++)
            writeLpn(lpn, (uint32_t)round);

    /* every good block took part in the rotation... */
    REQUIRE(dev->minEraseCount() >= 1);
    /* ...and no block is dramatically more worn than another */
    REQUIRE(dev->maxEraseCount() - dev->minEraseCount() <= 5);
}

FTL_TEST(wear_leveling, DeviceSurvivesFarBeyondOneBlocksLifetime, FtlFixture) {
    /* The device as a whole absorbs MORE total erases than any single
     * block could survive (limit = 500). Without wear leveling the
     * churn would concentrate and kill blocks; with it, every block
     * carries a small share and nothing dies. */
    const uint32_t limit = dev->geometry().eraseLimit;   // 500 per block
    for (uint32_t round = 0; round < 800; round++)
        for (uint32_t lpn = 0; lpn < 6; lpn++)
            writeLpn(lpn, round);

    REQUIRE(dev->stats().erases > limit);       // > one block's lifetime
    REQUIRE(dev->badBlockCount() == 0);         // yet nothing died
    for (uint32_t lpn = 0; lpn < 6; lpn++)
        expectLpn(lpn, 799);
}
