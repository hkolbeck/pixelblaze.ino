#ifndef PixelblazeSDBuffer_h
#define PixelblazeSDBuffer_h

#include "PixelblazeClient.h"
#include <SD.h>

//Should only ever be invoked for streams that are actually files
void closeFileStream(Stream* stream) {
    File* asFile = (File*) stream;
    asFile->close();
}

size_t bulkWrite(Stream* stream, const uint8_t *buffer, size_t size) {
    File* asFile = (File*) stream;
    return asFile->write(buffer, size);
}

class PixelblazeSDBuffer : PixelblazeBuffer {
public:
    PixelblazeSDBuffer(String &_root, bool (*_isTrash)(File)) {
        root = _root;
        isTrash = _isTrash;
    }

    CloseableStream* makeWriteStream(String &bufferId, bool append) override {
        String path = root + bufferId;

        //SD docs are unclear about whether FILE_WRITE will nuke existing, and code is convoluted enough that
        //finding the impl being called eludes me. If it doesn't we'll have to nuke manually.
        File f = SD.open(path, append ? O_APPEND : O_WRITE);
        return new CloseableStream(&f, bulkWrite, closeFileStream);
    }

    CloseableStream* makeReadStream(String &bufferId) override {
        String path = root + bufferId;
        File f = SD.open(path, FILE_READ);
        return new CloseableStream(&f, bulkWrite, closeFileStream);
    }

    void deleteStreamResults(String &bufferId) override {
        String path = root + bufferId;
        if (!SD.remove(path)) {
            Serial.print(F("Failed to delete file: "));
            Serial.println(path);
        }
    }

    void garbageCollect() override {
        File rootDir = SD.open(root);
        if (!rootDir || !rootDir.isDirectory()) {
            Serial.print(F("Root dir doesn't exist or isn't a directory, can't garbage collect: "));
            Serial.println(root);
            return;
        }

        String rootWithSlash = root.endsWith("/") ? root : root + "/";
        walkTree(rootDir, rootWithSlash);
        rootDir.close();
    }

    //The tree should be very shallow, leaving this recursive for now
    void walkTree(File &dir, String &fullPath) {
        File file = dir.openNextFile();
        while (file) {
            if (file.isDirectory()) {
                walkTree(file, fullPath + file.name() + "/");
            } else {
                if (isTrash(file)) {
                    String filePath = fullPath + file.name();
                    file.close();

                    if (!SD.remove(filePath)) {
                        Serial.print(F("Failed to remove file: "));
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
};

#endif