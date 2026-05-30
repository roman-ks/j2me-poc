package dev.roman.hello;

public class StringNullIndexOf {
    static String getNullString() {
        return null;
    }

    public static void main(String[] args) {
        int result = -1;
        try {
            String s = getNullString();
            String lower = s.toLowerCase();
            result = lower.indexOf("true");
        } catch (NullPointerException e) {
            result = -1;
        }
        NativeRuntime.printInt(result);
    }
}
