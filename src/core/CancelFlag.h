#pragma once

#include <atomic>

// Indicateur d'annulation du travail reseau, partage par tout le processus.
// Pourquoi : la fermeture de la fenetre ne doit jamais detruire un QThread encore
// en train de scanner (qFatal). Les boucles longues (scan, client Tuya) testent ce
// drapeau et rendent la main vite ; le destructeur attend ensuite la fin du thread.
inline std::atomic<bool>& cancelFlag()
{
    static std::atomic<bool> flag{false};
    return flag;
}
