// Print Error bookmarks with the first 3 words at each address, to find undecoded q32s opcodes.
// @category JieLi
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Bookmark;
import java.util.Iterator;

public class DumpErrors extends GhidraScript {
    @Override
    public void run() throws Exception {
        Iterator<Bookmark> it = currentProgram.getBookmarkManager().getBookmarksIterator("Error");
        while (it.hasNext()) {
            Bookmark b = it.next();
            StringBuilder s = new StringBuilder();
            for (int i = 0; i < 3; i++) s.append(String.format("%04x ", getShort(b.getAddress().add(2 * i)) & 0xffff));
            println("ERR\t" + b.getAddress() + "\t" + s + "\t" + b.getComment());
        }
    }
}
