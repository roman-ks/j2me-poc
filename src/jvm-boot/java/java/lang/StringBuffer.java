package java.lang;

/*
 * Minimal JVM POC boot class.
 *
 * The public shape follows the small Java 1.3/CLDC-era StringBuffer API, with
 * implementation ideas cross-checked against GNU Classpath's StringBuffer
 * source. This version is intentionally tiny and only contains methods our
 * interpreter is ready to execute.
 */
public final class StringBuffer {
    private static final int DEFAULT_CAPACITY = 16;

    private char[] value;
    private int count;

    public StringBuffer() {
        this(DEFAULT_CAPACITY);
    }

    public StringBuffer(int capacity) {
        value = new char[capacity];
    }

    public StringBuffer(String str) {
        this(str.length() + DEFAULT_CAPACITY);
        append(str);
    }

    public int length() {
        return count;
    }

    public int capacity() {
        return value.length;
    }

    public StringBuffer append(String str) {
        if (str == null) {
            str = "null";
        }

        int len = str.length();
        ensureCapacity(count + len);
        str.getChars(0, len, value, count);
        count += len;
        return this;
    }

    public StringBuffer append(boolean value) {
        return append(value ? "true" : "false");
    }

    public StringBuffer append(int value) {
        if (value == 0) {
            return append('0');
        }
        if (value == -2147483648) {
            return append("-2147483648");
        }
        if (value < 0) {
            append('-');
            value = -value;
        }

        char[] digits = new char[10];
        int pos = digits.length;
        while (value > 0) {
            int digit = value % 10;
            digits[--pos] = (char) ('0' + digit);
            value = value / 10;
        }

        int len = digits.length - pos;
        ensureCapacity(count + len);
        int i = 0;
        while (i < len) {
            valueAt(count + i, digits[pos + i]);
            i++;
        }
        count += len;
        return this;
    }

    public StringBuffer append(char ch) {
        ensureCapacity(count + 1);
        valueAt(count, ch);
        count++;
        return this;
    }

    public char charAt(int index) {
        return value[index];
    }

    public void setCharAt(int index, char ch) {
        valueAt(index, ch);
    }

    public StringBuffer delete(int start, int end) {
        if (end > count) {
            end = count;
        }
        if (start < 0) {
            start = 0;
        }
        if (start >= end) {
            return this;
        }
        int removed = end - start;
        int i = start;
        while (i < count - removed) {
            valueAt(i, value[i + removed]);
            i++;
        }
        count -= removed;
        return this;
    }

    public String toString() {
        return new String(value, 0, count);
    }

    private void ensureCapacity(int minimumCapacity) {
        if (minimumCapacity <= value.length) {
            return;
        }

        int newCapacity = value.length * 2 + 2;
        if (newCapacity < minimumCapacity) {
            newCapacity = minimumCapacity;
        }

        char[] next = new char[newCapacity];
        int i = 0;
        while (i < count) {
            next[i] = value[i];
            i++;
        }
        value = next;
    }

    private void valueAt(int index, char ch) {
        value[index] = ch;
    }
}
