using System;
using System.IO;
using Aiverse.Render;
class RenderFrame {
    static void Main(string[] args) {
        using(var model=RenderModel.Open(args[0])) {
            var rgba=model.Render(96,72);
            if(rgba.Length!=96*72*4) throw new Exception("Wrong frame size");
            File.WriteAllBytes(args[1],rgba);
            try {model.Render(0,72);throw new Exception("Accepted zero width");}
            catch(ArgumentOutOfRangeException) {}
        }
    }
}
