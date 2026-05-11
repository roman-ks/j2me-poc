package java.lang;

public class System {

    private System() {
    }

    public static native void gc();

    public static native void arraycopy(Object src, int srcPos, Object dest, int destPos, int length);
}
