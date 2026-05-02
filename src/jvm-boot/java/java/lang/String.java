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

    public String(byte[] value) {
        this(value, 0, value.length);
    }

    public String(byte[] value, int offset, int count) {
        char[] chars = new char[count];
        int i = 0;
        while (i < count) {
            int b = value[offset + i];
            if (b < 0) {
                b += 256;
            }
            chars[i] = (char) b;
            i++;
        }
        init(chars, 0, count);
    }

    public static String valueOf(int value) {
        return new StringBuffer().append(value).toString();
    }

    public static String valueOf(char value) {
        return new StringBuffer().append(value).toString();
    }

    public char[] toCharArray() {
        int length = length();
        char[] chars = new char[length];
        getChars(0, length, chars, 0);
        return chars;
    }

    public String substring(int beginIndex) {
        return substring(beginIndex, length());
    }

    public String substring(int beginIndex, int endIndex) {
        int stringLength = length();
        if (beginIndex < 0) {
            beginIndex = 0;
        }
        if (endIndex < beginIndex) {
            endIndex = beginIndex;
        }
        if (endIndex > stringLength) {
            endIndex = stringLength;
        }

        int count = endIndex - beginIndex;
        char[] chars = new char[count];
        getChars(beginIndex, endIndex, chars, 0);
        return new String(chars);
    }

    public native int length();

    public native void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin);

    private native void init(char[] value, int offset, int count);
}
