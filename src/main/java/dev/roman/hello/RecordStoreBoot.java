package dev.roman.hello;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import javax.microedition.rms.RecordStore;

public final class RecordStoreBoot {
    public static void main(String[] args) throws Exception {
        RecordStore.deleteRecordStore("boot-rms");
        RecordStore store = RecordStore.openRecordStore("boot-rms", true);
        ByteArrayOutputStream firstBytes = new ByteArrayOutputStream();
        DataOutputStream firstOut = new DataOutputStream(firstBytes);
        firstOut.writeInt(16909060);
        byte[] first = firstBytes.toByteArray();
        if (store.getNumRecords() == 0) {
            store.addRecord(first, 0, first.length);
        } else {
            store.setRecord(1, first, 0, first.length);
        }
        store.closeRecordStore();

        RecordStore reopened = RecordStore.openRecordStore("boot-rms", false);
        byte[] loaded = reopened.getRecord(1);
        NativeRuntime.printInt(reopened.getNumRecords());
        NativeRuntime.printInt(loaded.length);
        NativeRuntime.printInt(loaded[2]);

        ByteArrayOutputStream secondBytes = new ByteArrayOutputStream();
        DataOutputStream secondOut = new DataOutputStream(secondBytes);
        secondOut.writeByte(9);
        secondOut.writeByte(8);
        byte[] second = secondBytes.toByteArray();
        reopened.setRecord(1, second, 0, second.length);
        byte[] saved = reopened.getRecord(1);
        NativeRuntime.printInt(saved[0] * 10 + saved[1]);
        reopened.closeRecordStore();

        RecordStore.deleteRecordStore("boot-rms");
        try {
            RecordStore.openRecordStore("boot-rms", false);
            NativeRuntime.printString("delete-fail");
        } catch (Exception expected) {
            NativeRuntime.printString("deleted");
        }
    }
}
