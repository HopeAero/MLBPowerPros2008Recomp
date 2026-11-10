// SPDX-License-Identifier: GPL-3.0-or-later
// Batch pseudo-C of many PowerPC functions from a raw MEM1 dump (REL already relocated).
// Args: <entries.txt> <out.c> ; entries: "0xADDR name" per line ('#' comments).
// The pseudo-C is Ghidra's inference, not the original source. Output stays under local/.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public class PowerProsBatchDecompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        List<String[]> entries = new ArrayList<>();
        for (String line : Files.readAllLines(Path.of(a[0]))) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            entries.add(line.split("\\s+", 2));
        }
        // Small-data bases fixed by __init_registers (0x800041A8).
        Address lo = toAddr(0x80000000L), hi = toAddr(0x817FFFFCL);
        Register r13 = currentProgram.getRegister("r13"), r2 = currentProgram.getRegister("r2");
        currentProgram.getProgramContext().setValue(r13, lo, hi, BigInteger.valueOf(0x80322060L));
        currentProgram.getProgramContext().setValue(r2, lo, hi, BigInteger.valueOf(0x80323940L));
        // Pass 1: names + functions, so calls between them print by name.
        for (String[] e : entries) {
            Address at = toAddr(Long.decode(e[0]));
            disassemble(at);
            Function f = getFunctionAt(at);
            if (f == null) f = createFunction(at, null);
            if (f != null && e.length > 1) f.setName(e[1].replaceAll("[^A-Za-z0-9_]", "_"), SourceType.USER_DEFINED);
        }
        // Pass 2: follow calls into the listed address ranges (dtk merges some functions,
        // so their real entries only show up as call targets). Ranges: "range 0xLO 0xHI" lines.
        List<long[]> ranges = new ArrayList<>();
        for (String line : Files.readAllLines(Path.of(a[0]))) {
            String[] p = line.trim().split("\\s+");
            if (p.length == 3 && p[0].equals("#range")) ranges.add(new long[]{Long.decode(p[1]), Long.decode(p[2])});
        }
        java.util.Set<Long> seen = new java.util.HashSet<>();
        for (String[] e : entries) seen.add(Long.decode(e[0]));
        for (int i = 0; i < entries.size(); i++) {
            Function f = getFunctionAt(toAddr(Long.decode(entries.get(i)[0])));
            if (f == null) continue;
            for (ghidra.program.model.listing.Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                if (!ins.getFlowType().isCall()) continue;
                for (Address t : ins.getFlows()) {
                    long v = t.getOffset();
                    boolean in = false;
                    for (long[] r : ranges) in |= v >= r[0] && v < r[1];
                    if (!in || !seen.add(v)) continue;
                    disassemble(t);
                    if (getFunctionAt(t) == null) createFunction(t, null);
                    entries.add(new String[]{String.format("0x%08X", v), String.format("fn_%08X", v)});
                }
            }
        }
        DecompInterface d = new DecompInterface();
        d.setOptions(new DecompileOptions());
        if (!d.openProgram(currentProgram)) throw new IllegalStateException("decompiler open failed");
        StringBuilder out = new StringBuilder();
        int ok = 0, bad = 0;
        for (String[] e : entries) {
            Function f = getFunctionAt(toAddr(Long.decode(e[0])));
            out.append("\n// ===== ").append(e[0]).append(e.length > 1 ? " " + e[1] : "").append(" =====\n");
            if (f == null) { out.append("// no function\n"); bad++; continue; }
            DecompileResults r = d.decompileFunction(f, 60, monitor);
            if (r.decompileCompleted() && r.getDecompiledFunction() != null) { out.append(r.getDecompiledFunction().getC()); ok++; }
            else { out.append("// decompile failed: ").append(r.getErrorMessage()).append("\n"); bad++; }
        }
        d.dispose();
        Files.writeString(Path.of(a[1]), out.toString(), StandardCharsets.UTF_8);
        println("POWERPROS_BATCH ok=" + ok + " failed=" + bad);
    }
}
