import java.io.File;

public class Test {
    public static void main(String[] args) {
        File f = new File("/Users/zishenwen/Work/CMU/15640_DistributedSystems/project/project2/15440-p2/srcr");
        System.out.println(f.exists());
        System.out.println(f.isDirectory());
    }
}
