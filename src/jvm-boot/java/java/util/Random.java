package java.util;

public class Random {
    private int seed;

    public Random() {
        seed = 12345;
    }

    public int nextInt() {
        seed = seed * 73 + 19;
        return seed;
    }
}
