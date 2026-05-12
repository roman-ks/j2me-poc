package dev.roman.hello;

public class StringBufferBoot {
    public static void main(String[] args) {
        StringBuffer buffer = new StringBuffer();
        buffer.append("enemy[");
        buffer.append(2);
        buffer.append(']');
        buffer.append(10);
        buffer.append(".png");
        buffer.append(true);
        NativeRuntime.printString(buffer.toString());
        NativeRuntime.printInt(buffer.length());
        NativeRuntime.printInt(buffer.capacity());
    }
}
