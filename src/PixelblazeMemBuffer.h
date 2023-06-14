#ifndef PixelblazeMemBuffer_h
#define PixelblazeMemBuffer_h

#include "PixelblazeClient.h"

struct NamedBuffer {
    String name;
    uint8_t *buffer;
    size_t used;
};

class BufferStream : public Stream {
public:
    BufferStream(NamedBuffer *buff, size_t buffLen, size_t writeIdx, size_t readIdx, bool readable)
            : buff(buff), buffLen(buffLen), writeIdx(writeIdx), readIdx(readIdx), readable(readable) {}

    size_t write(uint8_t uint8) override {
        if (readable) {
            return 0;
        }

        if (writeIdx >= buffLen) {
            return 0;
        }

        buff->buffer[writeIdx] = uint8;
        buff->used++;
        writeIdx++;
        return 1;
    }

    int available() override {
        if (readable) {
            return writeIdx - readIdx;
        } else {
            return 0;
        }
    }

    int read() override {
        if (readable) {
            if (readIdx >= buffLen || writeIdx <= readIdx) {
                return -1;
            } else {
                int v = buff->buffer[readIdx];
                readIdx++;
                return v;
            }
        } else {
            return -1;
        }
    }

    int peek() override {
        if (readable) {
            if (readIdx >= buffLen || writeIdx <= readIdx) {
                return -1;
            } else {
                return buff->buffer[readIdx];
            }
        } else {
            return -1;
        }
    }

    virtual ~BufferStream() {
        //We don't own buff
    }

private:
    NamedBuffer *buff;
    size_t buffLen;
    size_t readIdx;
    size_t writeIdx;
    bool readable;
};

/**
 * Buffers large binary reads in memory
 */
class PixelblazeMemBuffer : public PixelblazeBuffer {
public:
    explicit PixelblazeMemBuffer(size_t numBuffers = 3, size_t buffBytes = 10000) :
            numBuffers(numBuffers), buffBytes(buffBytes) {
        buffers = new NamedBuffer *[numBuffers];
    }

    virtual ~PixelblazeMemBuffer() {
        //If any of our CloseableStreams are in the world, they're about to get hosed.

        for (size_t idx = 0; idx < allocated) {
            delete[] buffers[idx];
        }
    }

private:
    CloseableStream *makeWriteStream(String &key, bool append) override {
        int buffIdx = 0;
        int emptyIdx = -1;
        while (buffIdx < allocated) {
            NamedBuffer *thisBuffer = buffers[buffIdx];
            if (key.equals(thisBuffer->name)) {
                if (append) {
                    return new CloseableStream(
                            new BufferStream(thisBuffer, buffBytes, thisBuffer->used, 0, false)
                    );
                } else {
                    return new CloseableStream(
                            new BufferStream(thisBuffer, buffBytes, 0, 0, false)
                    );
                }
            } else if (!thisBuffer->name.length() && emptyIdx < 0) {
                emptyIdx = buffIdx;
            }

            buffIdx++;
        }

        if (emptyIdx >= 0) {
            NamedBuffer *thisBuffer = buffers[emptyIdx];
            thisBuffer->name = key;

            return new CloseableStream(
                    new BufferStream(thisBuffer, buffBytes, 0, 0, false)
            );
        }

        if (buffIdx >= numBuffers) {
            return nullptr;
        }

        buffers[buffIdx] = new NamedBuffer{key, new uint8_t[buffBytes], 0};
        allocated++;

        return new CloseableStream(
                new BufferStream(buffers[buffIdx], buffBytes, 0, 0, false)
        );
    };

    CloseableStream *makeReadStream(String &key) override {
        int buffIdx = 0;
        while (buffIdx < allocated) {
            NamedBuffer *thisBuffer = buffers[buffIdx];
            if (key.equals(thisBuffer->name)) {
                return new CloseableStream(
                        new BufferStream(thisBuffer, buffBytes, thisBuffer->used, 0, true)
                );
            }

            buffIdx++;
        }

        return nullptr;
    };

    void deleteStreamResults(String &key) override {
        int buffIdx = 0;
        while (buffIdx < allocated) {
            NamedBuffer *thisBuffer = buffers[buffIdx];
            if (key.equals(thisBuffer->name)) {
                thisBuffer->name = "";
                thisBuffer->used = 0;
                return;
            }

            buffIdx++;
        }
    };
private:
    NamedBuffer **buffers;
    size_t numBuffers;
    size_t buffBytes;
    size_t allocated = 0;
};

#endif
