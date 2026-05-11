package java.io;

public class DataInputStream extends InputStream implements DataInput {
    private final InputStream in;
    private final byte[] work;

    public DataInputStream(InputStream in) {
        if (in == null) {
            throw new NullPointerException();
        }
        this.in = in;
        this.work = new byte[8];
    }

    public int available() throws IOException {
        return in.available();
    }

    public void close() throws IOException {
        in.close();
    }

    public synchronized void mark(int readlimit) {
        in.mark(readlimit);
    }

    public boolean markSupported() {
        return in.markSupported();
    }

    public int read() throws IOException {
        return in.read();
    }

    public int read(byte[] b) throws IOException {
        return in.read(b, 0, b.length);
    }

    public int read(byte[] b, int off, int len) throws IOException {
        checkBounds(b, off, len);
        return in.read(b, off, len);
    }

    public synchronized void reset() throws IOException {
        in.reset();
    }

    public long skip(long n) throws IOException {
        return in.skip(n);
    }

    public boolean readBoolean() throws EOFException, IOException {
        return readUnsignedByte() != 0;
    }

    public byte readByte() throws EOFException, IOException {
        return (byte) readUnsignedByte();
    }

    public int readUnsignedByte() throws EOFException, IOException {
        int value = in.read();
        if (value < 0) {
            throw new EOFException();
        }
        return value;
    }

    public short readShort() throws EOFException, IOException {
        return (short) readUnsignedShort();
    }

    public int readUnsignedShort() throws EOFException, IOException {
        readFully(work, 0, 2);
        return ((work[0] & 0xff) << 8) | (work[1] & 0xff);
    }

    public char readChar() throws EOFException, IOException {
        return (char) readUnsignedShort();
    }

    public int readInt() throws EOFException, IOException {
        readFully(work, 0, 4);
        return ((work[0] & 0xff) << 24)
            | ((work[1] & 0xff) << 16)
            | ((work[2] & 0xff) << 8)
            | (work[3] & 0xff);
    }

    public long readLong() throws EOFException, IOException {
        readFully(work, 0, 8);
        return ((long) (work[0] & 0xff) << 56)
            | ((long) (work[1] & 0xff) << 48)
            | ((long) (work[2] & 0xff) << 40)
            | ((long) (work[3] & 0xff) << 32)
            | ((long) (work[4] & 0xff) << 24)
            | ((long) (work[5] & 0xff) << 16)
            | ((long) (work[6] & 0xff) << 8)
            | (long) (work[7] & 0xff);
    }

    public float readFloat() throws EOFException, IOException {
        throw new IOException();
    }

    public double readDouble() throws EOFException, IOException {
        throw new IOException();
    }

    public void readFully(byte[] b) throws EOFException, IOException {
        readFully(b, 0, b.length);
    }

    public void readFully(byte[] b, int off, int len) throws EOFException, IOException {
        checkBounds(b, off, len);
        int done = 0;
        while (done < len) {
            int count = in.read(b, off + done, len - done);
            if (count < 0) {
                throw new EOFException();
            }
            if (count == 0) {
                int value = in.read();
                if (value < 0) {
                    throw new EOFException();
                }
                b[off + done] = (byte) value;
                count = 1;
            }
            done += count;
        }
    }

    public String readUTF() throws EOFException, IOException {
        return readUTF(this);
    }

    public static String readUTF(DataInput in) throws EOFException, IOException {
        int utfLen = in.readUnsignedShort();
        byte[] bytes = new byte[utfLen];
        char[] chars = new char[utfLen];
        in.readFully(bytes, 0, utfLen);

        int count = 0;
        int charCount = 0;
        while (count < utfLen) {
            int c = bytes[count] & 0xff;
            if (c > 127) {
                break;
            }
            count++;
            chars[charCount++] = (char) c;
        }

        while (count < utfLen) {
            int c = bytes[count] & 0xff;
            switch (c >> 4) {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                    count++;
                    chars[charCount++] = (char) c;
                    break;

                case 12:
                case 13: {
                    count += 2;
                    if (count > utfLen) {
                        throw new UTFDataFormatException();
                    }
                    int c2 = bytes[count - 1] & 0xff;
                    if ((c2 & 0xc0) != 0x80) {
                        throw new UTFDataFormatException();
                    }
                    chars[charCount++] = (char) (((c & 0x1f) << 6) | (c2 & 0x3f));
                    break;
                }

                case 14: {
                    count += 3;
                    if (count > utfLen) {
                        throw new UTFDataFormatException();
                    }
                    int c2 = bytes[count - 2] & 0xff;
                    int c3 = bytes[count - 1] & 0xff;
                    if ((c2 & 0xc0) != 0x80 || (c3 & 0xc0) != 0x80) {
                        throw new UTFDataFormatException();
                    }
                    chars[charCount++] = (char) (((c & 0x0f) << 12)
                        | ((c2 & 0x3f) << 6)
                        | (c3 & 0x3f));
                    break;
                }

                default:
                    throw new UTFDataFormatException();
            }
        }

        return new String(chars, 0, charCount);
    }

    public int skipBytes(int n) throws IOException {
        if (n <= 0) {
            return 0;
        }
        int skipped = 0;
        while (skipped < n) {
            long count = in.skip(n - skipped);
            if (count <= 0) {
                if (in.read() < 0) {
                    break;
                }
                count = 1;
            }
            skipped += (int) count;
        }
        return skipped;
    }

    private static void checkBounds(byte[] b, int off, int len) {
        if (b == null) {
            throw new NullPointerException();
        }
        if ((off | len) < 0 || len > b.length - off) {
            throw new IndexOutOfBoundsException();
        }
    }
}
