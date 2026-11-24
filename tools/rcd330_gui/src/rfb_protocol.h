#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <cstdint>

class RfbSession {
public:
    enum class State {
        AwaitingVersion,
        AwaitingSecurityTypes,
        AwaitingSecurityResult,
        AwaitingServerInit,
        Ready,
        Error,
    };

    RfbSession();
    void reset();
    bool feed(const QByteArray &chunk, QByteArray &outToServer, bool &frameDirty, QString &errorOut);

    static QByteArray buildPointerEvent(uint8_t buttonMask, uint16_t x, uint16_t y);
    static QByteArray buildFramebufferUpdateRequest(bool incremental, uint16_t x, uint16_t y, uint16_t w, uint16_t h);

    const QImage &framebuffer() const { return m_fb; }
    State state() const { return m_state; }

private:
    enum class MsgPhase {
        Idle,
        FbuHeader,
        RectHeader,
        RawPixels,
        CopyRectSrc,
        SetColorMapSkip,
        ServerCutTextLen,
        ServerCutTextBody,
    };

    static QByteArray buildSetPixelFormat32Xrgb();
    static QByteArray buildSetEncodings();

    State m_state = State::AwaitingVersion;
    MsgPhase m_phase = MsgPhase::Idle;
    QByteArray m_buf;
    QImage m_fb;
    uint16_t m_rectsRemaining = 0;
    uint16_t m_rx = 0;
    uint16_t m_ry = 0;
    uint16_t m_rw = 0;
    uint16_t m_rh = 0;
    uint32_t m_skipBytes = 0;
};
