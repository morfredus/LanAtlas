#pragma once

#include <QHash>
#include <QString>

class OuiLookup {
public:
    bool loadFile(const QString& path);
    QString vendor(const QString& mac) const;

private:
    QHash<QString, QString> map_;
};
