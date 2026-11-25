// Stock source: control_logic/subsystem/audio/vaudioproxy.cpp (printlog in
// 0x00048240).
//
// Audio manager proxy (opt/audio_proc): primary audio channel, source
// connection requests, and volume / mute status for the status bar.
#ifndef RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_AUDIO_VAUDIOPROXY_H
#define RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_AUDIO_VAUDIOPROXY_H

#include <stdint.h>

#include "control_logic/subsystem/proxy_decoders.h"
#include "service/utility/vbaseproxy.h"

class VAudioProxy : public VBaseProxy {
public:
    static VAudioProxy* Instance();

    void Init();
    // Current primary audio channel (stock m_primary_audio_cur_channel).
    uint8_t GetAudioMode() const;
    void SetCurPrimaryAudioChannel(uint8_t channel);
    void SetTargetPrimaryAudioChannel(int channel);
    // Requests a new primary audio source (VPowerNotifyFilter / hardkeys).
    void InsertAudioMode(uint8_t audio_id);
    // Channel id audio_proc uses for an HMI audio mode.
    uint8_t channel_switch_out(int audio_mode);
    int OnBuildConnectionReq(int audio_mode);
    int OnOffConnectionReq(int audio_mode);
    void OnGetAllAudioSetting();
    // Asks the VIP for its audio mode; it answers with 0x81 (WS_RUNNING).
    void OnGetAudioModeFromVIP();

    const AudioVolumeStatus& GetVolumeStatus() const { return m_volume; }

    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);

private:
    VAudioProxy();

    uint8_t m_connect_result;   // +0x04: source connect result received
    uint8_t m_cur_channel;      // +0x07
    uint8_t m_target_channel;   // +0x08
    uint8_t m_prev_channel;     // +0x09
    uint8_t m_channel_type;     // +0x4b: last channel_switch_out result
    AudioVolumeStatus m_volume; // +0x16 / +0x32..0x3a
    uint8_t m_mute_p4;          // +0x35
};

#endif // RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_AUDIO_VAUDIOPROXY_H
