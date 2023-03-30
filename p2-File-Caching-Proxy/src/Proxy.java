import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

public class Proxy {
    static AtomicInteger globalFD;
    static ConcurrentHashMap<Integer, FileStatus> fileTable;
    private static class FileHandler implements FileHandling {
        public int open( String path, OpenOption o ) {
            log("lib: open system call - " + path + " " + o);
            if (path == null) {
                return Errors.EINVAL; // Invalid argument
            }

            File f = new File(path);
            int fd;

            if (!o.equals(OpenOption.CREATE_NEW) && !o.equals(OpenOption.CREATE) && !f.exists()) {
                log("lib: open system call - error: No such file or directory");
                return Errors.ENOENT; // No such file or directory
            }

            FileStatus fileStatus = new FileStatus();
            if (f.isDirectory()) {
                fileStatus.setDir(true);
            }

            RandomAccessFile file;

            switch (o) {
                case READ:
                    fileStatus.setMode("r");
                    if (fileStatus.isDir()) {
                        break;
                    }
                    try {
                        file = new RandomAccessFile(f, fileStatus.getMode());
                        fileStatus.setFile(file);
                    } catch (FileNotFoundException fileNotFoundException) {
                        log("lib: open system call - error: No such file or directory");
                        return Errors.ENOENT; // No such file or directory
                    }
                    break;
                case WRITE:
                    if (fileStatus.isDir()) {
                        log("lib: open system call - error: Is a directory");
                        return Errors.EISDIR;
                    }
                    fileStatus.setMode("rw");
                    try {
                        file = new RandomAccessFile(f, fileStatus.getMode());
                        fileStatus.setFile(file);
                    } catch (FileNotFoundException fileNotFoundException) {
                        log("lib: open system call - error: No such file or directory");
                        return Errors.ENOENT; // No such file or directory
                    }
                    break;
                case CREATE_NEW:
                    if (f.exists()) {
                        log("lib: open system call - error: File exists");
                        return Errors.EEXIST; // File exists
                    }
                case CREATE:
                    if (fileStatus.isDir()) {
                        log("lib: open system call - error: Is a directory");
                        return Errors.EISDIR;
                    }
                    fileStatus.setMode("rw");
                    try {
                        f.createNewFile();
                        file = new RandomAccessFile(f, fileStatus.getMode());
                        fileStatus.setFile(file);
                    }  catch (FileNotFoundException fileNotFoundException) {
                        log("lib: open system call - error: No such file or directory");
                        return Errors.ENOENT;
                    } catch (IOException ioException) {
                        log("lib: open system call - error: I/O error");
                        return -5;
                    }
                    break;
                default:
                    log("lib: open system call - Invalid argument");
                    return Errors.EINVAL;
            }
            // filetable is synchronized but file is mutable?
            fd = globalFD.incrementAndGet();
            fileTable.put(fd, fileStatus);
            log("lib: open system call - return " + fd);
            return fd;
        }

        public int close( int fd ) {
            log("lib: close system call - " + fd);
            if (!fileTable.containsKey(fd)) {
                log("lib: close system call - error: Bad file number");
                return Errors.EBADF;
            }
            fileTable.remove(fd);
            log("lib: close system call - return 0");
            return 0;
        }

        public long write( int fd, byte[] buf ) {
            log("lib: write system call - " + fd + " with buf length " + buf.length);
            if (!fileTable.containsKey(fd)) {
                log("lib: write system call - error: Bad file number");
                return Errors.EBADF;
            }
            FileStatus fileStatus = fileTable.get(fd);
            if (fileStatus.isDir()) {
                log("lib: write system call - error: Is a directory");
                return Errors.EISDIR;
            }
            if (fileStatus.getMode().equals("r")) {
                log("lib: write system call - error: Bad file number");
                return Errors.EBADF;
            }
            try {
                fileStatus.getFile().write(buf);
                log("lib: write system call - return " + buf.length);
                return buf.length;
            } catch (IOException ioException) {
                log("lib: write system call - error: Device or resource busy");
                return Errors.EBUSY;
            }
        }

        public long read( int fd, byte[] buf ) {
            log("lib: read system call - " + fd + " with buf length " + buf.length);
            if (!fileTable.containsKey(fd)) {
                log("lib: read system call - error: Bad file number");
                return Errors.EBADF;
            }
            FileStatus fileStatus = fileTable.get(fd);
            if (fileStatus.isDir()) {
                log("lib: read system call - error: Is a directory");
                return Errors.EISDIR;
            }
            try {
                long result = fileStatus.getFile().read(buf);
                result = result == -1 ? 0 : result;
                log("lib: read system call - return " + result);
                return result;
            } catch (IOException ioException) {
                log("lib: read system call - error: Device or resource busy");
                return Errors.EBUSY; // Device or resource busy
            } catch (NullPointerException nullPointerException) {
                log("lib: read system call - error: Bad address");
                return -14; // EFAULT: Bad address
            }
        }

        public long lseek( int fd, long pos, LseekOption o ) {
            log("lib: lseek system call - " + fd + " with pos " + pos + " and option " + o);
            if (!fileTable.containsKey(fd)) {
                log("lib: lseek system call - error: Bad file number");
                return Errors.EBADF;
            }
            FileStatus fileStatus = fileTable.get(fd);
            if (fileStatus.isDir()) {
                log("lib: lseek system call - error: Is a directory");
                return Errors.EISDIR;
            }
            try {
                RandomAccessFile file = fileStatus.getFile();
                long curHeader = file.getFilePointer();
                switch (o) {
                    case FROM_CURRENT:
                        file.seek(curHeader + pos);
                        log("lib: lseek system call - return" + curHeader + pos);
                        return curHeader + pos;
                    case FROM_END:
                        file.seek(file.length() + pos);
                        log("lib: lseek system call - return" + pos + file.length());
                        return file.length() + pos;
                    case FROM_START:
                        file.seek(pos);
                        log("lib: lseek system call - return" + pos);
                        return pos;
                    default:
                        log("lib: lseek system call - error: Invalid argument");
                        return Errors.EINVAL;
                }
            } catch (IOException e) {
                log("lib: lseek system call - error: Device or resource busy");
                return Errors.EBUSY;
            }
        }

        public int unlink( String path ) {
            log("lib: unlink system call - " + path);
            if (path == null) {
                return Errors.ENOENT;
            }

            File f = new File(path);

            if (!f.exists()) {
                log("lib: unlink system call - error: No such file or directory");
                return Errors.ENOENT;
            }

            if (f.isDirectory()) {
                log("lib: unlink system call - error: Is a directory");
                return Errors.EISDIR;
            }

            try {
                boolean isDeleted = f.delete();
                log("lib: unlink system call - " + isDeleted);
                if (isDeleted) return 0;
            } catch (SecurityException e) {
                log("lib: unlink system call - error: Operation not permitted");
                return Errors.EPERM; // Operation not permitted
            }
            log("lib: unlink system call - error: I/O error");
            return -5; // I/O error
        }

        public void clientdone() {
        }

        private static void log(String s) {
            System.out.println(s);
        }
    }


    private static class FileHandlingFactory implements FileHandlingMaking {
        public FileHandling newclient() {
            return new FileHandler();
        }
    }

    public static void main(String[] args) throws IOException {
        globalFD = new AtomicInteger(1000);
        fileTable = new ConcurrentHashMap<>();
        System.out.println("proxy start");
        (new RPCreceiver(new FileHandlingFactory())).run();
    }
}
