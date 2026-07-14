#ifndef PORTRANGEPARSER_H
#define PORTRANGEPARSER_H

#include <QString>
#include <QVector>
#include <QtGlobal>

/**
 * @brief Result of parsing the port-list text entered by the user.
 */
struct PortParseResult
{
    QVector<quint16> ports;
    QString error;

    /**
     * @brief Reports whether parsing produced at least one valid port.
     */
    bool isValid() const { return error.isEmpty() && !ports.isEmpty(); }
};

/**
 * @brief Converts comma-separated ports and ranges into sorted unique ports.
 *
 * Supported examples include "10160", "10160-10169", and
 * "10160-10169,10200". Invalid input is reported through PortParseResult::error.
 */
class PortRangeParser final
{
public:
    /**
     * @brief Parses and validates a port specification.
     * @param specification User-entered comma-separated ports/ranges.
     * @param maximumPortCount Safety limit for one start operation.
     * @return Sorted, deduplicated ports or an explanatory error.
     */
    static PortParseResult parse(const QString &specification, int maximumPortCount = 128);
};

#endif // PORTRANGEPARSER_H
