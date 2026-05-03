package dev.roman.hello;

public class StringIndexOf {
    public static void main(String[] args) {
        String text = new String(new byte[] {
            'O', 'h', '!', '|', 'W', 'h', 'a', 't', ' ', 'e', 'v', 'e', 'r', '.', '.', '.', '|', 'U', 'n', 't', 'i', 'l'
        });

        NativeRuntime.printInt(text.indexOf("|"));
        NativeRuntime.printInt(text.indexOf("|", 4));
        NativeRuntime.printInt(text.indexOf("missing"));
    }
}