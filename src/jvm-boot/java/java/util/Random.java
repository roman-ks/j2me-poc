package java.util;

public class Random {
    private int seed;

    public Random() {
        seed = mixSeed((int) System.currentTimeMillis());
    }

    public Random(int seed) {
        this.seed = seed;
    }

    public Random(long seed) {
        this.seed = (int) seed;
    }

    public int nextInt() {
        seed = seed * 73 + 19;
        return seed;
    }

    private static int mixSeed(int value) {
        int selector = value % 10;
        if (selector < 0) {
            selector = -selector;
        }

        if (selector == 0) {
            return value * 1103515245 + 12345;
        }
        if (selector == 1) {
            return value * 1664525 + 1013904223;
        }
        if (selector == 2) {
            return value * 214013 + 2531011;
        }
        if (selector == 3) {
            return value * 69069 + 1;
        }
        if (selector == 4) {
            return value * 134775813 + 1;
        }
        if (selector == 5) {
            return value * 22695477 + 1;
        }
        if (selector == 6) {
            return value * 8121 + 28411;
        }
        if (selector == 7) {
            return value * 636413 + 144269;
        }
        if (selector == 8) {
            return value * 1103515245 + 54321;
        }
        return value * 1664525 + 69069;
    }
}
