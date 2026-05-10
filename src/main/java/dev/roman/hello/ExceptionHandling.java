package dev.roman.hello;

class MarkerException extends RuntimeException {
}

class ChildMarkerException extends MarkerException {
}

public final class ExceptionHandling {
    private static int finallyValue;

    public static void main(String[] args) {
        exactCatch();
        parentCatch();
        missThenParentCatch();
        propagationCatch();
        NativeRuntime.printInt(finallyReturn());
        finallyThrow();
    }

    private static void exactCatch() {
        try {
            throw new MarkerException();
        } catch (MarkerException e) {
            NativeRuntime.printString("exact");
        }
    }

    private static void parentCatch() {
        try {
            throw new ChildMarkerException();
        } catch (RuntimeException e) {
            NativeRuntime.printString("parent");
        }
    }

    private static void missThenParentCatch() {
        try {
            throw new ChildMarkerException();
        } catch (IllegalArgumentException e) {
            NativeRuntime.printString("wrong");
        } catch (MarkerException e) {
            NativeRuntime.printString("miss-parent");
        }
    }

    private static void propagationCatch() {
        try {
            throwFromChild();
        } catch (MarkerException e) {
            NativeRuntime.printString("propagated");
        }
    }

    private static void throwFromChild() {
        throw new MarkerException();
    }

    private static int finallyReturn() {
        try {
            return 3;
        } finally {
            finallyValue = 4;
            NativeRuntime.printString("finally-return");
        }
    }

    private static void finallyThrow() {
        try {
            try {
                throw new MarkerException();
            } finally {
                NativeRuntime.printString("finally-throw");
            }
        } catch (MarkerException e) {
            NativeRuntime.printString("finally-caught");
        }
        NativeRuntime.printInt(finallyValue);
    }
}
