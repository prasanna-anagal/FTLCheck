/*
 * NandDevice — a software model of a raw NAND flash chip.
 *
 * It enforces the four physical rules that make NAND flash hard to
 * use (and which the FTL exists to hide):
 *
 *   1. You program (write) one PAGE at a time.
 *   2. You erase a whole BLOCK at a time — never a single page.
 *   3. A programmed page cannot be programmed again until its block
 *      is erased. There is no overwrite-in-place.
 *   4. Every erase wears the block; past its erase limit it goes bad.
 *
 * Plus one realism detail: pages inside a block must be programmed
 * in order (0, 1, 2, ...), exactly like real NAND.
 *
 * Each page also has an OOB ("out of band") spare area. Real NAND
 * chips have this too — firmware stores metadata there. Our FTL uses
 * it for the logical page number, a sequence number, and a checksum,
 * which is what makes power-loss recovery possible.
 *
 * The device also supports fault injection (power loss, bit flips,
 * erase failures, factory bad blocks) so the test framework can prove
 * the FTL handles failures — that is the "firmware validation" part.
 */
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace nand {

struct Geometry {
    uint32_t blocks        = 64;    // blocks per chip
    uint32_t pagesPerBlock = 32;    // pages per block
    uint32_t pageSize      = 512;   // data bytes per page
    uint32_t eraseLimit    = 5000;  // erases a block survives
};

/* Out-of-band (spare) metadata stored alongside each page's data. */
struct Oob {
    static constexpr uint32_t NO_LPN = 0xFFFFFFFFu;
    uint32_t lpn      = NO_LPN;  // which logical page this data belongs to
    uint32_t seq      = 0;       // global write sequence number
    uint32_t checksum = 0;       // checksum of the page data
};

enum class PageState : uint8_t { Erased, Programmed };

/* ---- error types thrown by the device ---- */
struct NandError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct ProgramError  : NandError { using NandError::NandError; };
struct EraseError    : NandError { using NandError::NandError; };
struct BadBlockError : NandError { using NandError::NandError; };
struct PowerLoss     : NandError {
    PowerLoss() : NandError("POWER LOSS during a program operation") {}
};

struct DeviceStats {
    uint64_t reads = 0, programs = 0, erases = 0;
};

class NandDevice {
public:
    explicit NandDevice(const Geometry& g = Geometry());

    const Geometry& geometry() const { return geo_; }

    /* Write one page. Throws if the block is bad, the page is not
     * erased, pages are written out of order, or an injected power
     * loss fires (in which case nothing is written). */
    void programPage(uint32_t block, uint32_t page,
                     const std::vector<uint8_t>& data, const Oob& oob);

    /* Read one page's data and OOB. Reading erased pages is allowed
     * (returns 0xFF filler, like real NAND). */
    void readPage(uint32_t block, uint32_t page,
                  std::vector<uint8_t>& data, Oob& oob) const;

    /* Erase a whole block. Throws EraseError and marks the block bad
     * if it is worn out or an erase failure was injected. */
    void eraseBlock(uint32_t block);

    PageState pageState(uint32_t block, uint32_t page) const;
    uint32_t  programmedPages(uint32_t block) const;
    bool      isBad(uint32_t block) const;
    uint32_t  eraseCount(uint32_t block) const;
    uint32_t  badBlockCount() const;
    uint32_t  minEraseCount() const;   // over good blocks
    uint32_t  maxEraseCount() const;
    DeviceStats stats() const { return stats_; }

    /* ---- fault injection (the validation framework drives these) ---- */

    /* The N-th program operation from now will throw PowerLoss. */
    void injectPowerLossAfter(uint32_t programs) { powerLossIn_ = (int64_t)programs; }
    void clearPowerLoss() { powerLossIn_ = -1; }

    /* Flip one bit of already-stored data (models charge leakage /
     * read disturb — the reason real SSDs need ECC). */
    void injectBitFlip(uint32_t block, uint32_t page, uint32_t bitIndex);

    /* The next erase of this block will fail and mark it bad
     * (models a "grown" bad block). */
    void injectEraseFailure(uint32_t block);

    /* Mark a block bad from the start (factory bad block — real NAND
     * chips ship with some). Call before the FTL takes ownership. */
    void markFactoryBad(uint32_t block);

private:
    struct Page {
        std::vector<uint8_t> data;
        Oob oob;
        PageState state = PageState::Erased;
    };
    struct Block {
        std::vector<Page> pages;
        uint32_t eraseCount      = 0;
        uint32_t programmedPages = 0;   // pages written since last erase
        bool bad                 = false;
        bool failNextErase       = false;
    };

    void checkBlock(uint32_t block) const;
    void checkPage(uint32_t block, uint32_t page) const;

    Geometry geo_;
    std::vector<Block> blocks_;
    mutable DeviceStats stats_;
    int64_t powerLossIn_ = -1;   // -1 = disarmed, otherwise a countdown
};

} // namespace nand
