#include "rfb_protocol.h"

#include <QtEndian>
#include <algorithm>
#include <cstring>

namespace {
constexpr int kDefaultWidth = 800;
constexpr int kDefaultHeight = 480;

void putU16(QByteArray &out, uint16_t v) {
    char b[2];
    qToBigEndian(v, b);
    out.append(b, 2);
}

void putU32(QByteArray &out, uint32_t v) {
    char b[4];
    qToBigEndian(v, b);
    out.append(b, 4);
}

void putS32(QByteArray &out, int32_t v) {
    putU32(out, static_cast<uint32_t>(v));
}

uint16_t getU16(const char *p) {
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(p));
}

uint32_t getU32(const char *p) {
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(p));
}

int32_t getS32(const char *p) {
    return static_cast<int32_t>(getU32(p));
}
} // namespace

RfbSession::RfbSession() {
    reset();
}

void RfbSession::reset() {
    m_state = State::AwaitingVersion;
    m_phase = MsgPhase::Idle;
    m_buf.clear();
    m_fb = QImage(kDefaultWidth, kDefaultHeight, QImage::Format_RGB32);
    m_fb.fill(Qt::black);
    m_rectsRemaining = 0;
    m_rx = m_ry = m_rw = m_rh = 0;
    m_skipBytes = 0;
}

QByteArray RfbSession::buildSetPixelFormat32Xrgb() {
    QByteArray out;
    out.reserve(20);
    out.append(char(0));          // message-type = SetPixelFormat
    out.append(3, char(0));       // padding
    out.append(char(32));         // bits-per-pixel
    out.append(char(24));         // depth
    out.append(char(0));          // big-endian-flag = 0 (little-endian host)
    out.append(char(1));          // true-colour-flag = 1
    putU16(out, 255);             // red-max
    putU16(out, 255);             // green-max
    putU16(out, 255);             // blue-max
    out.append(char(16));         // red-shift
    out.append(char(8));          // green-shift
    out.append(char(0));          // blue-shift
    out.append(3, char(0));       // padding
    return out;
}

QByteArray RfbSession::buildSetEncodings() {
    QByteArray out;
    out.reserve(16);
    out.append(char(2));          // message-type = SetEncodings
    out.append(char(0));          // padding
    putU16(out, 3);               // number-of-encodings
    putS32(out, 0);               // Raw
    putS32(out, 1);               // CopyRect
    putS32(out, -223);            // DesktopSize pseudo-encoding
    return out;
}

QByteArray RfbSession::buildFramebufferUpdateRequest(bool incremental, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    QByteArray out;
    out.reserve(10);
    out.append(char(3));
    out.append(char(incremental ? 1 : 0));
    putU16(out, x);
    putU16(out, y);
    putU16(out, w);
    putU16(out, h);
    return out;
}

QByteArray RfbSession::buildPointerEvent(uint8_t buttonMask, uint16_t x, uint16_t y) {
    QByteArray out;
    out.reserve(6);
    out.append(char(5));
    out.append(char(buttonMask));
    putU16(out, x);
    putU16(out, y);
    return out;
}

bool RfbSession::feed(const QByteArray &chunk, QByteArray &outToServer, bool &frameDirty, QString &errorOut) {
    frameDirty = false;
    if (m_state == State::Error) {
        return false;
    }
    m_buf.append(chunk);

    while (true) {
        if (m_state == State::AwaitingVersion) {
            if (m_buf.size() < 12) return true;
            if (!m_buf.startsWith("RFB ")) {
                m_state = State::Error;
                errorOut = "Invalid RFB server banner";
                return false;
            }
            m_buf.remove(0, 12);
            outToServer.append("RFB 003.008\n");
            m_state = State::AwaitingSecurityTypes;
            continue;
        }

        if (m_state == State::AwaitingSecurityTypes) {
            if (m_buf.isEmpty()) return true;
            const uint8_t count = static_cast<uint8_t>(m_buf[0]);
            if (count == 0) {
                if (m_buf.size() < 5) return true;
                const uint32_t reasonLen = getU32(m_buf.constData() + 1);
                if (static_cast<uint64_t>(m_buf.size()) < 5ULL + static_cast<uint64_t>(reasonLen)) return true;
                m_state = State::Error;
                errorOut = QString::fromUtf8(m_buf.mid(5, static_cast<int>(std::min<uint32_t>(reasonLen, 4096u))));
                return false;
            }
            if (m_buf.size() < 1 + count) return true;
            bool hasNone = false;
            for (int i = 0; i < count; ++i) {
                if (static_cast<uint8_t>(m_buf[1 + i]) == 1) {
                    hasNone = true;
                    break;
                }
            }
            m_buf.remove(0, 1 + count);
            if (!hasNone) {
                m_state = State::Error;
                errorOut = "RFB server does not support SecurityType None";
                return false;
            }
            outToServer.append(char(1));
            m_state = State::AwaitingSecurityResult;
            continue;
        }

        if (m_state == State::AwaitingSecurityResult) {
            if (m_buf.size() < 4) return true;
            const uint32_t status = getU32(m_buf.constData());
            m_buf.remove(0, 4);
            if (status != 0) {
                m_state = State::Error;
                errorOut = "RFB security handshake rejected";
                return false;
            }
            outToServer.append(char(1)); // ClientInit shared=1
            m_state = State::AwaitingServerInit;
            continue;
        }

        if (m_state == State::AwaitingServerInit) {
            if (m_buf.size() < 24) return true;
            const uint16_t w = getU16(m_buf.constData() + 0);
            const uint16_t h = getU16(m_buf.constData() + 2);
            const uint32_t nameLen = getU32(m_buf.constData() + 20);
            if (nameLen > 65536 || w == 0 || h == 0 || w > 4096 || h > 4096) {
                m_state = State::Error;
                errorOut = "Invalid RFB ServerInit dimensions";
                return false;
            }
            if (static_cast<uint32_t>(m_buf.size()) < 24 + nameLen) return true;
            m_buf.remove(0, static_cast<int>(24 + nameLen));
            m_fb = QImage(w, h, QImage::Format_RGB32);
            m_fb.fill(Qt::black);
            m_state = State::Ready;
            m_phase = MsgPhase::Idle;
            outToServer.append(buildSetPixelFormat32Xrgb());
            outToServer.append(buildSetEncodings());
            outToServer.append(buildFramebufferUpdateRequest(false, 0, 0, w, h));
            continue;
        }

        // State::Ready
        if (m_phase == MsgPhase::Idle) {
            if (m_buf.isEmpty()) return true;
            const uint8_t msgType = static_cast<uint8_t>(m_buf[0]);
            if (msgType == 0) {
                m_buf.remove(0, 1);
                m_phase = MsgPhase::FbuHeader;
            } else if (msgType == 1) {
                if (m_buf.size() < 6) return true;
                const uint16_t numColors = getU16(m_buf.constData() + 4);
                m_buf.remove(0, 6);
                m_skipBytes = static_cast<uint32_t>(numColors) * 6u;
                m_phase = MsgPhase::SetColorMapSkip;
            } else if (msgType == 2) {
                m_buf.remove(0, 1); // Bell
            } else if (msgType == 3) {
                m_buf.remove(0, 1);
                m_phase = MsgPhase::ServerCutTextLen;
            } else {
                m_state = State::Error;
                errorOut = QString("Unsupported RFB server message type %1").arg(msgType);
                return false;
            }
            continue;
        }

        if (m_phase == MsgPhase::SetColorMapSkip || m_phase == MsgPhase::ServerCutTextBody) {
            if (static_cast<uint32_t>(m_buf.size()) < m_skipBytes) return true;
            m_buf.remove(0, static_cast<int>(m_skipBytes));
            m_skipBytes = 0;
            m_phase = MsgPhase::Idle;
            continue;
        }

        if (m_phase == MsgPhase::ServerCutTextLen) {
            if (m_buf.size() < 7) return true; // 3 pad + 4 len
            m_skipBytes = getU32(m_buf.constData() + 3);
            m_buf.remove(0, 7);
            m_phase = MsgPhase::ServerCutTextBody;
            continue;
        }

        if (m_phase == MsgPhase::FbuHeader) {
            if (m_buf.size() < 3) return true; // 1 pad + 2 numRects
            m_rectsRemaining = getU16(m_buf.constData() + 1);
            m_buf.remove(0, 3);
            if (m_rectsRemaining == 0) {
                m_phase = MsgPhase::Idle;
                outToServer.append(buildFramebufferUpdateRequest(
                    true, 0, 0,
                    static_cast<uint16_t>(m_fb.width()),
                    static_cast<uint16_t>(m_fb.height())));
            } else {
                m_phase = MsgPhase::RectHeader;
            }
            continue;
        }

        if (m_phase == MsgPhase::RectHeader) {
            if (m_buf.size() < 12) return true;
            m_rx = getU16(m_buf.constData() + 0);
            m_ry = getU16(m_buf.constData() + 2);
            m_rw = getU16(m_buf.constData() + 4);
            m_rh = getU16(m_buf.constData() + 6);
            const int32_t enc = getS32(m_buf.constData() + 8);
            m_buf.remove(0, 12);

            if (enc == 0) {
                m_phase = MsgPhase::RawPixels;
            } else if (enc == 1) {
                m_phase = MsgPhase::CopyRectSrc;
            } else if (enc == -223) {
                if (m_rw > 0 && m_rh > 0 && m_rw <= 4096 && m_rh <= 4096) {
                    m_fb = QImage(m_rw, m_rh, QImage::Format_RGB32);
                    m_fb.fill(Qt::black);
                    frameDirty = true;
                }
                --m_rectsRemaining;
                if (m_rectsRemaining == 0) {
                    m_phase = MsgPhase::Idle;
                    outToServer.append(buildFramebufferUpdateRequest(
                        false, 0, 0,
                        static_cast<uint16_t>(m_fb.width()),
                        static_cast<uint16_t>(m_fb.height())));
                }
            } else {
                m_state = State::Error;
                errorOut = QString("Unsupported RFB encoding %1").arg(enc);
                return false;
            }
            continue;
        }

        if (m_phase == MsgPhase::RawPixels) {
            const qsizetype needed = static_cast<qsizetype>(m_rw) * static_cast<qsizetype>(m_rh) * 4;
            if (m_buf.size() < needed) return true;
            const char *src = m_buf.constData();
            const int maxY = std::min<int>(m_fb.height(), m_ry + m_rh);
            const int copyCols = std::max(0, std::min<int>(m_fb.width() - m_rx, m_rw));
            if (copyCols > 0) {
                for (int row = 0; row < (maxY - m_ry); ++row) {
                    uchar *dst = m_fb.scanLine(m_ry + row) + static_cast<qsizetype>(m_rx) * 4;
                    std::memcpy(dst, src + static_cast<qsizetype>(row) * m_rw * 4,
                                static_cast<size_t>(copyCols) * 4);
                }
            }
            m_buf.remove(0, needed);
            frameDirty = true;
            --m_rectsRemaining;
            if (m_rectsRemaining == 0) {
                m_phase = MsgPhase::Idle;
                outToServer.append(buildFramebufferUpdateRequest(
                    true, 0, 0,
                    static_cast<uint16_t>(m_fb.width()),
                    static_cast<uint16_t>(m_fb.height())));
            } else {
                m_phase = MsgPhase::RectHeader;
            }
            continue;
        }

        if (m_phase == MsgPhase::CopyRectSrc) {
            if (m_buf.size() < 4) return true;
            const uint16_t sx = getU16(m_buf.constData() + 0);
            const uint16_t sy = getU16(m_buf.constData() + 2);
            m_buf.remove(0, 4);
            const QImage patch = m_fb.copy(sx, sy, m_rw, m_rh);
            const int maxY = std::min<int>(m_fb.height(), m_ry + patch.height());
            const int copyCols = std::max(0, std::min<int>(m_fb.width() - m_rx, patch.width()));
            if (copyCols > 0) {
                for (int row = 0; row < (maxY - m_ry); ++row) {
                    std::memcpy(m_fb.scanLine(m_ry + row) + static_cast<qsizetype>(m_rx) * 4,
                                patch.constScanLine(row),
                                static_cast<size_t>(copyCols) * 4);
                }
            }
            frameDirty = true;
            --m_rectsRemaining;
            if (m_rectsRemaining == 0) {
                m_phase = MsgPhase::Idle;
                outToServer.append(buildFramebufferUpdateRequest(
                    true, 0, 0,
                    static_cast<uint16_t>(m_fb.width()),
                    static_cast<uint16_t>(m_fb.height())));
            } else {
                m_phase = MsgPhase::RectHeader;
            }
            continue;
        }
    }
}
