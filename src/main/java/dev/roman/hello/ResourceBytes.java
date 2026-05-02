package dev.roman.hello;

import java.io.InputStream;

public class ResourceBytes {
    public static void main(String[] args) throws Exception {
        byte[] header = new byte[4];
        InputStream stream = new ResourceBytes().getClass().getResourceAsStream("/hello.txt");
        int count = stream.read(header);

        byte[][] map = new byte[2][3];
        map[1][2] = header[3];

        String[] names = new String[2];
        names[0] = "tile";
        names[1] = "map";

        NativeRuntime.printInt(count);
        NativeRuntime.printInt(header[0]);
        NativeRuntime.printInt(map.length * 10 + map[1].length);
        NativeRuntime.printInt(map[1][2]);
        NativeRuntime.printString(names[0]);
    }
}