package java.io;

public abstract class OutputStream implements Closeable {
    public OutputStream() {
    }

    public abstract void write(int b) throws IOException;

    public void write(byte[] b) throws IOException {
        write(b, 0, b.length);
    }

    public void write(byte[] b, int off, int len) throws IOException {
        if (b == null) {
            throw new NullPointerException();
        }
        if ((off | len) < 0 || len > b.length - off) {
            throw new IndexOutOfBoundsException();
        }
        int i = 0;
        while (i < len) {
            write(b[off + i]);
            i++;
        }
    }

    public void flush() throws IOException {
    }

    public void close() throws IOException {
    }
}
