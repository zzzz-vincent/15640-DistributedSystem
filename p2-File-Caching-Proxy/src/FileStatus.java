import java.io.File;
import java.io.RandomAccessFile;
import java.nio.file.Path;
import java.nio.file.Paths;

public class FileStatus {
    private boolean isDir;
    private String mode;
    private RandomAccessFile file;

    public FileStatus() {
        isDir = false;
        mode = null;
        file = null;
    }

    public boolean isDir() {
        return isDir;
    }

    public String getMode() {
        return mode;
    }

    public RandomAccessFile getFile() {
        return file;
    }

    public void setDir(boolean dir) {
        isDir = dir;
    }

    public void setMode(String mode) {
        this.mode = mode;
    }

    public void setFile(RandomAccessFile file) {
        this.file = file;
    }
}
