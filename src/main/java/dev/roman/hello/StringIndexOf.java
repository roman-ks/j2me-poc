package dev.roman.hello;

public class StringIndexOf {
    public static void main(String[] args) {
        String text = new String(new byte[] {
            'O', 'h', '!', '|', 'W', 'h', 'a', 't', ' ', 'e', 'v', 'e', 'r', '.', '.', '.', '|', 'U', 'n', 't', 'i', 'l'
        });

        NativeRuntime.printInt(text.indexOf("|"));
        NativeRuntime.printInt(text.indexOf("|", 4));
        NativeRuntime.printInt(text.indexOf("missing"));
        NativeRuntime.printInt(text.indexOf((int) '|', 4));
        NativeRuntime.printInt(text.indexOf((int) 'x', 0));
        NativeRuntime.printInt(text.charAt(4));
        NativeRuntime.printInt("abc".compareTo("abc"));
        NativeRuntime.printInt("abc".compareTo("abd"));
        NativeRuntime.printInt("abc".compareTo("ab"));
    }
}
