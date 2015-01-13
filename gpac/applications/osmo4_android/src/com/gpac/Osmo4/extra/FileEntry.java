/**
 * $URL: https://gpac.svn.sourceforge.net/svnroot/gpac/trunk/gpac/applications/osmo4_android/src/com/gpac/Osmo4/extra/FileEntry.java $
 *
 * $LastChangedBy: enst_devs $ - $LastChangedDate: 2011-07-05 18:35:26 +0200 (mar. 05 juil. 2011) $
 */
package com.gpac.Osmo4.extra;

import java.io.File;

/**
 * @version $Revision: 3371 $
 * 
 */
public class FileEntry implements Comparable<FileEntry> {

    private final File file;

    private final String name;

    /**
     * Constructor
     * 
     * @param f
     */
    public FileEntry(File f) {
        this.file = f;
        this.name = f.getName();
    }

    /**
     * Constructor
     * 
     * @param f
     * @param name The name to use
     */
    public FileEntry(File f, String name) {
        this.file = f.getAbsoluteFile();
        this.name = name;
    }

    /**
     * Get the name of option
     * 
     * @return The name of option
     */
    public String getName() {
        return name;
    }

    @Override
    public int compareTo(FileEntry o) {
        return getName().toLowerCase().compareTo(o.getName());
    }

    /**
     * @return the file
     */
    public File getFile() {
        return file;
    }
}