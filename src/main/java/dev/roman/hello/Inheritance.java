package dev.roman.hello;

class InheritanceBase {
    int inheritedValue() {
        return 41;
    }
}

class InheritanceChild extends InheritanceBase {
    int callInherited() {
        return inheritedValue() + 1;
    }
}

public class Inheritance {
    public static void main(String[] args) {
        InheritanceChild child = new InheritanceChild();
        NativeRuntime.printInt(child.callInherited());
    }
}
