// Unit test for the per-fill internal/external gap-seam classification used by the gap-threshold
// closure gate (removeShapesByGapThreshold -> countExternalGapGroups).
//
// Verifies that:
//   * a gap leading to a fill with the SAME dominant player is an internal seam (not counted);
//   * a gap leading to a different owner, an unowned fill, or off the board (-1) is external;
//   * a mixed gap group (some cells same-owner, some not) is external (not "all internal");
//   * a connected same-owner region can be PARTIALLY owned: an interior sub-fill behind only
//     internal seams has 0 external gaps and survives the threshold, while a same-owner sibling
//     that leaks exceeds the threshold and is denied — the two are decided independently.
//
// Build target: gap-seam-test (see CMakeLists.txt). Run: ./gap-seam-test  (exit 0 = all passed).
//
// The classification helper is pure (no config / no flood-fill machinery), so the test constructs
// Fill objects directly, attaches gap groups, and supplies a hand-built cellsToFill board plus a
// per-fill dominant array.

#include "fill.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(const std::string& name, size_t got, size_t expected) {
    const bool ok = (got == expected);
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name
              << "  (got " << got << ", expected " << expected << ")\n";
    if (!ok) ++g_failures;
}

// A fill carrying only the gap groups we want to classify (cells/closure are irrelevant here).
Fill makeFillWithGaps(const std::vector<std::vector<Position>>& gapGroups) {
    Fill f(64, 64);
    for (const auto& gg : gapGroups) f.addGapGroup(gg);
    return f;
}

} // namespace

int main() {
    const int W = 64, H = 64;
    const size_t THRESHOLD = 2; // matches the project's CLOSED_SHAPE_GAPS_THRESHOLD on example1

    // cellsToFill maps a board position -> fill index (-1 = obstruction / not on any fill).
    std::vector<std::vector<int>> cellsToFill(W, std::vector<int>(H, -1));

    // Fill index -> dominant player (0 = unowned). Models player 4's enclosure plus the open field:
    //   f0: player-4 interior (the main enclosed area)
    //   f1: player-4 bridge fill (same owner, thin connector — closure would be empty in practice)
    //   f2: the open field / a different owner (player 2)
    //   f3: player-4 sibling sub-fill that LEAKS to the open field
    std::vector<size_t> dom = {4, 4, 2, 4};

    // Pick one marker cell per fill and route it (via cellsToFill) to that fill index.
    auto cellOf = [](int idx) { return Position{static_cast<size_t>(idx * 2),
                                                static_cast<size_t>(idx * 2)}; };
    for (int idx = 0; idx < static_cast<int>(dom.size()); ++idx) {
        const Position c = cellOf(idx);
        cellsToFill[c.first][c.second] = idx;
    }
    const Position toF1 = cellOf(1);
    const Position toF2 = cellOf(2);
    const Position offBoard = {40, 40}; // stays -1 in cellsToFill -> a real opening

    // f0: player-4 interior; all three gap groups lead into f1 (same owner 4) -> all internal.
    const Fill f0 = makeFillWithGaps({ {toF1}, {toF1}, {toF1} });
    check("interior fill: 3 same-owner seams -> 0 external",
          countExternalGapGroups(f0, dom[0], cellsToFill, dom), 0);

    // f1: the bridge fill itself has no gaps of its own.
    const Fill f1 = makeFillWithGaps({});
    check("bridge fill: no gaps -> 0 external",
          countExternalGapGroups(f1, dom[1], cellsToFill, dom), 0);

    // f3: player-4 sibling that leaks: two gaps to the open field (f2) and one off the board.
    const Fill f3 = makeFillWithGaps({ {toF2}, {toF2}, {offBoard} });
    check("leaky sibling: 3 openings -> 3 external",
          countExternalGapGroups(f3, dom[3], cellsToFill, dom), 3);

    // Mixed gap group: one cell to the same owner, one to a different owner -> external (not all internal).
    const Fill fMixed = makeFillWithGaps({ {toF1, toF2} });
    check("mixed seam (same + different owner) -> 1 external",
          countExternalGapGroups(fMixed, 4, cellsToFill, dom), 1);

    // A seam into an UNOWNED neighbour (dominant 0) is external even though the neighbour is a real fill.
    {
        std::vector<std::vector<int>> c2(W, std::vector<int>(H, -1));
        c2[2][2] = 1;                       // position (2,2) -> fill index 1
        const std::vector<size_t> dom2 = {4, 0}; // fill 1 is unowned
        const Fill fToUnowned = makeFillWithGaps({ {Position{2, 2}} });
        check("seam into unowned fill -> 1 external",
              countExternalGapGroups(fToUnowned, 4, c2, dom2), 1);
    }

    // An unowned fill (dominant 0) treats all its gaps as external (its closure is meaningless anyway).
    const Fill fUnowned = makeFillWithGaps({ {toF1}, {toF2} });
    check("unowned fill: all gaps external",
          countExternalGapGroups(fUnowned, 0, cellsToFill, dom), 2);

    // --- Partial ownership of a same-owner region (the core behaviour under test) ---
    // f0 and f3 are BOTH player 4. With THRESHOLD = 2:
    //   f0 has 0 external (<= 2)  -> would KEEP its closure,
    //   f3 has 3 external (>  2)  -> would be DENIED its closure.
    // They are decided independently: the region is partially owned.
    const bool f0Denied = countExternalGapGroups(f0, 4, cellsToFill, dom) > THRESHOLD;
    const bool f3Denied = countExternalGapGroups(f3, 4, cellsToFill, dom) > THRESHOLD;
    check("partial ownership: interior (same owner) survives", f0Denied ? 1u : 0u, 0u);
    check("partial ownership: leaky sibling (same owner) denied", f3Denied ? 1u : 0u, 1u);

    if (g_failures == 0) {
        std::cout << "\nALL GAP-SEAM TESTS PASSED\n";
        return 0;
    }
    std::cout << "\n" << g_failures << " GAP-SEAM TEST(S) FAILED\n";
    return 1;
}
