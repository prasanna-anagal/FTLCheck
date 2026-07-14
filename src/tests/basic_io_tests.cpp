/* Suite: basic_io — the FTL's contract with the host for plain
 * reads, writes, overwrites and trims. */
#include "fixture.h"
#include "../framework/test_registry.h"

FTL_TEST(basic_io, WriteThenReadRoundTrip, FtlFixture) {
    writeLpn(5, 42);
    expectLpn(5, 42);
}

FTL_TEST(basic_io, OverwriteReturnsLatestVersion, FtlFixture) {
    writeLpn(1, 10);
    writeLpn(1, 99);           /* NAND can't overwrite in place, so this
                                  really lands on a different physical
                                  page — the map must follow it */
    expectLpn(1, 99);
}

FTL_TEST(basic_io, OverwriteMovesToNewPhysicalPage, FtlFixture) {
    writeLpn(3, 7);
    auto before = ftl->debugPhysical(3);
    writeLpn(3, 8);
    auto after = ftl->debugPhysical(3);
    REQUIRE(before != after);          /* proof of write-out-of-place */
    expectLpn(3, 8);
}

FTL_TEST(basic_io, ReadOfUnwrittenPageThrows, FtlFixture) {
    REQUIRE_THROWS_AS(ftl->read(0), ftl::NotWritten);
}

FTL_TEST(basic_io, TrimmedPageBecomesUnreadable, FtlFixture) {
    writeLpn(9, 1);
    ftl->trim(9);
    REQUIRE_THROWS_AS(ftl->read(9), ftl::NotWritten);
}

FTL_TEST(basic_io, EveryLogicalPageIsUsable, FtlFixture) {
    for (uint32_t lpn = 0; lpn < ftl->logicalPages(); lpn++)
        writeLpn(lpn, lpn);
    for (uint32_t lpn = 0; lpn < ftl->logicalPages(); lpn++)
        expectLpn(lpn, lpn);
}

FTL_TEST(basic_io, WriteBeyondCapacityRejected, FtlFixture) {
    REQUIRE_THROWS_AS(writeLpn(ftl->logicalPages(), 0), ftl::FtlError);
}

FTL_TEST(basic_io, WrongSizedWriteRejected, FtlFixture) {
    std::vector<uint8_t> tooShort(10, 0xAB);
    REQUIRE_THROWS_AS(ftl->write(0, tooShort), ftl::FtlError);
}
