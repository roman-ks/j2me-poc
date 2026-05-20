package dev.roman.hello;

import javax.microedition.lcdui.Font;

public class VirtualDispatchTest {
    public static void main(String[] args) {
        Font f = Font.getDefaultFont();
        NativeRuntime.printInt(f.getHeight());
        NativeRuntime.printInt(f.stringWidth("hello"));
    }
}
