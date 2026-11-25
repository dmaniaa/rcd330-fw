// File name is ours; stock name unknown.
//
// libvmf_client.so (/opt/lib) entry points used by opt/hmi. The library ships
// no header; prototypes follow its exported C symbols and the decompiled
// bodies in docs/decompiled/5624/vmf/libvmf_client/functions/. Every call returns a
// short status: >= 0 success, < 0 error.
#ifndef RCD330_HMI_SERVICE_VMF_VMF_API_H
#define RCD330_HMI_SERVICE_VMF_VMF_API_H

extern "C" {
// nw_vmf_connect (0x000139ac): registers client `name` with opt/vmf; returns
// the client handle (1..15) or < 0.
int nw_vmf_connect(const char* name, int arg1, int arg2, short timeout_ms, unsigned short buf_size);
// nw_vmf_register_msg_group (0x00015494): subscribes to group_count (< 0xfb)
// RX groups; a NULL groups pointer fails with -12.
int nw_vmf_register_msg_group(int handle, unsigned short group_count, const unsigned char* groups);
// nw_vmf_send (0x000160ec): sends a basic (pkt[0] != 1) or MOST control packet.
int nw_vmf_send(char handle, const void* pkt);
// nw_vmf_receive (0x0001655c): blocks for the next packet; returns the bytes
// read (0 = none) or < 0. The third argument is the size of pkt: the library
// rejects packets whose header length does not fit it.
int nw_vmf_receive(char handle, void* pkt, unsigned short buf_size);
}

#endif // RCD330_HMI_SERVICE_VMF_VMF_API_H
