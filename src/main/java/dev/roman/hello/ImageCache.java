package dev.roman.hello;

import javax.microedition.lcdui.Image;

public class ImageCache {
    public static void main(String[] args) throws Exception {
        Image first = Image.createImage("/tools/freej2me/resources/org/recompile/icon.png");
        Image second = Image.createImage("/tools/freej2me/resources/org/recompile/icon.png");

        if (first == second) {
            NativeRuntime.printInt(1);
        } else {
            NativeRuntime.printInt(0);
        }
    }
}