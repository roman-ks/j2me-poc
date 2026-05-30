package javax.microedition.media;

import java.io.InputStream;

public class Manager {
    
    public static Player createPlayer(String locator) {
        return new Player();
    }

    public static Player createPlayer(InputStream stream, String type) {
        return new Player();
    }

}
