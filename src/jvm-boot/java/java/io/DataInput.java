package java.io;

import java.io.EOFException;

public interface DataInput {
    boolean readBoolean() throws EOFException, IOException;
    byte readByte() throws EOFException, IOException;
    int readUnsignedByte() throws EOFException, IOException;
    short readShort() throws EOFException, IOException;
    int readUnsignedShort() throws EOFException, IOException;
    char readChar() throws EOFException, IOException;
    int readInt() throws EOFException, IOException;
    long readLong() throws EOFException, IOException;
    float readFloat() throws EOFException, IOException;
    double readDouble() throws EOFException, IOException;
    void readFully(byte[] b) throws EOFException, IOException;
    void readFully(byte[] b, int off, int len) throws EOFException, IOException;
    String readUTF() throws EOFException, IOException;
    int skipBytes(int n) throws IOException;
}
