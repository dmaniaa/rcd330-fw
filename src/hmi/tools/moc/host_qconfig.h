// File name is ours; stock name unknown.
// Minimal host configuration for compiling Qt 4.7.4 src/tools/moc on macOS/Linux.
#ifndef RCD330_HOST_QCONFIG_H
#define RCD330_HOST_QCONFIG_H

#define QT_PRODUCT_LICENSEE "Open Source"
#define QT_PRODUCT_LICENSE "OpenSource"
#ifndef QT_EDITION
#  define QT_EDITION QT_EDITION_OPENSOURCE
#endif
#ifndef QWS
#  define QWS
#endif
#ifndef Q_WS_QWS
#  define Q_WS_QWS
#endif
#define Q_BIG_ENDIAN 4321
#define Q_LITTLE_ENDIAN 1234
#define Q_BYTE_ORDER Q_LITTLE_ENDIAN
#define QT_POINTER_SIZE 8
#define QT_LARGEFILE_SUPPORT 64
#ifndef QT_NO_QOBJECT
#  define QT_NO_QOBJECT
#endif
#ifndef QT_NO_THREAD
#  define QT_NO_THREAD
#endif
#ifndef QT_NO_UNICODETABLES
#  define QT_NO_UNICODETABLES
#endif
#ifndef QT_NO_CODECS
#  define QT_NO_CODECS
#endif
#ifndef QT_NO_TEXTCODEC
#  define QT_NO_TEXTCODEC
#endif
#ifndef QT_NO_SYSTEMLOCALE
#  define QT_NO_SYSTEMLOCALE
#endif
#ifndef QT_NO_LIBRARY
#  define QT_NO_LIBRARY
#endif

#endif
