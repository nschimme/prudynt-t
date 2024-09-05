#ifndef PCM_BACKCHANNEL_SOURCE_HPP
#define PCM_BACKCHANNEL_SOURCE_HPP

#include "liveMedia.hh"
#include <vector>
#include <mutex>

// PCMBackchannelSource class declaration
class PCMBackchannelSource : public FramedSource {
public:
    static PCMBackchannelSource* createNew(UsageEnvironment& env);

protected:
    PCMBackchannelSource(UsageEnvironment& env);
    virtual ~PCMBackchannelSource();

    virtual void doGetNextFrame() override;

    // Adds a new client audio stream
    void addClientStream(const u_int8_t* data, unsigned dataSize);

    // Removes a client stream (e.g., when a client disconnects)
    void removeClientStream(unsigned clientId);

    // TODO: Add member variables and methods to handle PCM audio frames, taking into account Profile-T requirements.

private:
    void mixAndOutputToSpeaker();

    // Member variables to manage multiple client streams
    unsigned clientId; // The ID of the client sending the audio
    std::map<unsigned, std::vector<u_int8_t>> clientDataBuffers; // Data buffer per client
    std::mutex bufferMutex; // Mutex to protect access to the buffers

    // TODO: Implement an audio mixer to mix multiple client streams before sending them to the speaker.
};

#endif // PCM_BACKCHANNEL_SOURCE_HPP

