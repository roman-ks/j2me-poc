package dev.roman.j2mepoc;

public class HelloNums {
    public static void main(String[] args) {
        int a = 5;
        int b = 10;
        int sum = a + b;
        NativeRuntime.printInt(sum);
    }
}
