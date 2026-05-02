package java.lang;

public final class Integer {
    private Integer() {
    }

    public static int parseInt(String value) {
        if (value == null) {
            return 0;
        }

        int length = value.length();
        if (length == 0) {
            return 0;
        }

        char[] chars = value.toCharArray();
        int index = 0;
        int sign = 1;
        if (chars[0] == '-') {
            sign = -1;
            index = 1;
        }

        int result = 0;
        while (index < length) {
            result = result * 10 + (chars[index] - '0');
            index++;
        }
        return result * sign;
    }
}