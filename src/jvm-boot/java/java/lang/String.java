package java.lang;

public final class String {
    public native int length();

    public native void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin);
}
