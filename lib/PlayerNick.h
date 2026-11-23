/*
 * PlayerNick - bezpecny zapis nickov do players_all
 *
 * Vo WoT je nick jedinecny medzi zivymi uctami. players_all ale synchronizuje nicky len clenom
 * klanov, takze zaznam hraca mimo klanu moze drzat nick, ktory uz dostal niekto iny. Aby zapis
 * noveho nicku nenarazil na UNIQUE index na players_all.nickname, kazda operacia najprv UVOLNI nick:
 * zastaranemu drzitelovi nastavi nickname = NULL a jeho nick zapise do players_nick (historia).
 *
 * Vsetko prebieha v jednej transakcii (BEGIN ... COMMIT), pri chybe sa vrati ROLLBACK a funkcia
 * vrati false, takze historia a zmena nicku nikdy nezostanu rozlozene.
 */

#ifndef PLAYERNICK_H
#define PLAYERNICK_H

#include <libpq-fe.h>
#include <string>

namespace PlayerNick
{

// Hrac sa premenoval: uvolni new_name u inych uctov, zapise old_name do players_nick a nastavi
// players_all.nickname = new_name. false pri chybe DB (nic sa nezmenilo).
bool Rename(PGconn *conn, int account_id, const std::string &old_name, const std::string &new_name);

// Novy hrac v players_all: uvolni nick u inych uctov a vlozi riadok (account_id, clan_id, nickname).
// false pri chybe DB (nic sa nezmenilo).
bool InsertPlayer(PGconn *conn, int account_id, int clan_id, const std::string &nickname);

} // namespace PlayerNick

#endif
