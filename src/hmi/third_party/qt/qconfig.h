/* File name is ours; stock name unknown.
 *
 * Minimal QtCore configuration for Qt Embedded (QWS) 4.7.4 on little-endian
 * ARMv7 Linux, standing in for the configure-generated qconfig.h that the
 * device's /usr/local/qte4.7.4 build used. qglobal.h includes it as
 * <QtCore/qconfig.h>; src/hmi/CMakeLists.txt places it ahead of the Qt tree.
 *
 * QT_ARCH_ARM matters for ABI: qglobal.h then makes qreal a float, which
 * matches the device libQtCore.so.4 (e.g. QEasingCurve::setAmplitude(float)).
 */
#ifndef RCD330_QT_QCONFIG_H
#define RCD330_QT_QCONFIG_H

/* License information */
#define QT_PRODUCT_LICENSEE "Open Source"
#define QT_PRODUCT_LICENSE "OpenSource"

/* Qt edition */
#ifndef QT_EDITION
#  define QT_EDITION QT_EDITION_OPENSOURCE
#endif

/* Window system: Qt for Embedded Linux */
#ifndef Q_WS_QWS
#  define Q_WS_QWS
#endif

/* Machine byte-order */
#define Q_BIG_ENDIAN 4321
#define Q_LITTLE_ENDIAN 1234
#define Q_BYTE_ORDER Q_LITTLE_ENDIAN

/* Machine architecture */
#define QT_ARCH_ARM

/* Compile time features */
#define QT_LARGEFILE_SUPPORT 64
#define QT_POINTER_SIZE 4

#endif /* RCD330_QT_QCONFIG_H */
