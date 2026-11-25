// Ghidra pre-analysis script for the RCD 330 VIP (Renesas RH850/F1x, R7F7010253) firmware image.
//
// Image layout (5624_vw vipImage.bin, 1 MiB + 12-byte trailer):
//   0x00000..0x07fff  stage 0 boot manager   (vectors @0x0,    EIINT table @0x0200,  reset 0x4da0)
//   0x08000..0x0ffff  stage 1 flash loader   (vectors @0x8000, EIINT table @0x8200,  reset 0xca82)
//   0x10000..0x66fff  stage 2 application    (vectors @0x10000, EIINT table @0x10200, reset 0x455d2)
//   0xff000..0xfffff  config / trailer
// gp = ep = 0 in every stage (absolute addressing, GHS toolchain), so no base register pinning needed.
// The application copies ROM 0x5fb1c..0x619ad -> RAM 0xfede8000 at reset (copy table @0x10688);
// we materialise that as an initialized RAM block so the decompiler sees initial values.
//
// Usage: tools/ghidra/vip_headless.sh <proj> -import vipImage.bin -loader BinaryLoader
//        -loader-baseAddr 0 -processor V850:LE:32:v850e3v5 -scriptPath tools/ghidra -preScript VipSetup.java
// @category RCD330

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.PointerDataType;

public class VipSetup extends GhidraScript {
    private Memory mem;

    private void uninit(String name, long start, long end, boolean io) throws Exception {
        if (end <= start) return;
        MemoryBlock b = mem.createUninitializedBlock(name, toAddr(start), end - start, false);
        b.setRead(true); b.setWrite(true); b.setExecute(false); b.setVolatile(io);
    }

    private long u32(long off) throws Exception { return mem.getInt(toAddr(off)) & 0xffffffffL; }

    private void entry(long a, String name) throws Exception {
        Address addr = toAddr(a);
        if (name != null) createLabel(addr, name, true, SourceType.USER_DEFINED);
        disassemble(addr);
        if (getFunctionAt(addr) == null) createFunction(addr, name);
        addEntryPoint(addr);
    }

    @Override
    public void run() throws Exception {
        mem = currentProgram.getMemory();
        MemoryBlock rom = mem.getBlock(toAddr(0));
        rom.setName("code_flash"); rom.setWrite(false); rom.setExecute(true);

        // Application initialized-data image (ROM 0x5fb1c..0x619ad -> RAM 0xfede8000).
        long src = u32(0x10688), srcEnd = u32(0x1068c), dst = u32(0x10690);
        byte[] init = new byte[(int) (srcEnd - src)];
        mem.getBytes(toAddr(src), init);
        uninit("ram_lo", 0xFEBC0000L, dst, false);
        MemoryBlock data = mem.createInitializedBlock("app_data", toAddr(dst),
                new java.io.ByteArrayInputStream(init), init.length, monitor, false);
        data.setRead(true); data.setWrite(true); data.setExecute(false);
        uninit("ram_hi", dst + init.length, 0xFF000000L, false);
        uninit("periph", 0xFF000000L, 0xFFFFFFFFL, true);
        println(String.format("app_data: ROM 0x%x..0x%x -> RAM 0x%x", src, srcEnd, dst));

        String[] vn = {"RESET","SYSERR","HVTRAP","FETRAP","TRAP0","TRAP1","RIE","FPP_FPI",
                       "UCPOP","MIP_MDP","PIE","DEBUG","MAE","RFU_D","FENMI","FEINT"};
        long[][] stages = {{0x0, 0x8000}, {0x8000, 0x10000}, {0x10000, 0x67000}};
        String[] sn = {"BM", "FL", "APP"};
        for (int s = 0; s < stages.length; s++) {
            long base = stages[s][0], lim = stages[s][1];
            for (int i = 0; i < 32; i++) {
                long v = base + i * 0x10L;
                if ((mem.getShort(toAddr(v)) & 0xffff) != 0x02e0) continue;  // JR disp32
                String n = sn[s] + "_VEC_" + (i < vn.length ? vn[i] : "EIINT_P" + (i - 16));
                createLabel(toAddr(v), n, true, SourceType.USER_DEFINED);
                disassemble(toAddr(v));
                long tgt = v + mem.getInt(toAddr(v + 2));
                entry(tgt, sn[s] + "_" + (i < vn.length ? vn[i] : "EIINT_P" + (i - 16)) + "_handler");
            }
            // EIINT table-reference vector table (INTBP = base + 0x200).
            long tbl = base + 0x200;
            createLabel(toAddr(tbl), sn[s] + "_EIINT_TABLE", true, SourceType.USER_DEFINED);
            for (int ch = 0; ch < 288; ch++) {
                long p = tbl + ch * 4L, t = u32(p);
                if (t < base || t >= lim) break;
                createData(toAddr(p), PointerDataType.dataType);
                if (getFunctionAt(toAddr(t)) == null) entry(t, null);
                if (getSymbolAt(toAddr(t)) == null || getSymbolAt(toAddr(t)).getSource() == SourceType.DEFAULT)
                    createLabel(toAddr(t), String.format("%s_isr_%04x", sn[s], t & 0xffff), true, SourceType.USER_DEFINED);
            }
        }
        long appMain = 0x13de2L;
        long rodataStart = 0x54e68L;
        long rodataEnd = 0x61aa0L;
        for (String a : getScriptArgs()) {
            if (a.startsWith("appMain=")) appMain = Long.decode(a.substring(8));
            else if (a.startsWith("rodataStart=")) rodataStart = Long.decode(a.substring(12));
            else if (a.startsWith("rodataEnd=")) rodataEnd = Long.decode(a.substring(10));
        }
        entry(0x3096, "BM_main");
        entry(appMain, "APP_main");

        // Seed indirect function-pointer tables in APP rodata/init-data
        // so Ghidra discovers task, KWP/UDS, BAP, and state-machine callbacks before auto-analysis.
        int seeded = 0;
        for (long off = rodataStart; off < rodataEnd; off += 4) {
            long tgt = u32(off);
            if (tgt < 0x106a0L || tgt >= rodataStart || (tgt & 1) != 0) continue;
            int first16 = mem.getShort(toAddr(tgt)) & 0xffff;
            int prev16 = mem.getShort(toAddr(tgt - 2)) & 0xffff;
            boolean isPrepare = (first16 & 0xffc0) == 0x0780;
            boolean afterRet = (prev16 == 0x007f) || ((prev16 & 0x00ff) == 0x003f) || (prev16 == 0x0000);
            if (!isPrepare && !afterRet) continue;
            if (getFunctionAt(toAddr(tgt)) == null) {
                entry(tgt, null);
                seeded++;
            }
        }
        println("seeded " + seeded + " indirect function pointer targets from rodata/data");
    }
}
