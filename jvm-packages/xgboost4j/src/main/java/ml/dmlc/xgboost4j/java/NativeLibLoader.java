/*
 Copyright (c) 2014, 2021 by Contributors

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */
package ml.dmlc.xgboost4j.java;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Field;
import java.util.Locale;

import cn.hutool.core.util.RuntimeUtil;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import static ml.dmlc.xgboost4j.java.NativeLibLoader.LibraryPathProvider.getLibraryPathFor;
import static ml.dmlc.xgboost4j.java.NativeLibLoader.LibraryPathProvider.getPropertyNameForLibrary;

/**
 * class to load native library
 *
 * @author hzx
 */
@SuppressWarnings("CommentedOutCode")
public class NativeLibLoader {
  private static final Log logger = LogFactory.getLog(NativeLibLoader.class);

  /**
   * Supported OS enum.
   */
  enum OS {
    WINDOWS("windows"), MACOS("macos"), LINUX("linux"), SOLARIS("solaris");

    final String name;

    OS(String name) {
      this.name = name;
    }

    /**
     * Detects the OS using the system properties.
     * Throws IllegalStateException if the OS is not recognized.
     *
     * @return The OS.
     */
    static OS detectOS() {
      String os = System.getProperty("os.name", "generic").toLowerCase(Locale.ENGLISH);
      if (os.contains("mac") || os.contains("darwin")) {
        return MACOS;
      } else if (os.contains("win")) {
        return WINDOWS;
      } else if (os.contains("nux")) {
        return LINUX;
      } else if (os.contains("sunos")) {
        return SOLARIS;
      } else {
        throw new IllegalStateException("Unsupported OS:" + os);
      }
    }

  }

  /**
   * Supported architecture enum.
   */
  enum Arch {
    X86_64("x86_64"), AARCH64("aarch64"), SPARC("sparc");

    final String name;

    Arch(String name) {
      this.name = name;
    }

    /**
     * Detects the chip architecture using the system properties.
     * Throws IllegalStateException if the architecture is not recognized.
     *
     * @return The architecture.
     */
    static Arch detectArch() {
      String arch = System.getProperty("os.arch", "generic").toLowerCase(Locale.ENGLISH);
      if (arch.startsWith("amd64") || arch.startsWith("x86_64")) {
        return X86_64;
      } else if (arch.startsWith("aarch64") || arch.startsWith("arm64")) {
        return AARCH64;
      } else if (arch.startsWith("sparc")) {
        return SPARC;
      } else {
        throw new IllegalStateException("Unsupported architecture:" + arch);
      }
    }
  }

  /**
   * Utility class to determine the path of a native library.
   */
  static class LibraryPathProvider {

    private static final String nativeResourcePath = "/lib";
    private static final String customNativeLibraryPathPropertyPrefix = "xgboostruntime.native.";

    static String getPropertyNameForLibrary(String libName) {
      return customNativeLibraryPathPropertyPrefix + libName;
    }

    /**
     * If a library-specific system property is set, this value is
     * being used without further processing.
     * Otherwise, the library path depends on the OS and architecture.
     *
     * @return path of the native library
     */
    static String getLibraryPathFor(OS os, Arch arch, String libName) {

      String libraryPath = System.getProperty(getPropertyNameForLibrary(libName));

      if (libraryPath == null) {
        libraryPath = getLibraryBasePathFor(os, arch) + System.mapLibraryName(libName);
      }

      logger.debug("Using path " + libraryPath + " for library with name " + libName);

      return libraryPath;
    }

    static String getLibraryBasePathFor(OS os, Arch arch) {
      return nativeResourcePath + "/" + getPlatformFor(os, arch) + "/";
    }

  }

  private static boolean initialized = false;
  private static final String[] libNames = new String[]{"xgboost4j"};
  private static final String ldPrefixPath = "./xgboost4j-libs";
  public static String ldPath;
  public static String ldBasePath;
  public static String libBasePath;

  static {
    /*
    String cpPath = ResourceUtil.getResource("", NativeLibLoader.class).getPath();
    cpPath = cpPath.substring(0, cpPath.length() - 1);
    cpPath = cpPath.replace("/" + NativeLibLoader.class.getPackage().getName().replaceAll("\\.", "/"), "");*/
    // String libBasePath = cpPath + getLibraryBasePathFor(os, arch);
    OS os = OS.detectOS();
    Arch arch = Arch.detectArch();
    ldBasePath = LibraryPathProvider.getLibraryBasePathFor(os, arch);
    libBasePath = ldPrefixPath + ldBasePath;
    ldPath = getLDPath();
    initLDLibrary();
  }

  @SuppressWarnings("StringConcatenationInLoop")
  private static void initLDLibrary() {
    String sysLibPaths = System.getProperty("java.library.path");
    if (!ldPath.isEmpty() && !sysLibPaths.contains(ldPath)) {
      // System.setProperty("java.library.path", sysLibPaths + ":" + ldPath);
      System.setProperty("java.library.path", ldPath);
    }
    /*
    try {
      Field field = ClassLoader.class.getDeclaredField("sys_paths");
      field.setAccessible(true);
      field.set(null, null);
    } catch (Exception e) {
      e.printStackTrace();
    }
    */
    try {
      String zippedLibPath = createTempFileFromResource(ldBasePath + "lib.zip");
      /*
      String exportLd = "export LD_LIBRARY_PATH=" + ldPath;
      String cmd = "([ -d /tmp/xgboost4j ] || (mkdir /tmp/xgboost4j && " +
        "unzip " + zippedLibPath + " -d /tmp/xgboost4j/)) && (grep -q '" + exportLd + "' ~/.bashrc || (echo '" +
        exportLd + "' >> ~/.bashrc && source ~/.bashrc && " + exportLd + "))";
      */
      // String cmd = "rm -rf " + ldBasePath + " && mkdir " + ldBasePath + " && unzip " + zippedLibPath +
      // " -d " + ldBasePath;
      String cmd = "[ -d " + ldPrefixPath + " ] || (mkdir " + ldPrefixPath + " && unzip " + zippedLibPath +
        " -d " + ldPrefixPath + " && mv ";
      String[] paths = {"boost@lib", "grpc@lib64", "grpc@lib"};
      for (String path : paths) {
        String[] ps = path.split("@");
        String libPath = libBasePath + (ps.length == 2 ? (ps[0] + "/" + ps[1]) : path);
        cmd += libPath + "/* ";
      }
      cmd += libBasePath + "lib/)";

      Process p = RuntimeUtil.exec("sh", "-c", cmd);
      p.waitFor();
      p.destroy();
    } catch (Exception e) {
      e.printStackTrace();
      throw new RuntimeException(e);
    }
    System.out.println("java.library.path: " + System.getProperty("java.library.path"));
    System.out.println("LD_LIBRARY_PATH: " + System.getenv("LD_LIBRARY_PATH"));
  }

  private static String getLDPath() {
    // String[] paths = {"lib", "boost@lib", "grpc@lib64", "grpc@lib"};
    String[] paths = {"lib"};
    StringBuilder libPaths = new StringBuilder();
    for (String path : paths) {
      String[] ps = path.split("@");
      String libPath = libBasePath + (ps.length == 2 ? (ps[0] + "/" + ps[1]) : path);
      libPaths.append(":").append(libPath);
      addNativeDir(libPath);
    }
    String lps = libPaths.toString();
    if (!lps.isEmpty()) {
      lps = lps.substring(1);
    }
    return lps;
  }

  private static void addNativeDir(String libPath) {
    try {
      Field field = ClassLoader.class.getDeclaredField("usr_paths");
      field.setAccessible(true);
      String[] paths = (String[]) field.get(null);
      String[] tmp = paths;
      for (int i = 0; i < paths.length; ++i) {
        String path = tmp[i];
        if (libPath.equals(path)) {
          return;
        }
      }

      tmp = new String[paths.length + 1];
      System.arraycopy(paths, 0, tmp, 0, paths.length);
      tmp[paths.length] = libPath;
      field.set(null, tmp);
    } catch (IllegalAccessException e) {
      logger.error(e.getMessage());
      throw new RuntimeException("Failed to get permissions to set library path");
    } catch (NoSuchFieldException e) {
      logger.error(e.getMessage());
      throw new RuntimeException("Failed to get field handle to set library path");
    }
  }

  /**
   * Loads the XGBoost library.
   * <p>
   * Throws IllegalStateException if the architecture or OS is unsupported.
   * <ul>
   *   <li>Supported OS: macOS, Windows, Linux, Solaris.</li>
   *   <li>Supported Architectures: x86_64, aarch64, sparc.</li>
   * </ul>
   * Throws UnsatisfiedLinkError if the library failed to load its dependencies.
   */
  static synchronized void initXGBoost() {
    if (!initialized) {
      OS os = OS.detectOS();
      Arch arch = Arch.detectArch();
      for (String libName : libNames) {
        String libraryPathInJar = getLibraryPathFor(os, arch, libName);
        loadLibraryFromJar(os, arch, libName, libraryPathInJar);
      }
      initialized = true;
    }
  }

  private static void loadLibraryFromJar(OS os, Arch arch, String libName, String libraryPathInJar) {
    try {
      loadLibraryFromJar(libraryPathInJar);
    } catch (UnsatisfiedLinkError ule) {
      String failureMessageIncludingOpenMPHint = "Failed to load " + libName +
        " due to missing native dependencies for platform " + getPlatformFor(os, arch) +
        ", this is likely due to a missing OpenMP dependency";

      switch (os) {
        case WINDOWS:
          logger.error(failureMessageIncludingOpenMPHint);
          logger.error("You may need to install 'vcomp140.dll' or 'libgomp-1.dll'");
          break;
        case MACOS:
          logger.error(failureMessageIncludingOpenMPHint);
          logger.error("You may need to install 'libomp.dylib', via `brew install libomp` " + "or similar");
          break;
        case LINUX:
          logger.error(failureMessageIncludingOpenMPHint);
          logger.error("You may need to install 'libgomp.so' (or glibc) via your package " + "manager.");
          logger.error("Alternatively, if your Linux OS is musl-based, you should set " +
            "the path for the native library " + libName + " " + "via the system property " +
            getPropertyNameForLibrary(libName));
          break;
        case SOLARIS:
          logger.error(failureMessageIncludingOpenMPHint);
          logger.error("You may need to install 'libgomp.so' (or glibc) via your package " + "manager.");
          break;
      }
      throw ule;
    } catch (IOException ioe) {
      logger.error("Failed to load " + libName + " library from jar for platform " +
        getPlatformFor(os, arch));
      throw new RuntimeException(ioe);
    }
  }

  /**
   * Loads library from current JAR archive
   * <p/>
   * The file from JAR is copied into system temporary directory and then loaded.
   * The temporary file is deleted after exiting.
   * Method uses String as filename because the pathname is "abstract", not system-dependent.
   * <p/>
   * The restrictions of {@link File#createTempFile(java.lang.String, java.lang.String)} apply to
   * {@code path}.
   *
   * @param path The filename inside JAR as absolute path (beginning with '/'),
   *             e.g. /package/File.ext
   * @throws IOException              If temporary file creation or read/write operation fails
   * @throws IllegalArgumentException If source file (param path) does not exist
   * @throws IllegalArgumentException If the path is not absolute or if the filename is shorter than
   *                                  three characters
   */
  private static void loadLibraryFromJar(String path) throws IOException, IllegalArgumentException {
    String temp = createTempFileFromResource(path);
    System.load(temp);
  }

  /**
   * Create a temp file that copies the resource from current JAR archive
   * <p/>
   * The file from JAR is copied into system temp file.
   * The temporary file is deleted after exiting.
   * Method uses String as filename because the pathname is "abstract", not system-dependent.
   * <p/>
   * The restrictions of {@link File#createTempFile(java.lang.String, java.lang.String)} apply to
   * {@code path}.
   *
   * @param path Path to the resources in the jar
   * @return The created temp file.
   * @throws IOException              If it failed to read the file.
   * @throws IllegalArgumentException If the filename is invalid.
   */
  static String createTempFileFromResource(String path) throws IOException, IllegalArgumentException {
    // Obtain filename from path
    if (!path.startsWith("/")) {
      throw new IllegalArgumentException("The path has to be absolute (start with '/').");
    }

    String[] parts = path.split("/");
    String filename = (parts.length > 1) ? parts[parts.length - 1] : null;

    // Split filename to prefix and suffix (extension)
    String prefix = "";
    String suffix = null;
    if (filename != null) {
      parts = filename.split("\\.", 2);
      prefix = parts[0];
      suffix = (parts.length > 1) ? "." + parts[parts.length - 1] : null; // Thanks, davs! :-)
    }

    // Check if the filename is okay
    if (filename == null || prefix.length() < 3) {
      throw new IllegalArgumentException("The filename has to be at least 3 characters long.");
    }
    // Prepare temporary file
    File temp = File.createTempFile(prefix, suffix);
    temp.deleteOnExit();

    if (!temp.exists()) {
      throw new FileNotFoundException("File " + temp.getAbsolutePath() + " does not exist.");
    }

    // Prepare buffer for data copying
    byte[] buffer = new byte[1024];
    int readBytes;

    // Open and check input stream
    try (InputStream is = NativeLibLoader.class.getResourceAsStream(path);
         OutputStream os = new FileOutputStream(temp)) {
      if (is == null) {
        throw new FileNotFoundException("File " + path + " was not found inside JAR.");
      }

      // Open output stream and copy data between source file in JAR and the temporary file
      while ((readBytes = is.read(buffer)) != -1) {
        os.write(buffer, 0, readBytes);
      }
    }

    return temp.getAbsolutePath();
  }

  private static String getPlatformFor(OS os, Arch arch) {
    return os.name + "/" + arch.name;
  }

}
