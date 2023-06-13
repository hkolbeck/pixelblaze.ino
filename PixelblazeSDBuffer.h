#ifndef PixelblazeSDBuffer_h
#define PixelblazeSDBuffer_h

#include "PixelblazeClient.h"
#include <SD.h>

class PixelblazeSDBuffer : {
public:
    PixelblazeSDBuffer(String _root, bool (*_isTrash)(File)) {
        root = _root;
        isTrash = _isTrash;
    }

    virtual Stream makeWriteStream(String key, bool append) {
        String path = root + key;

        //SD docs are unclear about whether FILE_WRITE will nuke existing, and code is convoluted enough that
        //finding the impl being called eludes me. If it doesn't we'll have to nuke manually.
        return SD.open(path, append ? FILE_APPEND : FILE_WRITE, true);
    }

    virtual Stream makeReadStream(String key) {
        String path = root + key;
        return SD.open(path, FILE_READ);
    }

    virtual void cleanupStream(String key) {
        String path = root + key;
        if (!SD.remove(path)) {
            Serial.print("Failed to delete file: ");
            Serial.println(path);
        }
    }

    virtual void garbageCollect() {
        File rootDir = SD.open(root);
        if (!rootDir || !rootDir.isDirectory()) {
            Serial.print("Root dir doesn't exist or isn't a directory, can't garbage collect: ");
            Serial.println(root);
            return;
        }

        String rootWithSlash = root.endsWith("/") ? root : root + "/";
        walkTree(rootDir, rootWithSlash);
        rootDir.close();
    };

    void walkTree(File dir, String fullPath) {
        File file = dir.openNextFile();
        while (file) {
            if (file.isDirectory()) {
                walkTree(file, fullPath + file.name() + "/");
            } else {
                if (isTrash(file)) {
                    String filePath = fullPath + file.name();
                    file.close();

                    if (!SD.remove(filePath)) {
                        Serial.print("Failed to remove file: ");
                        Serial.println(filePath);
                    }

                    file = dir.openNextFile();
                    continue;
                }
            }

            file.close();
            file = dir.openNextFile();
        }
    }
private:
    String root;
    bool (*isTrash)(File);
}

#endif