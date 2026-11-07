// Tests des fonctions pures de LanAtlas (sans reseau ni interface).
#include "scan/TuyaListener.h"

#include <QtEndian>
#include <cstdio>
#include <cstdlib>

#define CHECK(cond) do { if (!(cond)) { std::printf("ECHEC ligne %d : %s\n", __LINE__, #cond); \
    std::exit(1); } } while (0)

static TuyaAnnounce at(const QDateTime& t)
{
    TuyaAnnounce a;
    a.lastSeen = t;
    return a;
}

int main()
{
    const QDateTime now = QDateTime::currentDateTime();

    // Purge : les annonces de plus de 5 minutes disparaissent, les recentes restent.
    QHash<QString, TuyaAnnounce> t;
    t.insert("1.1.1.1", at(now.addSecs(-301)));
    t.insert("1.1.1.2", at(now.addSecs(-10)));
    pruneAnnounces(t, now);
    CHECK(t.size() == 1 && t.contains("1.1.1.2"));

    // Plafond : on garde les plus recentes.
    t.clear();
    for (int i = 0; i < 20; ++i)
        t.insert(QString::number(i), at(now.addSecs(-i)));
    pruneAnnounces(t, now, 5);
    CHECK(t.size() == 5 && t.contains("0") && t.contains("4") && !t.contains("5"));

    // Lecture big-endian non signee : 0xFF en tete ne doit pas donner un negatif UB.
    const unsigned char b[4] = {0xff, 0x00, 0x00, 0x01};
    CHECK(qFromBigEndian<quint32>(b) == 0xff000001u);

    std::puts("OK");
    return 0;
}
