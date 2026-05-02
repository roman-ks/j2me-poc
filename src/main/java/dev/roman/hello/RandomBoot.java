package dev.roman.hello;

import java.util.Random;

public class RandomBoot {
    public static void main(String[] args) {
        Random random = new Random();
        NativeRuntime.printInt(random.nextInt());
        NativeRuntime.printInt(random.nextInt());
    }
}