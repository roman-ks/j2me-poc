package dev.roman.hello;

// Regression test for the `iconst_2; idiv` divide-by-2 superinstruction.
// The dividend must be a non-constant (method param) or javac folds `x / 2`
// at compile time and never emits idiv. Covers the cases a naive `>>1` gets
// wrong: Java idiv truncates toward zero, so -3/2 == -1 (not -2) and
// -1/2 == 0 (not -1).
public class DivByTwo {
    static int half(int x) {
        return x / 2;
    }

    public static void main(String[] args) {
        NativeRuntime.printInt(half(7));    // 3
        NativeRuntime.printInt(half(8));    // 4
        NativeRuntime.printInt(half(-3));   // -1  (naive >>1 gives -2)
        NativeRuntime.printInt(half(-4));   // -2
        NativeRuntime.printInt(half(0));    // 0
        NativeRuntime.printInt(half(-1));   // 0   (naive >>1 gives -1)
    }
}
