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

    public String(byte[] value, String charset) {
        this(value, 0, value.length, charset);
    }

    public String(byte[] value, int offset, int count, String charset) {
        // Charset name is honoured for "UTF-8"/"utf-8"/"UTF8"; anything else
        // falls back to byte-as-char (Latin-1). This covers the J2ME defaults
        // ("UTF-8", "ISO-8859-1") and degrades gracefully for unknown names.
        boolean useUtf8 = false;
        if (charset != null) {
            int len = charset.length();
            if (len == 5 || len == 4) {
                String upper = upperAscii(charset);
                if (upper.compareTo("UTF-8") == 0 || upper.compareTo("UTF8") == 0) {
                    useUtf8 = true;
                }
            }
        }
        if (!useUtf8) {
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
            return;
        }

        // UTF-8 decode. Upper bound on output length = count (BMP chars only).
        char[] chars = new char[count];
        int outIdx = 0;
        int i = 0;
        while (i < count) {
            int b0 = value[offset + i];
            if (b0 < 0) {
                b0 += 256;
            }
            if (b0 < 0x80) {
                chars[outIdx] = (char) b0;
                outIdx++;
                i++;
            } else if (b0 < 0xC0) {
                // Stray continuation byte — keep as Latin-1.
                chars[outIdx] = (char) b0;
                outIdx++;
                i++;
            } else if (b0 < 0xE0 && i + 1 < count) {
                int b1 = value[offset + i + 1];
                if (b1 < 0) {
                    b1 += 256;
                }
                int cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
                chars[outIdx] = (char) cp;
                outIdx++;
                i += 2;
            } else if (b0 < 0xF0 && i + 2 < count) {
                int b1 = value[offset + i + 1];
                if (b1 < 0) {
                    b1 += 256;
                }
                int b2 = value[offset + i + 2];
                if (b2 < 0) {
                    b2 += 256;
                }
                int cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
                chars[outIdx] = (char) cp;
                outIdx++;
                i += 3;
            } else {
                // 4-byte sequence (outside BMP) or truncated: emit '?'.
                chars[outIdx] = '?';
                outIdx++;
                i++;
            }
        }
        init(chars, 0, outIdx);
    }

    private static String upperAscii(String s) {
        int len = s.length();
        char[] out = new char[len];
        int i = 0;
        while (i < len) {
            char c = s.charAt(i);
            if (c >= 'a' && c <= 'z') {
                c = (char) (c - 32);
            }
            out[i] = c;
            i++;
        }
        return new String(out);
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

    public int indexOf(String value) {
        return indexOf(value, 0);
    }

    public int indexOf(int ch) {
        return indexOf(ch, 0);
    }

    public int indexOf(String value, int fromIndex) {
        if (value == null) {
            return -1;
        }

        char[] source = toCharArray();
        char[] target = value.toCharArray();
        int sourceLength = source.length;
        int targetLength = target.length;

        if (fromIndex < 0) {
            fromIndex = 0;
        }
        if (targetLength == 0) {
            return fromIndex <= sourceLength ? fromIndex : sourceLength;
        }

        int limit = sourceLength - targetLength;
        int i = fromIndex;
        while (i <= limit) {
            int j = 0;
            while (j < targetLength && source[i + j] == target[j]) {
                j++;
            }
            if (j == targetLength) {
                return i;
            }
            i++;
        }
        return -1;
    }

    public String trim() {
        int len = length();
        int start = 0;
        while (start < len && charAt(start) <= ' ') {
            start++;
        }
        int end = len;
        while (end > start && charAt(end - 1) <= ' ') {
            end--;
        }
        if (start == 0 && end == len) {
            return this;
        }
        return substring(start, end);
    }

    public native String toLowerCase();

    public native String toUpperCase();

    public native char charAt(int index);

    public native int indexOf(int ch, int fromIndex);

    public native int compareTo(String anotherString);

    public native boolean equals(Object other);

    public native int length();

    public native void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin);

    private native void init(char[] value, int offset, int count);
}
