#ifndef PORTRANGEPARSER_H
#define PORTRANGEPARSER_H

#include <QString>
#include <QVector>
#include <QtGlobal>

struct PortParseResult
{
    QVector<quint16> ports;
    QString error;

    bool isValid() const { return error.isEmpty() && !ports.isEmpty(); }
};

class PortRangeParser final
{
public:
    static PortParseResult parse(const QString &specification, int maximumPortCount = 128);
};

#endif // PORTRANGEPARSER_H
