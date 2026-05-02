package javax.microedition.rms;

public class RecordStore {
    public static RecordStore openRecordStore(String recordStoreName, boolean createIfNecessary) throws RecordStoreException {
        return new RecordStore();
    }

    public void addRecord(byte[] data, int offset, int numBytes) throws RecordStoreException {
        // Placeholder implementation
    }

    public void closeRecordStore() throws RecordStoreException {
        // Placeholder implementation
    }
    public int getNumRecords() throws RecordStoreException {
        // Placeholder implementation
        return 0;
    }
}
