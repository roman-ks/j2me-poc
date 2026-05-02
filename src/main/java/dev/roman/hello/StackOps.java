package dev.roman.hello;

public class StackOps {
    int value = 5;

    int incField() {
        return value++;
    }

    public static void main(String[] args) {
        int[] arr = new int[1];
        arr[0] = 10;
        NativeRuntime.printInt(arr[0]++);
        NativeRuntime.printInt(arr[0]);
        arr[0]--;
        NativeRuntime.printInt(arr[0]);

        int[] lhs = new int[1];
        int[] rhs = new int[1];
        lhs[0] = rhs[0] = 7;
        NativeRuntime.printInt(lhs[0]);
        NativeRuntime.printInt(rhs[0]);

        StackOps ops = new StackOps();
        NativeRuntime.printInt(ops.incField());
        NativeRuntime.printInt(ops.value);
    }
}