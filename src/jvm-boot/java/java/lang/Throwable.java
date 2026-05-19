package java.lang;

public class Throwable {
    private String message;

    public Throwable() {
    }

    public Throwable(String message) {
        this.message = message;
    }

    public String getMessage() {
        return message;
    }

    public String toString() {
        return message == null ? "" : message;
    }
}
