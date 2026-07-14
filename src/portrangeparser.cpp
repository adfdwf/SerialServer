#include "portrangeparser.h"

#include <QSet>
#include <QStringList>

#include <algorithm>

PortParseResult PortRangeParser::parse(const QString &specification, int maximumPortCount)
{
    PortParseResult result;
    if (maximumPortCount <= 0) {
        result.error = QStringLiteral("Maximum port count must be greater than zero");
        return result;
    }

    const QString trimmed = specification.trimmed();
    if (trimmed.isEmpty()) {
        result.error = QStringLiteral("Port list cannot be empty");
        return result;
    }

    QSet<quint16> uniquePorts;
    const QStringList groups = trimmed.split(QLatin1Char(','), Qt::KeepEmptyParts);
    for (const QString &rawGroup : groups) {
        const QString group = rawGroup.trimmed();
        if (group.isEmpty()) {
            result.error = QStringLiteral("Port list contains an empty item");
            return result;
        }

        const QStringList range = group.split(QLatin1Char('-'), Qt::KeepEmptyParts);
        if (range.size() > 2 || range.contains(QString())) {
            result.error = QStringLiteral("Invalid port item: %1").arg(group);
            return result;
        }

        bool startOk = false;
        const uint start = range.at(0).trimmed().toUInt(&startOk);
        bool endOk = startOk;
        const uint end = range.size() == 2 ? range.at(1).trimmed().toUInt(&endOk) : start;
        if (!startOk || !endOk || start == 0 || end == 0 || start > 65535 || end > 65535 || start > end) {
            result.error = QStringLiteral("Invalid port or range: %1").arg(group);
            return result;
        }

        for (uint port = start; port <= end; ++port) {
            uniquePorts.insert(static_cast<quint16>(port));
            if (uniquePorts.size() > maximumPortCount) {
                result.error = QStringLiteral("At most %1 ports can be started at once").arg(maximumPortCount);
                return result;
            }
        }
    }

    result.ports.reserve(uniquePorts.size());
    for (quint16 port : uniquePorts) {
        result.ports.push_back(port);
    }
    std::sort(result.ports.begin(), result.ports.end());
    return result;
}
