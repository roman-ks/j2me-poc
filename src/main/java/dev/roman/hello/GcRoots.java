package dev.roman.hello;

public class GcRoots {
    static Objects saved;

    static Objects makeEscaping(int value) {
        Objects obj = new Objects(value);
        saved = obj;
        return obj;
    }

    static void makeGarbage() {
        Objects temp = new Objects(3);
        int[] tmp = new int[2];
        tmp[0] = temp.add(4);
        NativeRuntime.printInt(tmp[0]);
    }

    public static void main(String[] args) {
        Objects escaped = makeEscaping(8);
        makeGarbage();
        System.gc();
        Objects reused = new Objects(11);
        NativeRuntime.printInt(reused.add(1));
        NativeRuntime.printInt(escaped.add(1));
    }
}
