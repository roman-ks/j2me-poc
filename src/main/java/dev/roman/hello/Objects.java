package dev.roman.hello;

public class Objects {
    int base;

    Objects(int base) {
        this.base = base;
    }

    int add(int x) {
        return base + x;
    }

    public static void main(String[] args) {
        Objects obj = new Objects(7);
        NativeRuntime.printInt(obj.add(5));
    }
}
