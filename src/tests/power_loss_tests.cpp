/* Suite: power_loss — the hardest firmware requirement: pull the
 * plug at the worst moment, and every ACKNOWLEDGED write must still
 * be there after recovery. Also: corrupted data must never be
 * silently returned. */
#include "fixture.h"
#include "../framework/test_registry.h"

#include <map>

FTL_TEST(power_loss, CommittedWritesSurvivePowerLoss, FtlFixture) {
    for (uint32_t lpn = 0; lpn < 10; lpn++)     /* old, safe data */
        writeLpn(lpn, lpn + 100);

    dev->injectPowerLossAfter(5);               /* the 6th program dies */

    uint32_t lastCommitted = 0;
    bool lost = false;
    for (uint32_t lpn = 10; lpn < 30 && !lost; lpn++) {
        try {
            writeLpn(lpn, lpn + 100);
            lastCommitted = lpn;                /* ack only after success */
        } catch (const nand::PowerLoss&) {
            lost = true;
        }
    }
    REQUIRE(lost);

    ftl->recover();                             /* RAM gone; rescan flash */

    for (uint32_t lpn = 0; lpn <= lastCommitted; lpn++)
        expectLpn(lpn, lpn + 100);              /* every ack'd write lives */
    REQUIRE_THROWS_AS(ftl->read(lastCommitted + 1), ftl::NotWritten);
}

FTL_TEST(power_loss, NewestVersionWinsAfterRecovery, FtlFixture) {
    /* three versions of the same page end up on flash — recovery
     * must pick the one with the highest sequence number */
    writeLpn(7, 1);
    writeLpn(7, 2);
    writeLpn(7, 3);
    ftl->recover();
    expectLpn(7, 3);
}

FTL_TEST(power_loss, FullMappingRebuiltFromOob, FtlFixture) {
    for (uint32_t lpn = 0; lpn < 60; lpn++)
        writeLpn(lpn, lpn * 3);

    ftl->recover();

    for (uint32_t lpn = 0; lpn < 60; lpn++)
        expectLpn(lpn, lpn * 3);
    REQUIRE_THROWS_AS(ftl->read(60), ftl::NotWritten);   /* no ghosts */
}

FTL_TEST(power_loss, SurvivesLossEvenDuringGarbageCollection, FtlFixture) {
    /* track exactly what the host believes is committed */
    std::map<uint32_t, uint32_t> committed;

    auto workload = [&](uint32_t rounds, uint32_t offset) {
        for (uint32_t r = 0; r < rounds; r++)
            for (uint32_t lpn = 0; lpn < 8; lpn++) {
                writeLpn(lpn, r + offset);
                committed[lpn] = r + offset;    /* only after the ack */
            }
    };

    workload(20, 0);                 /* warm up: GC is now active */

    dev->injectPowerLossAfter(30);   /* dies mid-workload — possibly
                                        in the middle of a GC move */
    bool lost = false;
    try { workload(20, 1000); } catch (const nand::PowerLoss&) { lost = true; }
    REQUIRE(lost);

    ftl->recover();
    for (const auto& [lpn, seed] : committed)
        expectLpn(lpn, seed);        /* no acknowledged write was lost */
}

FTL_TEST(power_loss, BitErrorIsDetectedNotSilentlyReturned, FtlFixture) {
    writeLpn(4, 77);

    /* cosmic ray / charge leak: flip one stored bit */
    auto [block, page] = ftl->debugPhysical(4);
    dev->injectBitFlip(block, page, /*bitIndex=*/13);

    /* returning corrupted data as if it were fine would be the worst
     * possible outcome — the checksum must catch it */
    REQUIRE_THROWS_AS(ftl->read(4), ftl::DataCorruption);
}
