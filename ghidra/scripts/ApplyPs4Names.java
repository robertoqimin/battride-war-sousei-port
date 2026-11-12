// Applies PS4 import names (PLT stubs -> functions, GOT slots -> labels) from plt_v102.json.
// @category KRPort
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
import java.util.regex.*;

public class ApplyPs4Names extends GhidraScript {
    @Override
    public void run() throws Exception {
        String path = getScriptArgs().length > 0 ? getScriptArgs()[0]
                : System.getProperty("user.home") + "/kr-port/plt_v102.json";
        String json = Files.readString(Path.of(path));
        int stubs = 0, got = 0;
        String stubPart = json.substring(json.indexOf("\"stubs\""), json.indexOf("\"got\""));
        String gotPart = json.substring(json.indexOf("\"got\""));
        Pattern p = Pattern.compile("\"(0x[0-9a-f]+)\"\\s*:\\s*\"([^\"]+)\"");
        Matcher m = p.matcher(stubPart);
        while (m.find()) {
            Address a = toAddr(m.group(1));
            Function f = getFunctionAt(a);
            if (f == null) f = createFunction(a, null);
            if (f != null) { f.setName(m.group(2), SourceType.IMPORTED); stubs++; }
        }
        m = p.matcher(gotPart);
        while (m.find()) {
            createLabel(toAddr(m.group(1)), "got_" + m.group(2), false, SourceType.IMPORTED);
            got++;
        }
        println("Named " + stubs + " PLT stubs and " + got + " GOT slots");
    }
}
