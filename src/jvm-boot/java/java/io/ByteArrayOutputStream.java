package java.io;

public class ByteArrayOutputStream extends OutputStream {
    protected byte[] buf;
    protected int count;

    public ByteArrayOutputStream() {
        this(32);
    }

    public ByteArrayOutputStream(int size) {
        if (size < 0) {
            throw new IllegalArgumentException();
        }
        buf = new byte[size];
    }

    public void write(int b) {
        int newCount = count + 1;
        ensureCapacity(newCount);
        buf[count] = (byte) b;
        count = newCount;
    }

    public void write(byte[] b, int off, int len) {
        if (b == null) {
            throw new NullPointerException();
        }
        if ((off | len) < 0 || len > b.length - off) {
            throw new IndexOutOfBoundsException();
        }
        int newCount = count + len;
        ensureCapacity(newCount);
        System.arraycopy(b, off, buf, count, len);
        count = newCount;
    }

    public void reset() {
        count = 0;
    }

    public int size() {
        return count;
    }

    public byte[] toByteArray() {
        byte[] out = new byte[count];
        System.arraycopy(buf, 0, out, 0, count);
        return out;
    }

    public void close() throws IOException {
    }

    private void ensureCapacity(int minCapacity) {
        if (minCapacity <= buf.length) {
            return;
        }
        int newCapacity = buf.length << 1;
        if (newCapacity < minCapacity) {
            newCapacity = minCapacity;
        }
        if (newCapacity == 0) {
            newCapacity = 1;
        }
        byte[] newBuf = new byte[newCapacity];
        System.arraycopy(buf, 0, newBuf, 0, count);
        buf = newBuf;
    }
}
