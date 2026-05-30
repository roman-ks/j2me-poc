package javax.microedition.rms;

public class RecordStore {
    private String name;
    private boolean closed;

    private RecordStore(String name) {
        this.name = name;
        this.closed = false;
    }

    public static RecordStore openRecordStore(String recordStoreName, boolean createIfNecessary) throws RecordStoreException {
        if (recordStoreName == null) {
            throw new NullPointerException();
        }
        if (!open0(recordStoreName, createIfNecessary)) {
            throw new RecordStoreException();
        }
        return new RecordStore(recordStoreName);
    }

    // MIDP 2.0 4-arg form: authmode + writable are ignored — we always treat
    // stores as private + writable.
    public static RecordStore openRecordStore(String recordStoreName,
                                              boolean createIfNecessary,
                                              int authmode,
                                              boolean writable) throws RecordStoreException {
        return openRecordStore(recordStoreName, createIfNecessary);
    }

    public static RecordStore openRecordStore(String recordStoreName, String vendorName, String suiteName) throws RecordStoreException {
        return openRecordStore(recordStoreName, true);
    }

    public static void deleteRecordStore(String recordStoreName) throws RecordStoreException {
        if (recordStoreName == null) {
            throw new NullPointerException();
        }
        if (!delete0(recordStoreName)) {
            throw new RecordStoreException();
        }
    }

    public int addRecord(byte[] data, int offset, int numBytes) throws RecordStoreException {
        checkOpen();
        int recordId = addRecord0(data, offset, numBytes);
        if (recordId <= 0) {
            throw new RecordStoreException();
        }
        return recordId;
    }

    public void closeRecordStore() throws RecordStoreException {
        closed = true;
    }

    public int getNumRecords() throws RecordStoreException {
        checkOpen();
        return getNumRecords0();
    }

    public void setRecord(int recordId, byte[] newData,
                          int offset, int numBytes) throws RecordStoreException {
        checkOpen();
        if (!setRecord0(recordId, newData, offset, numBytes)) {
            throw new RecordStoreException();
        }
    }

    public byte[] getRecord(int recordId) throws RecordStoreException {
        checkOpen();
        int size = getRecordSize0(recordId);
        if (size < 0) {
            throw new RecordStoreException();
        }
        byte[] data = new byte[size];
        if (!getRecord0(recordId, data, 0, size)) {
            throw new RecordStoreException();
        }
        return data;
    }

    public int getRecord(int recordId, byte[] buffer, int offset) throws RecordStoreException {
        checkOpen();
        int size = getRecordSize0(recordId);
        if (size < 0) {
            throw new RecordStoreException();
        }
        if (!getRecord0(recordId, buffer, offset, size)) {
            throw new RecordStoreException();
        }
        return size;
    }

    public int getRecordSize(int recordId) throws RecordStoreException {
        checkOpen();
        int size = getRecordSize0(recordId);
        if (size < 0) {
            throw new RecordStoreException();
        }
        return size;
    }

    private void checkOpen() throws RecordStoreException {
        if (closed) {
            throw new RecordStoreException();
        }
    }

    private static native boolean open0(String recordStoreName, boolean createIfNecessary);
    private static native boolean delete0(String recordStoreName);
    private native int addRecord0(byte[] data, int offset, int numBytes);
    private native int getNumRecords0();
    private native boolean setRecord0(int recordId, byte[] newData, int offset, int numBytes);
    private native int getRecordSize0(int recordId);
    private native boolean getRecord0(int recordId, byte[] data, int offset, int numBytes);
}
