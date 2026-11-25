package org.aiverse.render;

import java.lang.ref.Cleaner;
import java.nio.charset.StandardCharsets;
import java.util.Objects;

/** A loaded native model. Java 11+, top-down RGBA8, camera angles in radians. */
public final class RenderModel implements AutoCloseable {
    private static final Cleaner CLEANER = Cleaner.create();
    static { System.loadLibrary("aiverse_render_jni"); }
    private static native long openNative(byte[] path);
    private static native void closeNative(long handle);
    private static native byte[] renderNative(long handle, String backend, int width, int height, float yaw, float pitch);

    private static final class State implements Runnable {
        long handle;
        State(long handle) { this.handle = handle; }
        @Override public void run() {
            if (handle != 0) { closeNative(handle); handle = 0; }
        }
    }
    private final State state;
    private final Cleaner.Cleanable cleanable;
    private RenderModel(long handle) {
        state = new State(handle);
        cleanable = CLEANER.register(this,state);
    }
    public static RenderModel open(String path) {
        Objects.requireNonNull(path);
        if (path.indexOf('\0') >= 0) throw new IllegalArgumentException("Invalid model path");
        return new RenderModel(openNative(path.getBytes(StandardCharsets.UTF_8)));
    }
    @Override public synchronized void close() { cleanable.clean(); }
    public synchronized byte[] render(int width, int height, float yaw, float pitch, String backend) {
        if (state.handle == 0) throw new IllegalStateException("Model is closed");
        return renderNative(state.handle,Objects.requireNonNull(backend),width,height,yaw,pitch);
    }
    public byte[] render(int width, int height) { return render(width,height,.55f,.3f,"cpu"); }
}
