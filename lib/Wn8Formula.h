/*
 * Wn8Formula - jediny vzorec WN8 v projekte
 *
 * Pouziva ho PlayersWN8v2 (celozivotne WN8) aj PlayersWN8Recent (WN8 za 7/14/28 dni). Pri oprave
 * vzorca staci zmenit toto jedno miesto.
 *
 * Vstup su SUCTY za obdobie: skutocne (to, co hrac dosiahol) a ocakavane
 * (sucet ocakavanej hodnoty tanku * pocet bitiek na tanku). Ocakavane vyhry su
 * sucet 0.01 * expWinRate(tank) * bitky(tank), t.j. ocakavany POCET vyhier; skutocne vyhry
 * su POCET vyhier hraca.
 */

#ifndef WN8FORMULA_H
#define WN8FORMULA_H

namespace Wn8
{

struct Totals
{
    double dmg = 0.0;
    double spot = 0.0;
    double frags = 0.0;
    double def = 0.0;   // dropped_capture_points
    double wins = 0.0;  // pocet vyhier (nie win rate)
};

// Vracia 0 ak ocakavany damage je 0 (nie je z coho pocitat).
float Compute(const Totals &actual, const Totals &expected);

} // namespace Wn8

#endif
