// Exports a nWiiURecomp game profile and a symbol CSV from an analysed RPX.
// @category nWiiURecomp
//
// Run this on a program loaded with GhidraRPXLoader, after auto-analysis has
// finished. It writes two files next to each other:
//
//   <prefix>.toml         a configs/ profile: paste it into configs/ and pass
//                         it to --config. The [target] gates are filled from
//                         the program Ghidra actually loaded, so the profile
//                         authenticates that exact build.
//   <prefix>-symbols.csv  Name,Start,End,Size for every function Ghidra found.
//
// Candidate HLE hooks are emitted commented out, with the reason. Only names
// the runtime implements are emitted live — `nwiiu-run --list-hooks` prints
// that set, and an unknown name makes Machine's constructor throw rather than
// silently never firing.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.SymbolTable;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public class ExportWiiUProfile extends GhidraScript {

    // Names the runtime serves natively today. Keep in step with
    // nWiiURuntime/src/native_hooks.cpp — a name missing here just means a
    // commented-out candidate, a name that is wrong here means a profile that
    // throws on startup.
    private static final String[] IMPLEMENTED_HOOKS = {"Yaz0Decode"};

    // Substrings that suggest a routine worth serving natively, and the name
    // to try. A hit is a hint, not a decision: the address still has to be the
    // real entry point of a routine with the expected ABI.
    private static final Map<String, String> HOOK_CANDIDATES = new LinkedHashMap<>();
    static {
        HOOK_CANDIDATES.put("yaz0", "Yaz0Decode");
        HOOK_CANDIDATES.put("szs", "Yaz0Decode");
        HOOK_CANDIDATES.put("decompressszs", "Yaz0Decode");
        HOOK_CANDIDATES.put("yay0", "Yay0Decode");
        HOOK_CANDIDATES.put("lz77", "Lz77Decode");
        HOOK_CANDIDATES.put("memcpy", "MemCopy");
        HOOK_CANDIDATES.put("memset", "MemSet");
    }

    private static class Candidate {
        String name;
        String hook;
        long address;
        boolean implemented;
    }

    @Override
    protected void run() throws Exception {
        if (currentProgram == null) {
            println("No open program.");
            return;
        }

        String projectName = askString("nWiiURecomp profile",
                "Project name (e.g. NFS Most Wanted U)",
                currentProgram.getName());
        String prefix = slug(projectName);
        String productCode = askString("nWiiURecomp profile",
                "Product code, blank if unknown (e.g. WUP-P-BNFP)", "");
        String titleId = askString("nWiiURecomp profile",
                "Title id, blank if unknown (16 hex digits)", "");
        File outputDir = askDirectory("Choose output directory", "Select");
        if (outputDir == null) {
            return;
        }

        File csvFile = new File(outputDir, prefix + "-symbols.csv");
        File tomlFile = new File(outputDir, prefix + ".toml");

        List<Candidate> candidates = new ArrayList<>();
        int functionCount = writeSymbolCsv(csvFile, candidates);
        writeProfile(tomlFile, csvFile, projectName, prefix, productCode,
                titleId, candidates, functionCount);

        println("Functions exported: " + functionCount);
        println("Hook candidates: " + candidates.size());
        println("Symbols: " + csvFile.getAbsolutePath());
        println("Profile: " + tomlFile.getAbsolutePath());
        println("Next: cp " + tomlFile.getName() + " configs/ && "
                + "./build/nWiiUAnalyzer/nwiiu-analyze --config configs/"
                + tomlFile.getName() + " <game.rpx> build/" + prefix + ".json");
    }

    private int writeSymbolCsv(File csvFile, List<Candidate> candidates)
            throws Exception {
        int count = 0;
        try (PrintWriter writer = new PrintWriter(csvFile)) {
            writer.println("Name,Start,End,Size");
            FunctionIterator functions =
                    currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext()) {
                if (monitor.isCancelled()) {
                    break;
                }
                Function function = functions.next();
                if (function.isExternal()) {
                    continue;
                }
                Address start = function.getEntryPoint();
                Address end = function.getBody().getMaxAddress();
                long size = function.getBody().getNumAddresses();
                writer.printf("%s,0x%08X,0x%08X,%d%n", function.getName(),
                        start.getOffset(), end == null ? start.getOffset()
                                : end.getOffset(), size);
                ++count;

                Candidate candidate = classify(function.getName(),
                        start.getOffset());
                if (candidate != null) {
                    candidates.add(candidate);
                }
            }
        }
        return count;
    }

    private Candidate classify(String name, long address) {
        String needle = name.toLowerCase(Locale.ROOT).replace("_", "");
        for (Map.Entry<String, String> entry : HOOK_CANDIDATES.entrySet()) {
            if (!needle.contains(entry.getKey())) {
                continue;
            }
            Candidate candidate = new Candidate();
            candidate.name = name;
            candidate.hook = entry.getValue();
            candidate.address = address;
            candidate.implemented = isImplemented(entry.getValue());
            return candidate;
        }
        return null;
    }

    private static boolean isImplemented(String hook) {
        for (String known : IMPLEMENTED_HOOKS) {
            if (known.equals(hook)) {
                return true;
            }
        }
        return false;
    }

    private void writeProfile(File tomlFile, File csvFile, String projectName,
            String prefix, String productCode, String titleId,
            List<Candidate> candidates, int functionCount) throws Exception {
        String sha256 = currentProgram.getExecutableSHA256();
        long entryPoint = firstEntryPoint();

        try (PrintWriter writer = new PrintWriter(tomlFile)) {
            writer.println("# Generated by ExportWiiUProfile.java from "
                    + currentProgram.getName() + ".");
            writer.println("# Functions seen by Ghidra: " + functionCount + ".");
            writer.println("# Review every [hle_hooks] line before enabling it.");
            writer.println();
            writer.println("project_name  = " + quote(projectName));
            writer.println("output_dir    = " + quote("export/" + prefix));
            writer.println("target_prefix = " + quote(prefix));
            writer.println("symbols_csv   = " + quote(csvFile.getName()));
            writer.println();
            writer.println("[system]");
            writer.println("platform = \"WiiU\"");
            writer.println();
            writer.println("[target]");
            writeOptional(writer, "product_code", productCode);
            writeOptional(writer, "title_id", titleId);
            writer.println("title_version = 0");
            if (sha256 == null || sha256.isEmpty()) {
                writer.println("# sha256 unavailable — Ghidra did not record a "
                        + "digest for this import.");
                writer.println("# sha256      = \"\"");
            } else {
                // Ghidra hashes the imported file, which for an RPX import is
                // the .rpx itself — the same bytes nwiiu-analyze hashes.
                writer.println("sha256        = "
                        + quote(sha256.toLowerCase(Locale.ROOT)));
            }
            if (entryPoint == 0) {
                writer.println("# entry_point unavailable — no external entry "
                        + "point in the program.");
                writer.println("# entry_point = 0x00000000");
            } else {
                writer.printf("entry_point   = 0x%08X%n", entryPoint);
            }
            writer.println("name          = " + quote(projectName));
            writer.println();
            writer.println("[hle_hooks]");
            if (candidates.isEmpty()) {
                writer.println("# No candidates matched. That is the normal "
                        + "result for a stripped retail RPX:");
                writer.println("# name the routines in Ghidra first, or find "
                        + "them by profiling a boot with NWIIU_MISS_DUMP.");
            }
            for (Candidate candidate : candidates) {
                if (candidate.implemented) {
                    writer.printf("\"%08X\" = %s  # %s%n", candidate.address,
                            quote(candidate.hook), candidate.name);
                } else {
                    writer.printf(
                            "# \"%08X\" = %s  # %s — no native implementation "
                                    + "yet; add one to native_hooks.cpp first%n",
                            candidate.address, quote(candidate.hook),
                            candidate.name);
                }
            }
        }
    }

    // 0 means "not found", which the profile renders as a commented-out
    // entry_point — a looser gate, never a wrong one.
    private long firstEntryPoint() {
        SymbolTable symbols = currentProgram.getSymbolTable();
        for (Address address : symbols.getExternalEntryPointIterator()) {
            if (address != null) {
                return address.getOffset();
            }
        }
        return 0;
    }

    private static void writeOptional(PrintWriter writer, String key,
            String value) {
        if (value == null || value.trim().isEmpty()) {
            writer.println("# " + key + " = \"\"");
        } else {
            writer.println(key + " = " + quote(value.trim()));
        }
    }

    // The profile parser accepts \" \\ \n \t and nothing else, so escape to
    // exactly that set.
    private static String quote(String value) {
        StringBuilder text = new StringBuilder("\"");
        for (char item : value.toCharArray()) {
            if (item == '"' || item == '\\') {
                text.append('\\').append(item);
            } else if (item == '\n') {
                text.append("\\n");
            } else if (item == '\t') {
                text.append("\\t");
            } else if (item >= 0x20 && item != 0x7F) {
                text.append(item);
            }
        }
        return text.append('"').toString();
    }

    // Must match GameConfig::target_prefix(): lowercase alphanumerics, single
    // dashes, never leading with a digit.
    private static String slug(String value) {
        StringBuilder prefix = new StringBuilder();
        for (char item : value.toLowerCase(Locale.ROOT).toCharArray()) {
            if ((item >= 'a' && item <= 'z') || (item >= '0' && item <= '9')) {
                prefix.append(item);
            } else if (prefix.length() > 0
                    && prefix.charAt(prefix.length() - 1) != '-') {
                prefix.append('-');
            }
        }
        while (prefix.length() > 0
                && prefix.charAt(prefix.length() - 1) == '-') {
            prefix.setLength(prefix.length() - 1);
        }
        if (prefix.length() == 0
                || (prefix.charAt(0) >= '0' && prefix.charAt(0) <= '9')) {
            prefix.insert(0, 'g');
        }
        return prefix.toString();
    }
}
