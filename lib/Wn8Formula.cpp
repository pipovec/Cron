#include "./Wn8Formula.h"

#include <algorithm>

namespace Wn8
{

float Compute(const Totals &actual, const Totals &expected)
{
    if (expected.dmg == 0.0)
    {
        return 0.0f;
    }

    double rDMG  = actual.dmg   / expected.dmg;
    double rSpot = actual.spot  / expected.spot;
    double rFrag = actual.frags / expected.frags;
    double rDef  = actual.def   / expected.def;
    double rWin  = actual.wins  / expected.wins;

    double rDMGc  = std::max(0.0, (rDMG  - 0.22) / 0.78);
    double rSpotc = std::max(0.0, std::min(rDMGc + 0.1, (rSpot - 0.38) / 0.62));
    double rFragc = std::max(0.0, std::min(rDMGc + 0.2, (rFrag - 0.12) / 0.88));
    double rDefc  = std::max(0.0, std::min(rDMGc + 0.1, (rDef  - 0.10) / 0.90));
    double rWinc  = std::max(0.0, (rWin  - 0.71) / 0.29);

    return static_cast<float>(
        980.0 * rDMGc +
        210.0 * rDMGc * rFragc +
        155.0 * rFragc * rSpotc +
        75.0  * rDefc * rFragc +
        145.0 * std::min(1.8, rWinc)
    );
}

} // namespace Wn8
