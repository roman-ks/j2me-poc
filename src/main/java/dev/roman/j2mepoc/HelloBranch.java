package dev.roman.j2mepoc;

public class HelloBranch {
    public static void main(String[] args) {
        int a = 5;
        int b = 10;

        if (a < b) {
            NativeRuntime.printInt(111);
        } else {
            NativeRuntime.printInt(222);
        }
    }
}
