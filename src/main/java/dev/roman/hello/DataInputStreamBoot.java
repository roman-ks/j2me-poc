package dev.roman.hello;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.EOFException;

public final class DataInputStreamBoot {
    public static void main(String[] args) throws Exception {
        byte[] data = new byte[] {
            1, 2, 3, 4,
            0, 5,
            0, 4, 116, 101, 115, 116,
            9, 8
        };
        DataInputStream in = new DataInputStream(new ByteArrayInputStream(data));

        NativeRuntime.printInt(in.readInt());
        NativeRuntime.printInt(in.readUnsignedShort());
        NativeRuntime.printString(in.readUTF());

        byte[] tail = new byte[2];
        in.readFully(tail);
        NativeRuntime.printInt(tail[0] * 10 + tail[1]);

        try {
            in.readUnsignedByte();
            NativeRuntime.printString("missing-eof");
        } catch (EOFException e) {
            NativeRuntime.printString("eof");
        }
    }
}
