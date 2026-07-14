#include "ftl.h"

namespace ftl {

/* ------------------------------------------------------------------ */
/* construction                                                        */
/* ------------------------------------------------------------------ */

Ftl::Ftl(nand::NandDevice& dev, uint32_t spareBlocks)
    : dev_(dev), spareBlocks_(spareBlocks) {

    const auto& g = dev_.geometry();

    uint32_t usable = 0;
    blocks_.resize(g.blocks);
    for (uint32_t b = 0; b < g.blocks; b++) {
        if (dev_.isBad(b)) {
            blocks_[b].state = BlockState::Bad;   // factory bad block
        } else {
            blocks_[b].state = BlockState::Free;
            usable++;
        }
    }

    if (usable <= spareBlocks_)
        throw FtlError("not enough good blocks for the requested spare");

    /* Overprovisioning: the host only ever sees this much capacity. */
    logicalPages_ = (usable - spareBlocks_) * g.pagesPerBlock;
    map_.assign(logicalPages_, -1);
}

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

/* FNV-1a, a tiny well-known hash — stands in for the ECC a real SSD
 * uses to detect (and correct) bit errors. We detect only. */
uint32_t Ftl::checksum(const std::vector<uint8_t>& d) {
    uint32_t h = 2166136261u;
    for (uint8_t byte : d) {
        h ^= byte;
        h *= 16777619u;
    }
    return h;
}

int64_t Ftl::packAddr(uint32_t block, uint32_t page) const {
    return (int64_t)block * dev_.geometry().pagesPerBlock + page;
}
uint32_t Ftl::addrBlock(int64_t addr) const {
    return (uint32_t)(addr / dev_.geometry().pagesPerBlock);
}
uint32_t Ftl::addrPage(int64_t addr) const {
    return (uint32_t)(addr % dev_.geometry().pagesPerBlock);
}

uint32_t Ftl::freeBlockCount() const {
    uint32_t n = 0;
    for (const auto& b : blocks_)
        if (b.state == BlockState::Free) n++;
    return n;
}

/* Wear leveling lives here: of all free blocks, always pick the one
 * that has been erased the FEWEST times. */
uint32_t Ftl::pickFreeBlock() {
    int64_t best = -1;
    uint32_t bestErases = 0;
    for (uint32_t b = 0; b < blocks_.size(); b++) {
        if (blocks_[b].state != BlockState::Free) continue;
        uint32_t e = dev_.eraseCount(b);
        if (best < 0 || e < bestErases) { best = b; bestErases = e; }
    }
    if (best < 0)
        throw DriveFull("no free block available");
    return (uint32_t)best;
}

void Ftl::safeErase(uint32_t block) {
    try {
        dev_.eraseBlock(block);
        blocks_[block].state = BlockState::Free;
        blocks_[block].validPages = 0;
    } catch (const nand::EraseError&) {
        /* The block failed its erase and grew bad. Its live data was
         * already relocated before we tried to erase, so nothing is
         * lost — just retire it. */
        blocks_[block].state = BlockState::Bad;
        blocks_[block].validPages = 0;
    }
}

/* ------------------------------------------------------------------ */
/* garbage collection                                                  */
/* ------------------------------------------------------------------ */

/* GC's internal write path: put a relocated page somewhere safe.
 * Uses only FREE blocks for space — it must never recurse into GC. */
void Ftl::relocatePage(uint32_t lpn, const std::vector<uint8_t>& data,
                       uint32_t csum) {
    const auto& g = dev_.geometry();

    if (active_ < 0 || activeNextPage_ >= g.pagesPerBlock) {
        if (active_ >= 0) blocks_[active_].state = BlockState::Full;
        uint32_t b = pickFreeBlock();   // throws DriveFull if none left
        blocks_[b].state = BlockState::Active;
        active_ = b;
        activeNextPage_ = 0;
    }

    nand::Oob fresh;
    fresh.lpn = lpn;
    fresh.seq = seq_++;
    fresh.checksum = csum;
    dev_.programPage((uint32_t)active_, activeNextPage_, data, fresh);
    map_[lpn] = packAddr((uint32_t)active_, activeNextPage_);
    blocks_[active_].validPages++;
    activeNextPage_++;
    stats_.nandWrites++;
    stats_.pagesRelocated++;
}

void Ftl::gc() {
    const auto& g = dev_.geometry();

    /* Keep collecting until at least one block actually returns to
     * the free pool. This loop matters: if a victim FAILS its erase
     * (grown bad block) we reclaimed nothing, and stopping there
     * would slowly bleed the free pool dry — a bug our own fault-
     * injection tests caught. */
    for (;;) {
        /* Victim: the FULL block with the fewest valid pages —
         * cheapest to relocate, most space reclaimed. Ties are broken
         * by LOWEST ERASE COUNT: without that, the same few blocks
         * get erased forever while the rest never wear at all (also
         * found empirically — see the study guide). */
        int64_t victim = -1;
        uint32_t leastValid = 0, leastErases = 0;
        for (uint32_t b = 0; b < blocks_.size(); b++) {
            if (blocks_[b].state != BlockState::Full) continue;
            uint32_t v = blocks_[b].validPages;
            uint32_t e = dev_.eraseCount(b);
            if (victim < 0 || v < leastValid ||
                (v == leastValid && e < leastErases)) {
                victim = b;
                leastValid = v;
                leastErases = e;
            }
        }
        if (victim < 0 || leastValid == g.pagesPerBlock)
            throw DriveFull("garbage collection found nothing to reclaim");

        stats_.gcRuns++;

        /* Relocate every still-valid page out of the victim. */
        std::vector<uint8_t> data;
        nand::Oob oob;
        for (uint32_t p = 0; p < g.pagesPerBlock; p++) {
            if (dev_.pageState((uint32_t)victim, p) != nand::PageState::Programmed)
                continue;
            dev_.readPage((uint32_t)victim, p, data, oob);
            if (oob.lpn == nand::Oob::NO_LPN) continue;
            if (oob.lpn >= logicalPages_) continue;
            if (map_[oob.lpn] != packAddr((uint32_t)victim, p)) continue; // stale

            relocatePage(oob.lpn, data, oob.checksum);
            blocks_[victim].validPages--;
        }

        /* Everything valid is out — try to reclaim the victim. */
        safeErase((uint32_t)victim);
        if (blocks_[victim].state == BlockState::Free)
            return;                       // success: pool grew by one
        /* erase failed -> victim retired as bad; reclaim another */
    }
}

/* ------------------------------------------------------------------ */
/* write path                                                          */
/* ------------------------------------------------------------------ */

void Ftl::ensureActiveHasSpace() {
    const auto& g = dev_.geometry();

    if (active_ >= 0 && activeNextPage_ < g.pagesPerBlock)
        return;   // current active block still has room

    if (active_ >= 0)
        blocks_[active_].state = BlockState::Full;
    active_ = -1;

    /* Keep one free block in reserve: GC's relocations need somewhere
     * to go, so we must collect BEFORE handing out the last one. */
    if (freeBlockCount() <= 1)
        gc();   // guarantees the pool grew (or throws DriveFull)

    if (active_ < 0 || activeNextPage_ >= g.pagesPerBlock) {
        if (active_ >= 0) blocks_[active_].state = BlockState::Full;
        uint32_t b = pickFreeBlock();
        blocks_[b].state = BlockState::Active;
        active_ = b;
        activeNextPage_ = 0;
    }
}

void Ftl::invalidateOld(uint32_t lpn) {
    int64_t old = map_[lpn];
    if (old < 0) return;
    blocks_[addrBlock(old)].validPages--;
}

void Ftl::programToActive(uint32_t lpn, const std::vector<uint8_t>& data,
                          bool hostWrite) {
    ensureActiveHasSpace();

    nand::Oob oob;
    oob.lpn = lpn;
    oob.seq = seq_++;
    oob.checksum = checksum(data);

    /* If power dies inside programPage, the exception propagates
     * BEFORE the map is touched — so the previous version of this
     * LPN stays valid. That ordering is the crash-safety guarantee. */
    dev_.programPage((uint32_t)active_, activeNextPage_, data, oob);

    invalidateOld(lpn);
    map_[lpn] = packAddr((uint32_t)active_, activeNextPage_);
    blocks_[active_].validPages++;
    activeNextPage_++;

    stats_.nandWrites++;
    if (hostWrite) stats_.hostWrites++;
}

void Ftl::write(uint32_t lpn, const std::vector<uint8_t>& data) {
    if (lpn >= logicalPages_)
        throw FtlError("write beyond logical capacity: lpn " +
                       std::to_string(lpn));
    if (data.size() != dev_.geometry().pageSize)
        throw FtlError("write data must be exactly one page");
    programToActive(lpn, data, true);
}

/* ------------------------------------------------------------------ */
/* read path                                                           */
/* ------------------------------------------------------------------ */

std::vector<uint8_t> Ftl::read(uint32_t lpn) {
    if (lpn >= logicalPages_)
        throw FtlError("read beyond logical capacity: lpn " +
                       std::to_string(lpn));
    if (map_[lpn] < 0)
        throw NotWritten("logical page " + std::to_string(lpn) +
                         " has never been written (or was trimmed)");

    std::vector<uint8_t> data;
    nand::Oob oob;
    dev_.readPage(addrBlock(map_[lpn]), addrPage(map_[lpn]), data, oob);

    /* Integrity check — the poor man's ECC. */
    if (oob.lpn != lpn || checksum(data) != oob.checksum)
        throw DataCorruption("bit error detected reading lpn " +
                             std::to_string(lpn) + " (block " +
                             std::to_string(addrBlock(map_[lpn])) + ", page " +
                             std::to_string(addrPage(map_[lpn])) + ")");
    return data;
}

void Ftl::trim(uint32_t lpn) {
    if (lpn >= logicalPages_)
        throw FtlError("trim beyond logical capacity");
    if (map_[lpn] < 0) return;
    invalidateOld(lpn);
    map_[lpn] = -1;
}

/* ------------------------------------------------------------------ */
/* power-loss recovery                                                 */
/* ------------------------------------------------------------------ */

void Ftl::recover() {
    const auto& g = dev_.geometry();

    /* Simulate losing every byte of RAM state. */
    map_.assign(logicalPages_, -1);
    std::vector<uint32_t> winningSeq(logicalPages_, 0);
    for (auto& b : blocks_) {
        if (b.state != BlockState::Bad) b.state = BlockState::Free;
        b.validPages = 0;
    }
    active_ = -1;
    activeNextPage_ = 0;
    uint32_t maxSeq = 0;

    /* Scan the OOB of every programmed page on the chip. For each
     * LPN, the copy with the highest sequence number is the truth. */
    std::vector<uint8_t> data;
    nand::Oob oob;
    for (uint32_t b = 0; b < g.blocks; b++) {
        if (blocks_[b].state == BlockState::Bad) continue;

        uint32_t programmed = dev_.programmedPages(b);
        if (programmed == 0) continue;          // fully erased -> stays Free

        /* Partially-written blocks are sealed as Full rather than
         * resumed — simple and safe (GC reclaims the tail later). */
        blocks_[b].state = BlockState::Full;

        for (uint32_t p = 0; p < programmed; p++) {
            dev_.readPage(b, p, data, oob);
            if (oob.lpn == nand::Oob::NO_LPN || oob.lpn >= logicalPages_)
                continue;
            if (map_[oob.lpn] < 0 || oob.seq > winningSeq[oob.lpn]) {
                map_[oob.lpn] = packAddr(b, p);
                winningSeq[oob.lpn] = oob.seq;
            }
            if (oob.seq > maxSeq) maxSeq = oob.seq;
        }
    }

    /* Recount valid pages per block from the winning mappings. */
    for (uint32_t lpn = 0; lpn < logicalPages_; lpn++)
        if (map_[lpn] >= 0)
            blocks_[addrBlock(map_[lpn])].validPages++;

    seq_ = maxSeq + 1;
}

/* ------------------------------------------------------------------ */
/* debug / inspection                                                  */
/* ------------------------------------------------------------------ */

std::pair<uint32_t, uint32_t> Ftl::debugPhysical(uint32_t lpn) const {
    if (lpn >= logicalPages_ || map_[lpn] < 0)
        throw NotWritten("lpn not mapped");
    return { addrBlock(map_[lpn]), addrPage(map_[lpn]) };
}

char Ftl::blockStateChar(uint32_t block) const {
    switch (blocks_.at(block).state) {
        case BlockState::Free:   return 'F';
        case BlockState::Active: return 'A';
        case BlockState::Full:   return 'U';
        case BlockState::Bad:    return 'B';
    }
    return '?';
}

} // namespace ftl
