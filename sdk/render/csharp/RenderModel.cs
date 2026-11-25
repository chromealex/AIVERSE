using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Aiverse.Render
{
    public sealed class RenderModel : SafeHandleZeroOrMinusOneIsInvalid
    {
        private const string Library = "aiverse_render";
        private RenderModel() : base(true) { }
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)]
        private static extern uint aiverse_render_abi_version();
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)]
        private static extern RenderModel aiverse_render_open([MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)]
        private static extern void aiverse_render_close(IntPtr model);
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)]
        private static extern IntPtr aiverse_render_last_error();
        [DllImport(Library, CallingConvention=CallingConvention.Cdecl)]
        private static extern int aiverse_render_rgba8(RenderModel model,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string backend, int width, int height,
            float yaw, float pitch, [Out] byte[] pixels, UIntPtr capacity);

        public static RenderModel Open(string path)
        {
            if (path == null || path.IndexOf('\0') >= 0) throw new ArgumentException("Invalid model path");
            if (aiverse_render_abi_version() != 1) throw new InvalidOperationException("Unsupported AIVERSE renderer ABI");
            var model = aiverse_render_open(path);
            if (model.IsInvalid) {
                model.Dispose();
                throw new InvalidOperationException(Marshal.PtrToStringUTF8(aiverse_render_last_error()));
            }
            return model;
        }

        public byte[] Render(int width=512, int height=512, float yaw=.55f, float pitch=.3f, string backend="cpu")
        {
            if (width<1 || width>4096 || height<1 || height>4096) throw new ArgumentOutOfRangeException("Frame dimensions");
            if (backend == null || backend.IndexOf('\0') >= 0) throw new ArgumentException("Invalid backend");
            var pixels = new byte[width*height*4];
            if (aiverse_render_rgba8(this,backend,width,height,yaw,pitch,pixels,(UIntPtr)pixels.Length)!=0)
                throw new InvalidOperationException(Marshal.PtrToStringUTF8(aiverse_render_last_error()));
            return pixels;
        }
        protected override bool ReleaseHandle() { aiverse_render_close(handle); return true; }
    }
}
