package javax.microedition.media;

public class Player {
    
    public void stop() {
        // No-op
    }

    public void deallocate() {
        // No-op
    }

    public void close() {
        // No-op
    }

    public void start() {
        // No-op
    }
    
    public void setLoopCount(int count) {
        // No-op
    }
    
    public int getLoopCount() {
        return 0;
    }
    
    public int getState() {
        return 100; // UNREALIZED
    }

    public void realize() {
        // No-op
    }

    public void prefetch() {
        // No-op
    }

    public void addPlayerListener(PlayerListener listener) {
        // No-op
    }


}
