package dev.roman.hello;

import java.io.ByteArrayInputStream;

public class ByteArrayInputStreamTest {
    public static void main(String[] args) {
        byte[] data = {10, 20, (byte) 200, (byte) 255};
        ByteArrayInputStream stream = new ByteArrayInputStream(data);

        // read all bytes — must return unsigned 0-255
        NativeRuntime.printInt(stream.read()); // 10
        NativeRuntime.printInt(stream.read()); // 20
        NativeRuntime.printInt(stream.read()); // 200
        NativeRuntime.printInt(stream.read()); // 255

        // past end — must return -1
        NativeRuntime.printInt(stream.read()); // -1
        NativeRuntime.printInt(stream.read()); // -1
    }
}
