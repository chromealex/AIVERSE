import java.nio.file.Files;
import java.nio.file.Paths;
import org.aiverse.render.RenderModel;
public final class RenderFrame {
    public static void main(String[] args) throws Exception {
        try (RenderModel model=RenderModel.open(args[0])) {
            byte[] rgba=model.render(96,72);
            if(rgba.length!=96*72*4) throw new AssertionError("Wrong frame size");
            Files.write(Paths.get(args[1]),rgba);
            try { model.render(0,72); throw new AssertionError("Accepted zero width"); }
            catch (IllegalArgumentException expected) {}
        }
    }
}
