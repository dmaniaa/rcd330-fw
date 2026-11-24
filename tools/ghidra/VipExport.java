// Export every function of the analysed VIP program as decompiled C, mirroring the layout of the
// other docs/decompiled/* archives (one file per function + an index).
// Args: out=<dir>  (default docs/decompiled/vip)
// @category RCD330

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.data.StringDataInstance;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class VipExport extends GhidraScript {
    private static String js(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') b.append('\\').append(c);
            else if (c < 0x20) b.append(String.format("\\u%04x", (int) c));
            else b.append(c);
        }
        return b.append('"').toString();
    }

    @Override
    public void run() throws Exception {
        String out = "docs/decompiled/vip";
        for (String a : getScriptArgs()) if (a.startsWith("out=")) out = a.substring(4);
        File fdir = new File(out, "functions");
        fdir.mkdirs();

        DecompInterface di = new DecompInterface();
        DecompileOptions opt = new DecompileOptions();
        di.setOptions(opt);
        di.toggleCCode(true);
        di.toggleSyntaxTree(false);
        di.setSimplificationStyle("decompile");
        di.openProgram(currentProgram);

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Listing listing = currentProgram.getListing();
        String sha = currentProgram.getExecutableSHA256();
        StringBuilder idx = new StringBuilder("[\n");
        int n = 0, ok = 0;
        for (Function f : fm.getFunctions(true)) {
            monitor.checkCancelled();
            long ea = f.getEntryPoint().getOffset();
            String file = String.format("0x%x.c", ea);
            DecompileResults r = di.decompileFunction(f, 120, monitor);
            String body = (r != null && r.decompileCompleted()) ? r.getDecompiledFunction().getC() : null;
            if (body != null) ok++;

            // callers / callees / strings referenced
            Set<String> callers = new TreeSet<>(), strs = new LinkedHashSet<>();
            for (Function c : f.getCallingFunctions(monitor)) callers.add(String.format("0x%x", c.getEntryPoint().getOffset()));
            Set<String> callees = new TreeSet<>();
            for (Function c : f.getCalledFunctions(monitor)) callees.add(String.format("0x%x", c.getEntryPoint().getOffset()));
            for (Address a : f.getBody().getAddresses(true)) {
                for (Reference ref : rm.getReferencesFrom(a)) {
                    Data d = listing.getDataAt(ref.getToAddress());
                    if (d != null && d.hasStringValue()) strs.add(String.valueOf(d.getValue()));
                }
            }
            AddressSetView body_ = f.getBody();
            try (Writer w = new OutputStreamWriter(new FileOutputStream(new File(fdir, file)), StandardCharsets.UTF_8)) {
                w.write("// ghidra headless export (tools/ghidra/VipExport.java)\n");
                w.write("// target: vipImage.bin (RH850, V850:LE:32:v850e3v5) sha256=" + sha + "\n");
                w.write("// name: " + f.getName() + "\n");
                w.write(String.format("// address: 0x%x\n", ea));
                w.write(String.format("// range: 0x%x-0x%x\n", body_.getMinAddress().getOffset(), body_.getMaxAddress().getOffset()));
                w.write("// callers: " + String.join(", ", callers) + "\n");
                w.write("// callees: " + String.join(", ", callees) + "\n");
                if (!strs.isEmpty()) w.write("// strings: " + String.join(" | ", strs).replace("\n", "\\n") + "\n");
                w.write("// pseudocode: Ghidra decompiler output, not original source\n\n");
                w.write(body != null ? body : "/* decompilation failed: " + (r == null ? "null" : r.getErrorMessage()) + " */\n");
            }
            if (n++ > 0) idx.append(",\n");
            idx.append(String.format("  {\"address\": \"0x%x\", \"name\": %s, \"size\": %d, \"callers\": %d, \"callees\": %d, \"decompiled\": %b}",
                    ea, js(f.getName()), body_.getNumAddresses(), callers.size(), callees.size(), body != null));
        }
        idx.append("\n]\n");
        try (Writer w = new OutputStreamWriter(new FileOutputStream(new File(out, "functions_index.json")), StandardCharsets.UTF_8)) {
            w.write(idx.toString());
        }
        // strings with xrefs
        StringBuilder sj = new StringBuilder("[\n");
        int sc = 0;
        for (Data d : listing.getDefinedData(true)) {
            if (!d.hasStringValue()) continue;
            List<String> xr = new ArrayList<>();
            for (Reference ref : rm.getReferencesTo(d.getAddress())) {
                Function c = fm.getFunctionContaining(ref.getFromAddress());
                xr.add(c != null ? String.format("0x%x", c.getEntryPoint().getOffset()) : ref.getFromAddress().toString());
            }
            if (sc++ > 0) sj.append(",\n");
            sj.append(String.format("  {\"address\": \"%s\", \"value\": %s, \"xref_functions\": [%s]}", d.getAddress(),
                    js(String.valueOf(d.getValue())), xr.stream().map(VipExport::js).reduce((a, b) -> a + ", " + b).orElse("")));
        }
        sj.append("\n]\n");
        try (Writer w = new OutputStreamWriter(new FileOutputStream(new File(out, "strings.json")), StandardCharsets.UTF_8)) {
            w.write(sj.toString());
        }
        di.dispose();
        println("exported " + n + " functions (" + ok + " decompiled), " + sc + " strings -> " + out);
    }
}
