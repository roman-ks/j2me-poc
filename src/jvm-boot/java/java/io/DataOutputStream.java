package java.io;

public class DataOutputStream extends OutputStream implements DataOutput {
    protected OutputStream out;

    public DataOutputStream(OutputStream out) {
        if (out == null) {
            throw new NullPointerException();
        }
        this.out = out;
    }

    public void write(int b) throws IOException {
        out.write(b);
    }

    public void write(byte[] b) throws IOException {
        out.write(b, 0, b.length);
    }

    public void write(byte[] b, int off, int len) throws IOException {
        out.write(b, off, len);
    }

    public void flush() throws IOException {
        out.flush();
    }

    public void close() throws IOException {
        out.close();
    }

    public void writeBoolean(boolean v) throws IOException {
        out.write(v ? 1 : 0);
    }

    public void writeByte(int v) throws IOException {
        out.write(v);
    }

    public void writeShort(int v) throws IOException {
        out.write((v >>> 8) & 0xff);
        out.write(v & 0xff);
    }

    public void writeChar(int v) throws IOException {
        writeShort(v);
    }

    public void writeInt(int v) throws IOException {
        out.write((v >>> 24) & 0xff);
        out.write((v >>> 16) & 0xff);
        out.write((v >>> 8) & 0xff);
        out.write(v & 0xff);
    }

    public void writeLong(long v) throws IOException {
        out.write((int) (v >>> 56) & 0xff);
        out.write((int) (v >>> 48) & 0xff);
        out.write((int) (v >>> 40) & 0xff);
        out.write((int) (v >>> 32) & 0xff);
        out.write((int) (v >>> 24) & 0xff);
        out.write((int) (v >>> 16) & 0xff);
        out.write((int) (v >>> 8) & 0xff);
        out.write((int) v & 0xff);
    }

    public void writeFloat(float v) throws IOException {
        throw new IOException();
    }

    public void writeDouble(double v) throws IOException {
        throw new IOException();
    }

    public void writeBytes(String s) throws IOException {
        int len = s.length();
        char[] chars = new char[len];
        s.getChars(0, len, chars, 0);
        int i = 0;
        while (i < len) {
            out.write(chars[i] & 0xff);
            i++;
        }
    }

    public void writeChars(String s) throws IOException {
        int len = s.length();
        char[] chars = new char[len];
        s.getChars(0, len, chars, 0);
        int i = 0;
        while (i < len) {
            writeChar(chars[i]);
            i++;
        }
    }

    public void writeUTF(String str) throws IOException {
        int len = str.length();
        char[] chars = new char[len];
        str.getChars(0, len, chars, 0);

        int utfLen = 0;
        int i = 0;
        while (i < len) {
            int c = chars[i];
            if (c >= 1 && c <= 127) {
                utfLen++;
            } else if (c > 2047) {
                utfLen += 3;
            } else {
                utfLen += 2;
            }
            i++;
        }
        if (utfLen > 65535) {
            throw new UTFDataFormatException();
        }

        writeShort(utfLen);
        i = 0;
        while (i < len) {
            int c = chars[i];
            if (c >= 1 && c <= 127) {
                out.write(c);
            } else if (c > 2047) {
                out.write(0xe0 | ((c >> 12) & 0x0f));
                out.write(0x80 | ((c >> 6) & 0x3f));
                out.write(0x80 | (c & 0x3f));
            } else {
                out.write(0xc0 | ((c >> 6) & 0x1f));
                out.write(0x80 | (c & 0x3f));
            }
            i++;
        }
    }
}
