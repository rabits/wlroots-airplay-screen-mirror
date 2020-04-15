import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.Files;
import java.io.*;

public class Exec {
    public static void main(String[] args) throws IOException, ClassNotFoundException, Exception {
        // Read frame.raw file from the current directory
        Path path = Paths.get("frame.raw");
        byte[] data = Files.readAllBytes(path);

        System.out.println("Test " + data.length);
        int start = 0;
        int end = data.length;

        // Run function to test
        Exec.processNALUBuffer(data, start, end);

        System.out.println("Start: " + start + ", end: " + end);

        // Write outframe.raw file to current directory
        File file = new File("outframe.raw");
        FileOutputStream fos = null;

        fos = new FileOutputStream(file);
        fos.write(data);
        fos.close();
    }

    /* renamed from: a */
    public static void processNALUBuffer(byte[] buff, int i, int payload_size) {
        int i3;
        int length;
        byte[] token = {0, 0, 0, 1};
        int a = findToken(buff, token, i, payload_size);
        while (a != -1) {
            int a2 = findToken(buff, token, token.length + a, payload_size);
            if (a2 != -1) {
                i3 = a2 - a;
                length = token.length;
            } else {
                i3 = payload_size - a;
                length = token.length;
            }
            int i4 = i3 - length;
            buff[a] = (byte) ((i4 >> 24) & 255);
            buff[a + 1] = (byte) ((i4 >> 16) & 255);
            buff[a + 2] = (byte) ((i4 >> 8) & 255);
            buff[a + 3] = (byte) (i4 & 255);
            a = a2;
        }
    }

    /* renamed from: a */
    private static int findToken(byte[] buff, byte[] token, int from, int payload_size) {
        boolean z;
        while (from < payload_size - token.length) {
            int i3 = 0;
            while (true) {
                if (i3 >= token.length) {
                    z = true;
                    break;
                } else if (buff[from + i3] != token[i3]) {
                    z = false;
                    break;
                } else {
                    i3++;
                }
            }
            if (z) {
                return from;
            }
            from++;
        }
        return -1;
    }
}
