package java.lang;

public final class String {
    public String() {
        init(new char[0], 0, 0);
    }

    public String(String value) {
        if (value == null) {
            init(new char[0], 0, 0);
            return;
        }

        int length = value.length();
        char[] chars = new char[length];
        value.getChars(0, length, chars, 0);
        init(chars, 0, length);
    }

    public String(char[] value) {
        this(value, 0, value.length);
    }

    public String(char[] value, int offset, int count) {
        init(value, offset, count);
    }

    public static String valueOf(int value) {
        return new StringBuffer().append(value).toString();
    }

    public static String valueOf(char value) {
        return new StringBuffer().append(value).toString();
    }

    public native int length();

    public native void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin);

    private native void init(char[] value, int offset, int count);
}
